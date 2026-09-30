#include "Preflight.h"

#include "LogService.h"
#include "ProcessRunner.h"
#include "core/PluginSet.h"
#include "core/SemVer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QThread>

using PreflightDecision::CheckResult;
using PreflightDecision::Reason;
using PreflightDecision::RepairOutcome;

namespace {
constexpr int kRepairTimeoutMs = 300 * 1000;

bool readJsonObject(const QString &path, QJsonObject *out, QString *error) {
    QFile file(path);
    if (!file.exists()) {
        if (error)
            *error = QStringLiteral("找不到 %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 %1：%2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("%1 解析失败：%2").arg(QDir::toNativeSeparators(path),
                         parseError.error != QJsonParseError::NoError ? parseError.errorString()
                                                                      : QStringLiteral("根节点不是对象"));
        return false;
    }
    *out = doc.object();
    return true;
}

// 从 startDir 起按 Node 解析规则逐级向上找 node_modules/<dep>/package.json；
// 名为 node_modules 的目录本身不再拼 node_modules。找不到返回空串
QString resolveDependency(const QString &startDir, const QString &dep) {
    QDir dir(startDir);
    while (true) {
        if (dir.dirName().compare(QStringLiteral("node_modules"), Qt::CaseInsensitive) != 0) {
            const QString candidate = dir.filePath(QStringLiteral("node_modules/") + dep + QStringLiteral("/package.json"));
            if (QFileInfo::exists(candidate))
                return candidate;
        }
        if (!dir.cdUp())
            break;
    }
    return {};
}

// 只校验 semver 范围；npm:/file:/git/workspace: 等协议、github 简写与 dist-tag（不含数字）只检查存在
bool rangeCheckable(const QString &range) {
    if (range.contains(QLatin1Char(':')) || range.contains(QLatin1Char('/')))
        return false;
    for (const QChar c : range) {
        if (c.isDigit())
            return true;
    }
    return false;
}

QString failureText(const CheckResult &r) {
    return (r.failure == Reason::Missing ? QStringLiteral("包缺失") : QStringLiteral("依赖不可解析"))
        + (r.detail.isEmpty() ? QString() : QStringLiteral("：") + r.detail);
}
} // namespace

Preflight::Preflight(ProcessRunner *runner, Quarantine *quarantine, QObject *parent)
    : QObject(parent), m_runner(runner), m_quarantine(quarantine) {}

Preflight::~Preflight() {
    // 工作线程只读文件系统，等它们结束即可；结束后不再回调
    for (QThread *t : std::as_const(m_threads)) {
        t->wait();
        delete t;
    }
    m_threads.clear();
}

QString Preflight::reasonText(const QString &reason) {
    const auto r = PreflightDecision::reasonFromString(reason);
    if (!r)
        return reason;
    switch (*r) {
    case Reason::Missing: return QStringLiteral("包缺失");
    case Reason::DepUnresolved: return QStringLiteral("依赖不可解析");
    case Reason::InstallFailed: return QStringLiteral("修复安装失败");
    case Reason::InstallTimeout: return QStringLiteral("修复安装超时");
    case Reason::NoPackageManager: return QStringLiteral("包管理器不可用");
    }
    return reason;
}

QString Preflight::queueKey(const QString &profileDir) {
    return QDir::cleanPath(QDir(profileDir).absolutePath()).toLower();
}

CheckResult Preflight::checkPlugin(const QString &profileDir, const QString &name) {
    CheckResult r;
    r.name = name;
    const QString link = QDir(profileDir).filePath(QStringLiteral("node_modules/") + name);
    const QString linkPkg = link + QStringLiteral("/package.json");
    if (!QFileInfo::exists(linkPkg)) {
        r.ok = false;
        r.failure = Reason::Missing;
        r.detail = QStringLiteral("node_modules 中找不到 %1").arg(name);
        return r;
    }
    // 真实路径（pnpm 下是 .pnpm/<pkg>@<ver>/node_modules/<pkg>），依赖从这里向上解析
    QString realDir = QFileInfo(link).canonicalFilePath();
    if (realDir.isEmpty())
        realDir = link;
    QJsonObject pkg;
    QString error;
    if (!readJsonObject(realDir + QStringLiteral("/package.json"), &pkg, &error)) {
        r.ok = false;
        r.failure = Reason::Missing;
        r.detail = error;
        return r;
    }
    const QJsonObject deps = pkg.value(QStringLiteral("dependencies")).toObject();
    for (auto it = deps.constBegin(); it != deps.constEnd(); ++it) {
        const QString dep = it.key();
        const QString range = it.value().toString().trimmed();
        const QString found = resolveDependency(realDir, dep);
        if (found.isEmpty()) {
            r.ok = false;
            r.failure = Reason::DepUnresolved;
            r.detail = QStringLiteral("%1（找不到）").arg(dep);
            return r;
        }
        if (!rangeCheckable(range))
            continue;
        QJsonObject depPkg;
        if (!readJsonObject(found, &depPkg, nullptr))
            continue; // 存在但读不了版本：不据此判失败
        const QString versionText = depPkg.value(QStringLiteral("version")).toString();
        const auto version = SemVer::parse(versionText);
        if (!version)
            continue;
        if (!SemVer::satisfies(*version, range)) {
            r.ok = false;
            r.failure = Reason::DepUnresolved;
            r.detail = QStringLiteral("%1（已安装 %2，不满足 %3）").arg(dep, versionText, range);
            return r;
        }
    }
    return r;
}

Preflight::Scan Preflight::scan(const QString &profileDir) {
    Scan s;
    QJsonObject pkg;
    if (!readJsonObject(QDir(profileDir).filePath(QStringLiteral("package.json")), &pkg, &s.error))
        return s;
    s.ok = true;
    QSet<QString> seen;
    for (const QString &name : PluginSet::bundles(pkg)) {
        // Core_Package 随 dsh 自带，不在 profile 的 node_modules 里
        if (name.isEmpty() || PluginSet::isCorePackage(name) || seen.contains(name))
            continue;
        seen.insert(name);
        s.results.append(checkPlugin(profileDir, name));
    }
    return s;
}

void Preflight::run(const Request &request, Done done) {
    const QString key = queueKey(request.profileDir);
    QList<Job> &queue = m_queues[key];
    Job job;
    job.request = request;
    job.done = std::move(done);
    queue.append(job);
    if (queue.size() > 1)
        log(request, QStringLiteral("自检排队：同一 profile 有其他泊位正在自检或修复"));
    startNext(key);
}

void Preflight::startNext(const QString &key) {
    auto it = m_queues.find(key);
    if (it == m_queues.end() || it->isEmpty() || it->first().started)
        return;
    Job &job = it->first();
    job.started = true;
    log(job.request, QStringLiteral("自检开始：profile %1").arg(job.request.profile));
    scanAsync(job.request.profileDir, [this, key](const Scan &s) { onFirstScan(key, s); });
}

void Preflight::scanAsync(const QString &profileDir, std::function<void(const Scan &)> then) {
    auto result = std::make_shared<Scan>();
    QThread *thread = QThread::create([result, profileDir]() { *result = scan(profileDir); });
    m_threads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread, result, then = std::move(then)]() {
        m_threads.removeOne(thread);
        thread->deleteLater();
        then(*result);
    });
    thread->start();
}

void Preflight::onFirstScan(const QString &key, const Scan &s) {
    Job &job = m_queues[key].first();
    if (!s.ok) {
        Result result;
        result.aborted = true;
        result.error = s.error;
        log(job.request, QStringLiteral("自检中止：profile 配置无法读取（%1），未启动，未隔离任何插件").arg(s.error));
        complete(key, result);
        return;
    }
    job.before = s.results;
    bool allOk = true;
    for (const CheckResult &r : s.results) {
        allOk = allOk && r.ok;
        log(job.request, r.ok ? QStringLiteral("自检通过：%1").arg(r.name)
                              : QStringLiteral("自检未通过：%1（%2）").arg(r.name, failureText(r)));
    }
    if (allOk) {
        finish(key, RepairOutcome::NotAttempted, std::nullopt);
        return;
    }
    repair(key);
}

void Preflight::repair(const QString &key) {
    Job &job = m_queues[key].first();
    const Request &req = job.request;
    if (req.exe.trimmed().isEmpty() || !m_runner) {
        log(req, QStringLiteral("跳过修复：找不到包管理器（dsh 可执行文件未配置）"));
        onRepaired(key, RepairOutcome::NoPackageManager);
        return;
    }
    const QStringList args = {QStringLiteral("plugin"), QStringLiteral("--profile"), req.profile,
                              QStringLiteral("install")};
    ProcessOptions options;
    if (!req.home.isEmpty())
        options.env.insert(QStringLiteral("DSH_HOME"), req.home);
    options.cwd = req.profileDir;
    options.timeoutMs = kRepairTimeoutMs;
    options.tailLines = 20;
    log(req, QStringLiteral("修复开始：%1 %2").arg(req.exe, args.join(QLatin1Char(' '))));
    ProcessTask *task = m_runner->run(req.exe, args, options);
    connect(task, &ProcessTask::finished, this,
            [this, key, task](bool ok, int code, const QString &tail, bool timedOut) {
                auto it = m_queues.find(key);
                if (it == m_queues.end() || it->isEmpty())
                    return;
                const Request &r = it->first().request;
                RepairOutcome outcome;
                if (task->failedToStart()) {
                    log(r, QStringLiteral("修复失败：无法启动包管理器（%1）").arg(task->errorString()));
                    outcome = RepairOutcome::NoPackageManager;
                } else if (timedOut) {
                    log(r, QStringLiteral("修复结束：超时（%1 秒）已终止").arg(kRepairTimeoutMs / 1000));
                    outcome = RepairOutcome::TimedOut;
                } else if (ok && code == 0) {
                    log(r, QStringLiteral("修复结束：exit 0"));
                    outcome = RepairOutcome::Succeeded;
                } else {
                    log(r, QStringLiteral("修复结束：exit %1%2").arg(QString::number(code),
                        tail.trimmed().isEmpty() ? QString() : QStringLiteral("\n") + tail.trimmed()));
                    outcome = RepairOutcome::Failed;
                }
                onRepaired(key, outcome);
            });
}

void Preflight::onRepaired(const QString &key, RepairOutcome outcome) {
    // 包管理器不可用时没有修复，直接按修复前结果隔离
    if (outcome == RepairOutcome::NoPackageManager) {
        finish(key, outcome, std::nullopt);
        return;
    }
    const QString dir = m_queues[key].first().request.profileDir;
    scanAsync(dir, [this, key, outcome](const Scan &s) {
        Job &job = m_queues[key].first();
        if (!s.ok) {
            Result result;
            result.aborted = true;
            result.error = s.error;
            log(job.request, QStringLiteral("自检中止：修复后 profile 配置无法读取（%1），未启动，未隔离任何插件").arg(s.error));
            complete(key, result);
            return;
        }
        for (const CheckResult &r : s.results) {
            log(job.request, r.ok ? QStringLiteral("复检通过：%1").arg(r.name)
                                  : QStringLiteral("复检未通过：%1（%2）").arg(r.name, failureText(r)));
        }
        finish(key, outcome, s.results);
    });
}

void Preflight::finish(const QString &key, RepairOutcome outcome,
                       const std::optional<QList<CheckResult>> &after) {
    Job &job = m_queues[key].first();
    const Request req = job.request;
    Result result;
    result.decision = PreflightDecision::decide(job.before, after, outcome);

    if (!result.decision.quarantined.isEmpty()) {
        QStringList names;
        for (const auto &q : result.decision.quarantined)
            names.append(q.name);
        QString error;
        if (!removeBundles(req.profileDir, names, &error)) {
            // 写不回 package.json：不记隔离，照常启动（dsh 会自己报加载错误）
            log(req, QStringLiteral("隔离失败：无法从 bundles 移除 %1（%2）").arg(names.join(QStringLiteral("、")), error));
        } else {
            const QString at = QDateTime::currentDateTime().toString(Qt::ISODate);
            for (const auto &q : result.decision.quarantined) {
                QuarantineEntry e;
                e.home = req.home;
                e.profile = req.profile;
                e.name = q.name;
                e.at = at;
                e.reason = PreflightDecision::reasonString(q.reason);
                e.detail = q.detail;
                result.quarantined.append(e);
                log(req, QStringLiteral("隔离：%1（%2%3）").arg(q.name, reasonText(e.reason),
                    q.detail.isEmpty() ? QString() : QStringLiteral("：") + q.detail));
            }
            if (m_quarantine && !m_quarantine->add(result.quarantined))
                log(req, QStringLiteral("quarantine.json 写入失败，隔离记录可能在重启后丢失"));
        }
    }
    log(req, QStringLiteral("自检完成：%1 个插件可用，%2 个被隔离")
                 .arg(result.decision.remaining.size())
                 .arg(result.quarantined.size()));
    complete(key, result);
}

void Preflight::complete(const QString &key, const Result &result) {
    auto it = m_queues.find(key);
    if (it == m_queues.end() || it->isEmpty())
        return;
    Job job = it->takeFirst();
    if (it->isEmpty())
        m_queues.erase(it);
    // done 里可能再次 run（同 key 入队并自行启动），startNext 会跳过已启动的队首
    if (job.done)
        job.done(result);
    startNext(key);
}

void Preflight::log(const Request &request, const QString &message) const {
    LogService::appendMarker(request.logPath, QStringLiteral("[自检] ") + message, request.secrets);
}

bool Preflight::removeBundles(const QString &profileDir, const QStringList &names, QString *error) {
    const QString path = QDir(profileDir).filePath(QStringLiteral("package.json"));
    QJsonObject pkg;
    if (!readJsonObject(path, &pkg, error))
        return false;
    for (const QString &name : names)
        pkg = PluginSet::setEnabled(pkg, name, false);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(pkg).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
