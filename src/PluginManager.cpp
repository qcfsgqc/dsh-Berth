#include "PluginManager.h"

#include "ProcessRunner.h"
#include "core/AffectedSet.h"
#include "core/BatchPlan.h"
#include "core/PackageSpec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

QString packageJsonPath(const QString &dir) {
    return QDir(dir).filePath(QStringLiteral("package.json"));
}

QString native(const QString &path) {
    return QDir::toNativeSeparators(path);
}

bool readBytes(const QString &dir, QByteArray *bytes, QString *error) {
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        *error = QStringLiteral("profile 目录不存在：%1").arg(native(dir));
        return false;
    }
    const QString path = packageJsonPath(dir);
    QFile file(path);
    if (!file.exists()) {
        *error = QStringLiteral("找不到 %1").arg(native(path));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取 %1：%2").arg(native(path), file.errorString());
        return false;
    }
    *bytes = file.readAll();
    return true;
}

bool parse(const QString &dir, const QByteArray &bytes, QJsonObject *root, QString *error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("%1 解析失败：%2（偏移 %3）")
                     .arg(native(packageJsonPath(dir)), parseError.errorString())
                     .arg(parseError.offset);
        return false;
    }
    if (!doc.isObject()) {
        *error = QStringLiteral("%1 根节点不是对象").arg(native(packageJsonPath(dir)));
        return false;
    }
    *root = doc.object();
    return true;
}

bool writeBytes(const QString &dir, const QByteArray &bytes, QString *error) {
    const QString path = packageJsonPath(dir);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("无法写入 %1：%2").arg(native(path), file.errorString());
        return false;
    }
    file.write(bytes);
    if (!file.commit()) {
        *error = QStringLiteral("写入 %1 失败：%2").arg(native(path), file.errorString());
        return false;
    }
    return true;
}

// 文件内容与快照不同时写回快照（QSaveFile 失败通常不会动原文件，这里兜底）
void restoreSnapshot(const QString &dir, const QByteArray &snapshot) {
    QByteArray current;
    QString ignored;
    if (readBytes(dir, &current, &ignored) && current == snapshot)
        return;
    writeBytes(dir, snapshot, &ignored);
}

// 快照 → 只改 bundles 中这一项 → QSaveFile 写出；失败时恢复快照。状态已符合时不写文件。
bool applyEnabled(const QString &dir, const QString &name, bool on, QString *error) {
    QByteArray snapshot;
    if (!readBytes(dir, &snapshot, error))
        return false;
    QJsonObject root;
    if (!parse(dir, snapshot, &root, error))
        return false;
    if (PluginSet::isEnabled(root, name) == on)
        return true;
    const QJsonObject next = PluginSet::setEnabled(root, name, on);
    if (!writeBytes(dir, QJsonDocument(next).toJson(QJsonDocument::Indented), error)) {
        restoreSnapshot(dir, snapshot);
        return false;
    }
    return true;
}

QVariantMap result(bool ok, const QString &error = {}) {
    QVariantMap map;
    map.insert(QStringLiteral("ok"), ok);
    map.insert(QStringLiteral("error"), error);
    return map;
}

// 进程输出尾部取最后一行非空内容作为失败原因
QString lastLine(const QString &tail) {
    const QStringList lines = tail.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QString line = it->trimmed();
        if (!line.isEmpty())
            return line;
    }
    return {};
}

constexpr int kCatalogTimeoutMs = 15000;
// 安装经 pnpm 下载依赖，给足时间
constexpr int kInstallTimeoutMs = 10 * 60 * 1000;

QVariantMap entryToMap(const CatalogEntry &e) {
    QVariantMap item;
    item.insert(QStringLiteral("name"), e.name);
    item.insert(QStringLiteral("description"), e.description);
    item.insert(QStringLiteral("stable"), e.stable);
    item.insert(QStringLiteral("beta"), e.beta);
    item.insert(QStringLiteral("alpha"), e.alpha);
    return item;
}

QVariantList entriesToList(const QList<CatalogEntry> &entries) {
    QVariantList out;
    for (const CatalogEntry &e : entries)
        out.append(entryToMap(e));
    return out;
}

QStringList dependencyNames(const QJsonObject &root) {
    return root.value(QStringLiteral("dependencies")).toObject().keys();
}

} // namespace

PluginManager::PluginManager(ProcessRunner *runner, QObject *parent)
    : QObject(parent), m_runner(runner) {}

void PluginManager::setOps(Ops ops) {
    m_ops = std::move(ops);
}

void PluginManager::setNetwork(QNetworkAccessManager *nam) {
    m_nam = nam;
}

QString PluginManager::profileDir(const QString &home, const QString &profile) const {
    const QString resolved = m_ops.resolveHome ? m_ops.resolveHome(home) : home;
    return resolved + QStringLiteral("/profiles/") + profile;
}

QString PluginManager::keyOf(const QString &home, const QString &profile) const {
    const QString resolved = m_ops.resolveHome ? m_ops.resolveHome(home) : home;
    return AffectedSet::normalizeHome(resolved).toLower() + QLatin1Char('|') + profile;
}

QVariantList PluginManager::targets() const {
    QVariantList out;
    QStringList keys;
    auto add = [&](const QString &home, const QString &profile) {
        if (profile.isEmpty())
            return;
        const QString key = keyOf(home, profile);
        if (keys.contains(key))
            return;
        keys.append(key);
        QVariantMap item;
        item.insert(QStringLiteral("home"), home);
        item.insert(QStringLiteral("profile"), profile);
        item.insert(QStringLiteral("label"), profile + QStringLiteral("  ·  ") + native(home));
        item.insert(QStringLiteral("key"), key);
        out.append(item);
    };
    if (!m_ops.resolveHome)
        return out;
    const QString defaultHome = m_ops.resolveHome({});
    if (m_ops.knownProfiles) {
        for (const QString &name : m_ops.knownProfiles())
            add(defaultHome, name);
    }
    if (m_ops.instances) {
        for (const Instance &item : m_ops.instances())
            add(m_ops.resolveHome(item.dshHome), item.profile);
    }
    return out;
}

QVariantMap PluginManager::list(const QString &home, const QString &profile, bool showCore) const {
    QVariantMap map;
    QVariantList plugins;
    const QString dir = profileDir(home, profile);
    QByteArray bytes;
    QJsonObject root;
    QString error;
    const bool ok = readBytes(dir, &bytes, &error) && parse(dir, bytes, &root, &error);
    if (ok) {
        const QHash<QString, QString> quarantined =
            m_ops.quarantined ? m_ops.quarantined(home, profile) : QHash<QString, QString>();
        for (const PluginSet::Row &row : PluginSet::list(root, showCore, quarantined.keys())) {
            QVariantMap item;
            item.insert(QStringLiteral("name"), row.name);
            item.insert(QStringLiteral("version"), row.version);
            item.insert(QStringLiteral("displayVersion"), row.displayVersion);
            item.insert(QStringLiteral("enabled"), row.enabled);
            item.insert(QStringLiteral("core"), row.core);
            item.insert(QStringLiteral("quarantined"), row.quarantined);
            item.insert(QStringLiteral("quarantineReason"), quarantined.value(row.name));
            plugins.append(item);
        }
    }
    map.insert(QStringLiteral("ok"), ok);
    map.insert(QStringLiteral("error"), ok ? QString() : error);
    map.insert(QStringLiteral("writable"), ok);
    map.insert(QStringLiteral("plugins"), plugins);
    return map;
}

bool PluginManager::isBusy(const QString &home, const QString &profile) const {
    return m_jobs.contains(keyOf(home, profile));
}

QVariantMap PluginManager::progress() const {
    QVariantMap map;
    for (auto it = m_jobs.cbegin(); it != m_jobs.cend(); ++it) {
        QVariantMap item;
        item.insert(QStringLiteral("done"), it->index);
        item.insert(QStringLiteral("total"), int(it->names.size()));
        item.insert(QStringLiteral("op"), it->op);
        map.insert(it.key(), item);
    }
    return map;
}

QStringList PluginManager::affected(const QString &home, const QString &profile) const {
    if (!m_ops.instances || !m_ops.resolveHome)
        return {};
    return AffectedSet::affected(m_ops.instances(), m_ops.resolveHome(home), profile, m_ops.resolveHome({}));
}

QVariantMap PluginManager::setEnabled(const QString &home, const QString &profile, const QString &name, bool on) {
    const QString verb = on ? QStringLiteral("启用") : QStringLiteral("禁用");
    if (isBusy(home, profile) || (m_ops.externalBusy && m_ops.externalBusy()))
        return result(false, QStringLiteral("%1「%2」失败：该 profile 有操作正在进行").arg(verb, name));
    QString error;
    if (!applyEnabled(profileDir(home, profile), name, on, &error))
        return result(false, QStringLiteral("%1「%2」失败：%3").arg(verb, name, error));

    PluginSet::BatchSummary summary;
    summary.record(name, true);
    emit pluginsChanged(home, profile);
    const QStringList ids = affected(home, profile);
    if (summary.needsRestartHint(ids))
        emit restartHintRequested(home, profile, ids);
    return result(true);
}

QVariantMap PluginManager::batch(const QString &home, const QString &profile, const QString &op,
                                 const QStringList &names) {
    if (op != QLatin1String("enable") && op != QLatin1String("disable") && op != QLatin1String("uninstall"))
        return result(false, QStringLiteral("未知的批量操作：%1").arg(op));
    if (names.isEmpty())
        return result(false, QStringLiteral("未选中任何插件"));
    const QString key = keyOf(home, profile);
    if (m_jobs.contains(key) || (m_ops.externalBusy && m_ops.externalBusy()))
        return result(false, QStringLiteral("该 profile 有操作正在进行"));

    const QString dir = profileDir(home, profile);
    // package.json 读不了时整批拒绝（与列表的"禁用写操作"一致）
    QByteArray bytes;
    QJsonObject root;
    QString error;
    if (!readBytes(dir, &bytes, &error) || !parse(dir, bytes, &root, &error))
        return result(false, error);

    if (op == QLatin1String("uninstall")) {
        // 与单个卸载一致：使用该 profile 的泊位须都已停止，且本机需要 pnpm
        QStringList busyNames;
        const QString resolved = m_ops.resolveHome ? m_ops.resolveHome(home) : home;
        const QString defaultHome = m_ops.resolveHome ? m_ops.resolveHome({}) : QString();
        if (m_ops.instances) {
            for (const Instance &item : m_ops.instances()) {
                const QString itemHome = item.dshHome.isEmpty() ? defaultHome : item.dshHome;
                if (item.profile == profile && AffectedSet::sameHome(itemHome, resolved)
                    && BatchPlan::isActiveState(item.status))
                    busyNames.append(item.name);
            }
        }
        if (!busyNames.isEmpty())
            return result(false, QStringLiteral("先停止：") + busyNames.join(QStringLiteral("、")));
        if (QStandardPaths::findExecutable(QStringLiteral("pnpm")).isEmpty())
            return result(false, QStringLiteral("需要安装 pnpm"));
    }

    // 去重但保持选中顺序
    QStringList ordered;
    for (const QString &name : names) {
        if (!name.isEmpty() && !ordered.contains(name))
            ordered.append(name);
    }

    Job job;
    job.home = home;
    job.profile = profile;
    job.dir = dir;
    job.op = op;
    job.names = ordered;
    m_jobs.insert(key, job);
    emit busyChanged();
    // 下一轮事件循环再开始，先让界面显示 0/N
    QTimer::singleShot(0, this, [this, key]() { step(key); });
    return result(true);
}

void PluginManager::step(const QString &key) {
    auto it = m_jobs.find(key);
    if (it == m_jobs.end())
        return;
    if (it->index >= it->names.size()) {
        finishJob(key);
        return;
    }
    const QString name = it->names.at(it->index);
    if (it->op == QLatin1String("uninstall")) {
        uninstallOne(key, name);
        return;
    }
    QString error;
    const bool ok = applyEnabled(it->dir, name, it->op == QLatin1String("enable"), &error);
    advance(key, name, ok, error);
}

void PluginManager::advance(const QString &key, const QString &name, bool ok, const QString &reason) {
    auto it = m_jobs.find(key);
    if (it == m_jobs.end())
        return;
    it->summary.record(name, ok, reason);
    it->index += 1;
    emit busyChanged();
    QTimer::singleShot(0, this, [this, key]() { step(key); });
}

// 单项卸载：快照 → 从 bundles 移除 → dsh plugin --profile <p> remove <name>；
// 任一步失败都把 package.json 恢复为快照，保持该插件的启用与安装状态不变
void PluginManager::uninstallOne(const QString &key, const QString &name) {
    const Job job = m_jobs.value(key);
    QByteArray snapshot;
    QString error;
    if (!readBytes(job.dir, &snapshot, &error)) {
        advance(key, name, false, error);
        return;
    }
    if (!applyEnabled(job.dir, name, false, &error)) {
        advance(key, name, false, error);
        return;
    }

    ProcessOptions options;
    const QString resolved = m_ops.resolveHome ? m_ops.resolveHome(job.home) : job.home;
    if (!resolved.isEmpty())
        options.env.insert(QStringLiteral("DSH_HOME"), resolved);
    options.cwd = job.dir;
    const QString exe = m_ops.dshExecutable ? m_ops.dshExecutable() : QStringLiteral("dsh");
    ProcessTask *task = m_runner->run(exe, {
        QStringLiteral("plugin"),
        QStringLiteral("--profile"), job.profile,
        QStringLiteral("remove"), name
    }, options);

    const QString dir = job.dir;
    connect(task, &ProcessTask::finished, this,
            [this, task, key, name, dir, snapshot](bool ok, int code, const QString &tail, bool timedOut) {
        if (ok) {
            advance(key, name, true, {});
            return;
        }
        restoreSnapshot(dir, snapshot);
        QString reason;
        if (task->failedToStart())
            reason = QStringLiteral("dsh 无法启动：") + task->errorString();
        else if (timedOut)
            reason = QStringLiteral("超时");
        else {
            reason = lastLine(tail);
            if (reason.isEmpty())
                reason = QStringLiteral("退出码 %1").arg(code);
        }
        advance(key, name, false, reason);
    });
}

void PluginManager::finishJob(const QString &key) {
    const Job job = m_jobs.take(key);
    emit busyChanged();

    const PluginSet::BatchSummary &s = job.summary;
    QVariantList failures;
    for (const PluginSet::Failure &f : s.failures) {
        QVariantMap item;
        item.insert(QStringLiteral("name"), f.name);
        item.insert(QStringLiteral("reason"), f.reason);
        failures.append(item);
    }
    QVariantMap summary;
    summary.insert(QStringLiteral("op"), job.op);
    summary.insert(QStringLiteral("total"), s.total);
    summary.insert(QStringLiteral("succeeded"), s.succeeded);
    summary.insert(QStringLiteral("failed"), s.failed());
    summary.insert(QStringLiteral("succeededNames"), s.succeededNames);
    summary.insert(QStringLiteral("failures"), failures);

    if (s.succeeded > 0)
        emit pluginsChanged(job.home, job.profile);
    const QStringList ids = affected(job.home, job.profile);
    if (s.needsRestartHint(ids))
        emit restartHintRequested(job.home, job.profile, ids);
    emit batchFinished(job.home, job.profile, summary);
}

// ---------------------------------------------------------------------------
// 插件市场：目录拉取
// ---------------------------------------------------------------------------

QVariantList PluginManager::catalog() const {
    return entriesToList(m_catalog);
}

QVariantList PluginManager::filterCatalog(const QString &keyword) const {
    return entriesToList(CatalogCodec::filter(m_catalog, keyword.trimmed()));
}

QString PluginManager::cachePath() const {
    const QString dir = m_ops.dataDir ? m_ops.dataDir() : QString();
    if (dir.isEmpty())
        return {};
    return QDir(dir).filePath(QStringLiteral("catalog-cache.json"));
}

void PluginManager::fallBackToCache() {
    if (!m_catalog.isEmpty()) {
        m_catalogFromCache = true;
        return;
    }
    m_catalogFromCache = false;
    const QString path = cachePath();
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const CatalogCodec::ParseResult parsed = CatalogCodec::parse(file.readAll());
    if (!parsed.ok)
        return;
    m_catalog = parsed.entries;
    m_catalogFromCache = true;
}

void PluginManager::fetchCatalog() {
    if (m_catalogReply)
        return;
    const QString url = m_ops.catalogUrl ? m_ops.catalogUrl().trimmed() : QString();
    if (const auto invalid = PackageSpec::validateCatalogUrl(url)) {
        m_catalogError = QStringLiteral("目录地址无效：") + *invalid;
        fallBackToCache();
        emit catalogChanged();
        emit catalogFetched(false, m_catalogError);
        return;
    }
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    if (!m_catalogTimer) {
        m_catalogTimer = new QTimer(this);
        m_catalogTimer->setSingleShot(true);
        connect(m_catalogTimer, &QTimer::timeout, this, [this]() {
            if (!m_catalogReply)
                return;
            m_catalogTimedOut = true;
            m_catalogReply->abort();
        });
    }

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    m_catalogTimedOut = false;
    m_catalogReply = m_nam->get(request);
    connect(m_catalogReply, &QNetworkReply::finished, this, &PluginManager::onCatalogReply);
    m_catalogTimer->start(kCatalogTimeoutMs);
    emit catalogChanged();
}

void PluginManager::onCatalogReply() {
    QNetworkReply *reply = m_catalogReply;
    if (!reply)
        return;
    m_catalogReply = nullptr;
    m_catalogTimer->stop();
    reply->deleteLater();

    QString error;
    if (m_catalogTimedOut) {
        error = QStringLiteral("拉取目录超时（15 秒）");
    } else if (reply->error() != QNetworkReply::NoError) {
        error = QStringLiteral("拉取目录失败：") + reply->errorString();
    } else {
        const CatalogCodec::ParseResult parsed = CatalogCodec::parse(reply->readAll());
        if (parsed.ok) {
            m_catalog = parsed.entries;
            m_catalogError.clear();
            m_catalogFromCache = false;
            const QString path = cachePath();
            if (!path.isEmpty()) {
                QDir().mkpath(QFileInfo(path).absolutePath());
                QSaveFile file(path);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(CatalogCodec::print(m_catalog));
                    file.commit();
                }
            }
            emit catalogChanged();
            emit catalogFetched(true, {});
            return;
        }
        error = QStringLiteral("目录格式错误：") + parsed.error;
    }
    m_catalogError = error;
    fallBackToCache();
    emit catalogChanged();
    emit catalogFetched(false, error);
}

// ---------------------------------------------------------------------------
// 插件市场：安装
// ---------------------------------------------------------------------------

QString PluginManager::validateInstallInput(const QString &input) const {
    const auto error = PackageSpec::validateInstallInput(input.trimmed());
    return error ? *error : QString();
}

QString PluginManager::distTag(const QString &channel) const {
    if (channel == QLatin1String("stable"))
        return QStringLiteral("latest");
    if (channel == QLatin1String("beta"))
        return QStringLiteral("beta");
    if (channel == QLatin1String("alpha"))
        return QStringLiteral("alpha");
    return {};
}

QVariantMap PluginManager::install(const QString &home, const QString &profile, const QString &spec,
                                   const QString &channel) {
    const QString input = spec.trimmed();
    // 格式不对时不调用包管理器
    if (const auto invalid = PackageSpec::validateInstallInput(input))
        return result(false, QStringLiteral("格式错误：") + *invalid);
    const PackageSpec::Parsed parsed = PackageSpec::parse(input);
    if (parsed.kind == PackageSpec::Kind::Invalid)
        return result(false, QStringLiteral("格式错误：") + parsed.error);
    QString tag;
    if (!channel.isEmpty()) {
        tag = distTag(channel);
        if (tag.isEmpty())
            return result(false, QStringLiteral("未知的渠道：%1").arg(channel));
    }
    if (profile.isEmpty())
        return result(false, QStringLiteral("未选择目标 profile"));
    const QString key = keyOf(home, profile);
    if (m_jobs.contains(key) || (m_ops.externalBusy && m_ops.externalBusy()))
        return result(false, QStringLiteral("该 profile 有操作正在进行"));

    const QString dir = profileDir(home, profile);
    QByteArray bytes;
    QJsonObject root;
    QString error;
    if (!readBytes(dir, &bytes, &error) || !parse(dir, bytes, &root, &error))
        return result(false, error);
    // dsh plugin add 转发给 pnpm
    if (QStandardPaths::findExecutable(QStringLiteral("pnpm")).isEmpty())
        return result(false, QStringLiteral("需要安装 pnpm"));

    // 没有显式 @版本 / @tag 的 npm 规格才按渠道追加 dist-tag，并记录渠道
    const bool useChannel = parsed.kind == PackageSpec::Kind::Npm && parsed.suffix.isEmpty() && !tag.isEmpty();
    Job job;
    job.home = home;
    job.profile = profile;
    job.dir = dir;
    job.op = QStringLiteral("install");
    job.names = {useChannel ? parsed.name + QLatin1Char('@') + tag : input};
    job.channel = useChannel ? channel : QString();
    m_jobs.insert(key, job);
    emit busyChanged();
    QTimer::singleShot(0, this, [this, key]() { installOne(key); });
    return result(true);
}

void PluginManager::installOne(const QString &key) {
    if (!m_jobs.contains(key))
        return;
    const Job job = m_jobs.value(key);
    QByteArray snapshot;
    QJsonObject before;
    QString error;
    if (!readBytes(job.dir, &snapshot, &error) || !parse(job.dir, snapshot, &before, &error)) {
        finishInstall(key, false, {}, error, {});
        return;
    }
    const QStringList depsBefore = dependencyNames(before);

    ProcessOptions options;
    const QString resolved = m_ops.resolveHome ? m_ops.resolveHome(job.home) : job.home;
    if (!resolved.isEmpty())
        options.env.insert(QStringLiteral("DSH_HOME"), resolved);
    options.cwd = job.dir;
    options.timeoutMs = kInstallTimeoutMs;
    options.tailLines = 20;
    const QString exe = m_ops.dshExecutable ? m_ops.dshExecutable() : QStringLiteral("dsh");
    const QString spec = job.names.value(0);
    ProcessTask *task = m_runner->run(exe, {
        QStringLiteral("plugin"),
        QStringLiteral("--profile"), job.profile,
        QStringLiteral("add"), spec
    }, options);

    connect(task, &ProcessTask::finished, this,
            [this, task, key, snapshot, depsBefore, spec](bool ok, int code, const QString &tail, bool timedOut) {
        if (!m_jobs.contains(key))
            return;
        const Job job = m_jobs.value(key);
        if (!ok) {
            restoreSnapshot(job.dir, snapshot);
            QString reason;
            if (task->failedToStart())
                reason = QStringLiteral("dsh 无法启动：") + task->errorString();
            else if (timedOut)
                reason = QStringLiteral("安装超时");
            else
                reason = QStringLiteral("安装失败（退出码 %1）").arg(code);
            finishInstall(key, false, {}, reason, tail);
            return;
        }

        // 确定要写入 bundles 的包名：npm 取规格中的包名；git 取安装后新增的依赖
        QString name;
        const PackageSpec::Parsed parsed = PackageSpec::parse(spec);
        if (parsed.kind == PackageSpec::Kind::Npm) {
            name = parsed.name;
        } else {
            QByteArray bytes;
            QJsonObject after;
            QString ignored;
            if (readBytes(job.dir, &bytes, &ignored) && parse(job.dir, bytes, &after, &ignored)) {
                for (const QString &dep : dependencyNames(after)) {
                    if (!depsBefore.contains(dep)) {
                        name = dep;
                        break;
                    }
                }
            }
        }
        if (name.isEmpty()) {
            restoreSnapshot(job.dir, snapshot);
            finishInstall(key, false, {}, QStringLiteral("安装命令已完成，但无法确定包名（package.json 中没有新增依赖）"),
                          tail);
            return;
        }
        QString error;
        if (!applyEnabled(job.dir, name, true, &error)) {
            restoreSnapshot(job.dir, snapshot);
            finishInstall(key, false, name, QStringLiteral("加入启用配置失败：") + error, tail);
            return;
        }
        if (!job.channel.isEmpty() && m_ops.recordChannel)
            m_ops.recordChannel(key + QLatin1Char('|') + name, job.channel);
        finishInstall(key, true, name, QStringLiteral("已安装「%1」并启用").arg(name), {});
    });
}

void PluginManager::finishInstall(const QString &key, bool ok, const QString &name, const QString &message,
                                  const QString &tail) {
    const Job job = m_jobs.take(key);
    emit busyChanged();
    if (ok) {
        emit pluginsChanged(job.home, job.profile);
        PluginSet::BatchSummary summary;
        summary.record(name, true);
        const QStringList ids = affected(job.home, job.profile);
        if (summary.needsRestartHint(ids))
            emit restartHintRequested(job.home, job.profile, ids);
    }
    emit installFinished(job.home, job.profile, job.names.value(0), ok, name, message, tail);
}
