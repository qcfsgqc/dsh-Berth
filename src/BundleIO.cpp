#include "BundleIO.h"

#include "ProcessRunner.h"
#include "ProfileOps.h"
#include "core/PluginSet.h"
#include "core/PortPick.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>

using BundleCodec::Bundle;
using BundleCodec::BundleFile;
using BundleCodec::BundlePlugin;

namespace {
constexpr int kDepsTimeoutMs = 10 * 60 * 1000;

// 在线程池执行 work；done 排队回主线程执行，且仅在 self 仍存在时执行
template <typename Result>
void runAsync(BundleIO *owner, std::function<Result()> work, std::function<void(const Result &)> done) {
    QPointer<BundleIO> self(owner);
    QThreadPool::globalInstance()->start([self, work = std::move(work), done = std::move(done)]() {
        Result result = work();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, done, result]() {
                if (self)
                    done(result);
            },
            Qt::QueuedConnection);
    });
}

struct OpResult {
    bool ok = false;
    QString message;
};

bool saveBytes(const QString &path, const QByteArray &data, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("无法写入 %1：%2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    if (file.write(data) != data.size()) {
        *error = QStringLiteral("写入 %1 失败：%2").arg(QDir::toNativeSeparators(path), file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *error = QStringLiteral("保存 %1 失败：%2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

// 递归收集 profile 目录下的文件（相对路径以 "/" 分隔）：跳过 BundleCodec::excluded 命中的路径
// （node_modules 目录整棵不进入）、符号链接与 junction，以及 skipAbs（泊位日志）
bool collectFiles(const QString &root, const QString &rel, const QString &skipAbs, QList<BundleFile> *out,
                  QString *error) {
    const QDir dir(rel.isEmpty() ? root : root + QLatin1Char('/') + rel);
    const QFileInfoList entries =
        dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
    for (const QFileInfo &fi : entries) {
        const QString childRel = rel.isEmpty() ? fi.fileName() : rel + QLatin1Char('/') + fi.fileName();
        if (BundleCodec::excluded(childRel))
            continue;
        if (fi.isSymbolicLink() || fi.isJunction() || fi.isShortcut())
            continue;
        if (fi.isDir()) {
            if (!collectFiles(root, childRel, skipAbs, out, error))
                return false;
            continue;
        }
        const QString abs = QDir::cleanPath(fi.absoluteFilePath());
        if (!skipAbs.isEmpty() && abs.compare(skipAbs, Qt::CaseInsensitive) == 0)
            continue;
        QFile file(abs);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("读取 %1 失败：%2").arg(QDir::toNativeSeparators(abs), file.errorString());
            return false;
        }
        BundleFile entry;
        entry.path = childRel;
        entry.data = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            *error = QStringLiteral("读取 %1 失败：%2").arg(QDir::toNativeSeparators(abs), file.errorString());
            return false;
        }
        out->append(entry);
    }
    return true;
}

// 从 package.json 生成插件清单（含 Core_Package）；没有 package.json 时为空清单
bool pluginsFrom(const QList<BundleFile> &files, QList<BundlePlugin> *out, QString *error) {
    for (const BundleFile &f : files) {
        if (f.path != QLatin1String("package.json"))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.data);
        if (!doc.isObject()) {
            *error = QStringLiteral("package.json 无法解析");
            return false;
        }
        for (const PluginSet::Row &row : PluginSet::list(doc.object(), true)) {
            BundlePlugin p;
            p.name = row.name;
            p.version = row.version;
            p.enabled = row.enabled;
            out->append(p);
        }
        return true;
    }
    return true;
}

// 按插件清单补齐 dependencies 并恢复启用状态；与现状一致时不改写 package.json（保持字节不变）
bool applyPlugins(const QString &dir, const QList<BundlePlugin> &plugins, QString *error) {
    if (plugins.isEmpty())
        return true;
    const QString path = dir + QStringLiteral("/package.json");
    QJsonObject pkg;
    const bool exists = QFileInfo::exists(path);
    if (exists) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("读取 package.json 失败：%1").arg(file.errorString());
            return false;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) {
            *error = QStringLiteral("package.json 无法解析");
            return false;
        }
        pkg = doc.object();
    }
    const QJsonObject before = pkg;
    QJsonObject deps = pkg.value(QStringLiteral("dependencies")).toObject();
    bool depsChanged = false;
    for (const BundlePlugin &p : plugins) {
        if (!p.version.isEmpty() && !deps.contains(p.name)) {
            deps.insert(p.name, p.version);
            depsChanged = true;
        }
    }
    if (depsChanged)
        pkg.insert(QStringLiteral("dependencies"), deps);
    for (const BundlePlugin &p : plugins)
        pkg = PluginSet::setEnabled(pkg, p.name, p.enabled);
    if (exists && pkg == before)
        return true;
    return saveBytes(path, QJsonDocument(pkg).toJson(QJsonDocument::Indented), error);
}

BundleCodec::DecodeResult readBundle(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        BundleCodec::DecodeResult r;
        r.error = QStringLiteral("无法读取 Bundle 文件：%1").arg(file.errorString());
        return r;
    }
    return BundleCodec::decode(file.readAll());
}

QString bundleProfileName(const Bundle &bundle) {
    if (!bundle.manifest.profileName.isEmpty())
        return bundle.manifest.profileName;
    return bundle.instance ? bundle.instance->profile : QString();
}
} // namespace

BundleIO::BundleIO(ProcessRunner *runner, QObject *parent) : QObject(parent), m_runner(runner) {}

QString BundleIO::localPath(const QString &path) {
    return path.startsWith(QLatin1String("file:"), Qt::CaseInsensitive) ? QUrl(path).toLocalFile() : path;
}

QString BundleIO::profileDir(const QString &home, const QString &profile) const {
    return m_ops.resolveHome(home) + QStringLiteral("/profiles/") + profile;
}

QString BundleIO::depsKey(const QString &home, const QString &profile) const {
    return m_ops.resolveHome(home) + QLatin1Char('|') + profile;
}

void BundleIO::setBusy(bool on) {
    if (m_busy == on)
        return;
    m_busy = on;
    emit busyChanged();
}

bool BundleIO::hasSecrets(const QString &id) const {
    for (const Instance &item : m_ops.instances()) {
        if (item.id != id)
            continue;
        for (const InstanceEnvVar &var : item.env) {
            if (var.secret)
                return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 导出
// ---------------------------------------------------------------------------

QVariantMap BundleIO::exportInstance(const QString &id, const QString &path, bool includeSecrets) {
    auto reject = [](const QString &error) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    };
    if (m_busy)
        return reject(QStringLiteral("已有导出或导入在进行"));
    Instance item;
    for (const Instance &it : m_ops.instances()) {
        if (it.id == id)
            item = it;
    }
    if (item.id.isEmpty())
        return reject(QStringLiteral("找不到泊位"));
    const QString dest = localPath(path);
    if (dest.isEmpty())
        return reject(QStringLiteral("保存路径无效"));
    const QString dir = profileDir(item.dshHome, item.profile);
    if (!QFileInfo(dir).isDir())
        return reject(QStringLiteral("profile 目录不存在：%1").arg(QDir::toNativeSeparators(dir)));

    Bundle bundle;
    bundle.manifest.type = BundleCodec::kTypeInstance;
    bundle.manifest.profileName = item.profile;
    BundleCodec::BundleInstance inst;
    inst.port = item.port;
    inst.profile = item.profile;
    inst.workspace = item.workspace;
    inst.dshVersion = item.dshVersion;
    inst.extraArgs = item.extraArgs;
    for (const InstanceEnvVar &var : item.env) {
        BundleCodec::BundleEnvVar env;
        env.key = var.key;
        env.value = var.value;
        env.secret = var.secret;
        inst.env.append(env);
    }
    bundle.instance = inst;
    // 泊位日志默认在 Berth 数据目录，不在 profile 目录；若被配置到 profile 目录内也要排除
    const QString skipAbs =
        item.logPath.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(item.logPath).absoluteFilePath());
    startExport(dir, dest, bundle, skipAbs, includeSecrets);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()}};
}

QVariantMap BundleIO::exportProfile(const QString &home, const QString &profile, const QString &path) {
    auto reject = [](const QString &error) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    };
    if (m_busy)
        return reject(QStringLiteral("已有导出或导入在进行"));
    const QString dest = localPath(path);
    if (dest.isEmpty())
        return reject(QStringLiteral("保存路径无效"));
    const QString dir = profileDir(home, profile);
    if (!QFileInfo(dir).isDir())
        return reject(QStringLiteral("profile 目录不存在：%1").arg(QDir::toNativeSeparators(dir)));
    Bundle bundle;
    bundle.manifest.type = BundleCodec::kTypeProfile;
    bundle.manifest.profileName = profile;
    // profile 类型：同一 profile 下所有泊位的日志都排除（仅当日志落在 profile 目录内时才有意义）
    startExport(dir, dest, bundle, QString(), true);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()}};
}

void BundleIO::startExport(const QString &dir, const QString &dest, Bundle base, const QString &skipAbs,
                           bool includeSecrets) {
    base.manifest.formatVersion = BundleCodec::kFormatVersion;
    base.manifest.exportedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    base.manifest.berthVersion = m_ops.berthVersion ? m_ops.berthVersion() : QString();
    // 所有泊位日志的绝对路径（profile 导出时逐个排除）
    QSet<QString> logs;
    if (!skipAbs.isEmpty())
        logs.insert(skipAbs.toLower());
    if (base.manifest.type == BundleCodec::kTypeProfile) {
        for (const Instance &item : m_ops.instances()) {
            if (!item.logPath.isEmpty())
                logs.insert(QDir::cleanPath(QFileInfo(item.logPath).absoluteFilePath()).toLower());
        }
    }
    setBusy(true);
    runAsync<OpResult>(
        this,
        [dir, dest, base, logs, includeSecrets]() {
            OpResult r;
            Bundle bundle = base;
            QString error;
            if (!collectFiles(dir, QString(), QString(), &bundle.files, &error)) {
                r.message = error;
                return r;
            }
            if (!logs.isEmpty()) {
                const QString root = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
                bundle.files.removeIf([&](const BundleFile &f) {
                    return logs.contains((root + QLatin1Char('/') + f.path).toLower());
                });
            }
            if (!pluginsFrom(bundle.files, &bundle.plugins, &error)) {
                r.message = error;
                return r;
            }
            if (!saveBytes(dest, BundleCodec::encode(bundle, includeSecrets), &error)) {
                r.message = error;
                return r;
            }
            r.ok = true;
            r.message = QDir::toNativeSeparators(dest);
            return r;
        },
        [this](const OpResult &r) {
            setBusy(false);
            emit exportFinished(r.ok, r.ok ? r.message : QStringLiteral("导出失败：") + r.message);
        });
}

// ---------------------------------------------------------------------------
// 导入
// ---------------------------------------------------------------------------

QString BundleIO::checkProfileName(const QString &home, const QString &name) const {
    const QString invalid = ProfileOps::validateProfileName(name);
    if (!invalid.isEmpty())
        return invalid;
    const QString dir = profileDir(home, name);
    if (QFileInfo::exists(dir))
        return QStringLiteral("已存在同名 profile：%1").arg(name);
    return {};
}

int BundleIO::suggestPort(int from) const {
    QSet<int> configured;
    for (const Instance &item : m_ops.instances())
        configured.insert(item.port);
    const auto listening = [this](int port) { return m_ops.isListening ? m_ops.isListening(port) : false; };
    return PortPick::next(from, listening, configured).value_or(0);
}

QVariantMap BundleIO::conflictInfo(const Bundle &bundle, const QString &home) const {
    const QString name = bundleProfileName(bundle);
    const QString nameError = ProfileOps::validateProfileName(name);
    const bool profileConflict = nameError.isEmpty() && QFileInfo::exists(profileDir(home, name));
    QString suggested = name;
    if (!nameError.isEmpty() || profileConflict) {
        const QString base = nameError.isEmpty() ? name : QStringLiteral("imported");
        suggested.clear();
        for (int n = nameError.isEmpty() ? 2 : 1; n < 1000; ++n) {
            const QString candidate = n == 1 ? base : QStringLiteral("%1-%2").arg(base).arg(n);
            if (checkProfileName(home, candidate).isEmpty()) {
                suggested = candidate;
                break;
            }
        }
    }
    int port = 0;
    QStringList portUsers;
    int suggestedPort = 0;
    if (bundle.instance) {
        port = bundle.instance->port;
        portUsers = PortPick::duplicates(m_ops.instances(), QString(), port);
        suggestedPort = portUsers.isEmpty() ? port : suggestPort(port);
    }
    QVariantMap out;
    out.insert(QStringLiteral("type"), bundle.manifest.type);
    out.insert(QStringLiteral("profileName"), name);
    out.insert(QStringLiteral("profileNameError"), nameError);
    out.insert(QStringLiteral("profileConflict"), profileConflict);
    out.insert(QStringLiteral("suggestedName"), suggested);
    out.insert(QStringLiteral("port"), port);
    out.insert(QStringLiteral("portConflict"), !portUsers.isEmpty());
    out.insert(QStringLiteral("portUsers"), portUsers);
    out.insert(QStringLiteral("suggestedPort"), suggestedPort);
    out.insert(QStringLiteral("missingSecrets"), BundleCodec::missingSecrets(bundle));
    out.insert(QStringLiteral("pluginCount"), int(bundle.plugins.size()));
    out.insert(QStringLiteral("fileCount"), int(bundle.files.size()));
    return out;
}

QVariantMap BundleIO::inspect(const QString &path, const QString &home) {
    const QString src = localPath(path);
    if (src.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("文件路径无效")}};
    runAsync<BundleCodec::DecodeResult>(
        this, [src]() { return readBundle(src); },
        [this, src, home](const BundleCodec::DecodeResult &decoded) {
            QVariantMap result;
            if (decoded.ok())
                result = conflictInfo(*decoded.value, home);
            result.insert(QStringLiteral("ok"), decoded.ok());
            result.insert(QStringLiteral("error"), decoded.ok() ? QString() : decoded.error);
            result.insert(QStringLiteral("path"), src);
            result.insert(QStringLiteral("home"), m_ops.resolveHome(home));
            emit inspected(result);
        });
    return {{QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()}};
}

QVariantMap BundleIO::importBundle(const QString &path, const QString &home, const QVariantMap &resolution) {
    if (m_busy)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("已有导出或导入在进行")}};
    const QString src = localPath(path);
    if (src.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("文件路径无效")}};
    setBusy(true);
    runAsync<BundleCodec::DecodeResult>(
        this, [src]() { return readBundle(src); },
        [this, home, resolution](const BundleCodec::DecodeResult &decoded) {
            auto fail = [this](const QString &message) {
                setBusy(false);
                emit importFinished(false, message, {{QStringLiteral("ok"), false},
                                                     {QStringLiteral("error"), message}});
            };
            if (!decoded.ok()) {
                fail(QStringLiteral("无法导入：") + decoded.error);
                return;
            }
            const Bundle bundle = *decoded.value;
            // 冲突复核（写入任何数据之前）
            const QString requested = resolution.value(QStringLiteral("profileName")).toString().trimmed();
            const QString profile = requested.isEmpty() ? bundleProfileName(bundle) : requested;
            const QString nameError = checkProfileName(home, profile);
            if (!nameError.isEmpty()) {
                fail(QStringLiteral("profile 冲突：") + nameError);
                return;
            }
            int port = 0;
            if (bundle.instance) {
                port = resolution.contains(QStringLiteral("port"))
                           ? resolution.value(QStringLiteral("port")).toInt()
                           : bundle.instance->port;
                if (resolution.value(QStringLiteral("autoPort")).toBool()) {
                    port = suggestPort(bundle.instance->port);
                    if (port == 0) {
                        fail(QStringLiteral("找不到可用端口"));
                        return;
                    }
                }
                if (port < 1 || port > 65535) {
                    fail(QStringLiteral("端口不合法：%1").arg(port));
                    return;
                }
                const QStringList users = PortPick::duplicates(m_ops.instances(), QString(), port);
                if (!users.isEmpty()) {
                    fail(QStringLiteral("端口冲突：%1 已被泊位 %2 使用")
                             .arg(port)
                             .arg(users.join(QStringLiteral("、"))));
                    return;
                }
            }
            // 写入 profile 文件（工作线程）；失败时删除本次新建的目录
            const QString dir = profileDir(home, profile);
            runAsync<OpResult>(
                this,
                [dir, bundle]() {
                    OpResult r;
                    if (QFileInfo::exists(dir)) {
                        r.message = QStringLiteral("目标目录已存在：%1").arg(QDir::toNativeSeparators(dir));
                        return r;
                    }
                    if (!QDir().mkpath(dir)) {
                        r.message = QStringLiteral("无法创建目录：%1").arg(QDir::toNativeSeparators(dir));
                        return r;
                    }
                    QString error;
                    bool ok = true;
                    for (const BundleFile &f : bundle.files) {
                        const QString target = dir + QLatin1Char('/') + f.path;
                        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
                            error = QStringLiteral("无法创建目录：%1")
                                        .arg(QDir::toNativeSeparators(QFileInfo(target).absolutePath()));
                            ok = false;
                            break;
                        }
                        if (!saveBytes(target, f.data, &error)) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok)
                        ok = applyPlugins(dir, bundle.plugins, &error);
                    if (!ok) {
                        QString ignored;
                        ProfileOps::removeProfileTree(dir, &ignored);
                        r.message = error;
                        return r;
                    }
                    r.ok = true;
                    return r;
                },
                [this, bundle, home, profile, port, fail](const OpResult &r) {
                    if (!r.ok) {
                        fail(QStringLiteral("导入失败：") + r.message);
                        return;
                    }
                    finishImportWrite(bundle, home, profile, port);
                });
        });
    return {{QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()}};
}

void BundleIO::finishImportWrite(const Bundle &bundle, const QString &home, const QString &profile, int port) {
    const QString resolvedHome = m_ops.resolveHome(home);
    QString instanceId;
    if (bundle.instance) {
        Instance item;
        item.name = QStringLiteral("%1 · 导入").arg(profile);
        item.port = port;
        item.profile = profile;
        // 默认 home 的泊位 dshHome 留空（与 createInstanceFor 一致）
        item.dshHome = resolvedHome == m_ops.resolveHome(QString()) ? QString() : resolvedHome;
        item.workspace = bundle.instance->workspace;
        item.dshVersion = bundle.instance->dshVersion;
        item.extraArgs = bundle.instance->extraArgs;
        for (const BundleCodec::BundleEnvVar &var : bundle.instance->env) {
            InstanceEnvVar env;
            env.key = var.key;
            env.value = var.valueOmitted ? QString() : var.value;
            env.secret = var.secret;
            item.env.append(env);
        }
        instanceId = m_ops.addInstance ? m_ops.addInstance(item) : QString();
        if (instanceId.isEmpty()) {
            // 泊位没能写入：撤销本次新建的 profile 目录，保持\"未创建任何数据\"
            QString ignored;
            ProfileOps::removeProfileTree(profileDir(home, profile), &ignored);
            setBusy(false);
            const QString message = QStringLiteral("导入失败：无法写入泊位配置");
            emit importFinished(false, message, {{QStringLiteral("ok"), false},
                                                 {QStringLiteral("error"), message}});
            return;
        }
    }
    if (m_ops.profilesChanged)
        m_ops.profilesChanged();

    DepsJob job{depsKey(home, profile), resolvedHome, profile, instanceId};
    const QStringList missing = BundleCodec::missingSecrets(bundle);
    const QString type = bundle.manifest.type;
    runDeps(job, [this, job, missing, type](bool ok, const QString &message, const QString &tail) {
        if (!ok) {
            m_depsJobs.insert(job.key, job);
            m_depsPending.insert(job.key, QVariantMap{{QStringLiteral("key"), job.key},
                                                      {QStringLiteral("home"), job.home},
                                                      {QStringLiteral("profile"), job.profile},
                                                      {QStringLiteral("instanceId"), job.instanceId},
                                                      {QStringLiteral("error"), message},
                                                      {QStringLiteral("tail"), tail}});
            emit depsPendingChanged();
        }
        QVariantMap result{{QStringLiteral("ok"), true},
                           {QStringLiteral("error"), QString()},
                           {QStringLiteral("type"), type},
                           {QStringLiteral("home"), job.home},
                           {QStringLiteral("profile"), job.profile},
                           {QStringLiteral("instanceId"), job.instanceId},
                           {QStringLiteral("missingSecrets"), missing},
                           {QStringLiteral("depsReady"), ok},
                           {QStringLiteral("depsKey"), job.key},
                           {QStringLiteral("depsError"), ok ? QString() : message},
                           {QStringLiteral("tail"), tail}};
        QString text = ok ? QStringLiteral("导入完成：%1").arg(job.profile)
                          : QStringLiteral("已导入 %1，依赖未就绪：%2").arg(job.profile, message);
        if (!missing.isEmpty())
            text += QStringLiteral("\n需要补填的敏感键：") + missing.join(QStringLiteral("、"));
        setBusy(false);
        emit importFinished(true, text, result);
    });
}

void BundleIO::runDeps(const DepsJob &job, std::function<void(bool, const QString &, const QString &)> done) {
    if (QStandardPaths::findExecutable(QStringLiteral("pnpm")).isEmpty()) {
        done(false, QStringLiteral("未找到 pnpm，依赖未重建"), QString());
        return;
    }
    ProcessOptions options;
    options.env.insert(QStringLiteral("DSH_HOME"), job.home);
    options.cwd = job.home + QStringLiteral("/profiles/") + job.profile;
    options.timeoutMs = kDepsTimeoutMs;
    options.tailLines = 20;
    ProcessTask *task = m_runner->run(m_ops.dshExecutable(),
                                      {QStringLiteral("plugin"), QStringLiteral("--profile"), job.profile,
                                       QStringLiteral("install")},
                                      options);
    connect(task, &ProcessTask::finished, this,
            [task, done = std::move(done)](bool ok, int code, const QString &tail, bool timedOut) {
                if (ok) {
                    done(true, QString(), tail);
                    return;
                }
                QString message;
                if (task->failedToStart())
                    message = QStringLiteral("无法启动 dsh：") + task->errorString();
                else if (timedOut)
                    message = QStringLiteral("依赖安装超时");
                else
                    message = QStringLiteral("依赖安装失败（退出码 %1）").arg(code);
                done(false, message, tail);
            });
}

QVariantMap BundleIO::retryDeps(const QString &key) {
    if (m_busy)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("已有导出或导入在进行")}};
    if (!m_depsJobs.contains(key))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("没有待重试的依赖重建")}};
    const DepsJob job = m_depsJobs.value(key);
    setBusy(true);
    runDeps(job, [this, key](bool ok, const QString &message, const QString &tail) {
        if (ok) {
            m_depsJobs.remove(key);
            m_depsPending.remove(key);
        } else {
            QVariantMap entry = m_depsPending.value(key).toMap();
            entry.insert(QStringLiteral("error"), message);
            entry.insert(QStringLiteral("tail"), tail);
            m_depsPending.insert(key, entry);
        }
        emit depsPendingChanged();
        setBusy(false);
        emit depsRetryFinished(key, ok, ok ? QStringLiteral("依赖已就绪") : message);
    });
    return {{QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()}};
}
