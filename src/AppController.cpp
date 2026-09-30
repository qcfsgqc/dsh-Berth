#include "AppController.h"
#include "PluginOps.h"
#include "ProfileOps.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {
// 取输出末尾最多 maxLines 行（去掉空行），作为结果摘要
QString tailLines(const QString &text, int maxLines) {
    QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
    if (lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    return lines.join(QLatin1Char('\n'));
}
}

AppController::AppController(QObject *parent)
    : QObject(parent), m_instances(this), m_settings(this), m_supervisor(this),
      m_profileTree(this), m_dshUpdate(this) {
    connect(&m_supervisor, &Supervisor::statusChanged, this, &AppController::applyStatus);
    m_dshUpdate.setSettings(&m_settings);
    // 泊位列表任何变化都全量重建树（泊位数量级小，整树重建开销可忽略）
    connect(&m_instances, &InstanceModel::dataChanged, this, [this]() { rebuildTree(); });
    connect(&m_instances, &InstanceModel::rowsInserted, this, [this]() { rebuildTree(); });
    connect(&m_instances, &InstanceModel::rowsRemoved, this, [this]() { rebuildTree(); });
    connect(&m_instances, &InstanceModel::modelReset, this, [this]() { rebuildTree(); });
    load();
    refreshProfiles();
    if (m_instances.rowCount() == 0 && !m_knownProfiles.isEmpty())
        importDetectedProfiles();
    rebuildTree();
}

InstanceModel *AppController::instances() { return &m_instances; }
Settings *AppController::settings() { return &m_settings; }
QString AppController::version() const { return QStringLiteral(BERTH_VERSION); }

QString AppController::createInstance() {
    Instance item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    int port = 3080;
    while (m_instances.containsPort(port))
        port += 1;
    item.port = port;
    item.name = QStringLiteral("泊位 %1").arg(m_instances.rowCount() + 1);
    item.profile = QStringLiteral("web");
    item.status = QStringLiteral("stopped");
    item.logPath = m_settings.dataDir() + QStringLiteral("/logs/") + item.id + QStringLiteral(".log");
    m_instances.upsert(item);
    save();
    return item.id;
}

QString AppController::createInstanceFrom(const QString &sourceId) {
    const Instance src = m_instances.item(sourceId);
    Instance item;
    if (!src.id.isEmpty()) {
        item.name = src.name + QStringLiteral(" - 副本");
        item.port = 3080;
        while (m_instances.containsPort(item.port))
            item.port += 1;
        item.profile = src.profile;
        item.dshHome = src.dshHome;
        item.workspace = src.workspace;
        item.autostart = src.autostart;
    } else {
        item.name = QStringLiteral("泊位 %1").arg(m_instances.rowCount() + 1);
        item.port = 3080;
        while (m_instances.containsPort(item.port))
            item.port += 1;
        item.profile = QStringLiteral("web");
    }
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.status = QStringLiteral("stopped");
    item.logPath = m_settings.dataDir() + QStringLiteral("/logs/") + item.id + QStringLiteral(".log");
    m_instances.upsert(item);
    save();
    return item.id;
}

void AppController::removeInstance(const QString &id) {
    if (m_supervisor.isRunning(id))
        m_supervisor.stop(id);
    m_instances.remove(id);
    save();
}

void AppController::updateInstance(const QString &id, const QString &name, int port,
                                   const QString &profile, const QString &dshHome,
                                   const QString &workspace, bool autostart) {
    Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return;
    if (port < 1 || port > 65535) {
        emit notice(QStringLiteral("端口必须在 1–65535"));
        return;
    }
    if (m_instances.containsPort(port, id)) {
        emit notice(QStringLiteral("端口 %1 已被其它泊位占用").arg(port));
        return;
    }
    item.name = name.trimmed().isEmpty() ? item.name : name.trimmed();
    item.port = port;
    item.profile = profile.trimmed().isEmpty() ? QStringLiteral("web") : profile.trimmed();
    item.dshHome = dshHome.trimmed();
    item.workspace = workspace.trimmed();
    item.autostart = autostart;
    m_instances.upsert(item);
    save();
}

void AppController::startInstance(const QString &id) {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty() || m_supervisor.isRunning(id))
        return;
    m_supervisor.start(item, m_settings.resolvedDshExecutable());
}

void AppController::stopInstance(const QString &id) {
    m_supervisor.stop(id);
}

void AppController::restartInstance(const QString &id) {
    stopInstance(id);
    startInstance(id);
}

void AppController::openUi(const QString &id) {
    if (m_instances.item(id).id.isEmpty())
        return;
    requestUi(id, 0);
}

void AppController::requestUi(const QString &id, int attempt) {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return;
    // 运行中但 token 行还没刷进日志：最多等 5 秒
    if (tokenUrl(id).isEmpty() && item.status == QLatin1String("running") && attempt < 20) {
        QTimer::singleShot(250, this, [this, id, attempt]() { requestUi(id, attempt + 1); });
        return;
    }
    emit uiRequested(id, uiUrl(id));
}

void AppController::openInBrowser(const QString &id) {
    if (m_instances.item(id).id.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl(uiUrl(id)));
}

QString AppController::uiUrl(const QString &id) const {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return {};
    const QString url = tokenUrl(id);
    return url.isEmpty() ? QStringLiteral("http://127.0.0.1:%1/").arg(item.port) : url;
}

QString AppController::tokenUrl(const QString &id) const {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return {};
    const QString text = readLog(id, 262144);
    // 只看最后一次启动之后的输出，避免拿到上次运行的过期 token
    const qsizetype start = text.lastIndexOf(QStringLiteral("] 启动: "));
    const QString recent = start >= 0 ? text.mid(start) : text;
    // dsh web 启动后打印：dsh web: http://127.0.0.1:<port>/?token=...
    const QRegularExpression re(QStringLiteral("https?://(?:127\\.0\\.0\\.1|localhost):%1/\\S*").arg(item.port));
    QRegularExpressionMatchIterator it = re.globalMatch(recent);
    QString last;
    while (it.hasNext())
        last = it.next().captured(0);
    return last;
}

void AppController::openLog(const QString &id) {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty() || item.logPath.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(item.logPath));
}

QString AppController::readLog(const QString &id, int maxBytes) const {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty() || item.logPath.isEmpty())
        return {};
    QFile file(item.logPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = file.size();
    const bool truncated = maxBytes > 0 && size > maxBytes;
    if (truncated)
        file.seek(size - maxBytes);
    QByteArray data = file.readAll();
    // 从中间截断时丢掉第一行残片，避免半个 UTF-8 字符
    if (truncated) {
        const int nl = data.indexOf('\n');
        if (nl >= 0)
            data.remove(0, nl + 1);
    }
    QString text = QString::fromUtf8(data);
    // 去掉 ANSI 颜色/光标控制序列，终端面板只显示纯文本
    static const QRegularExpression ansi(QStringLiteral("\\x1B\\[[0-9;?]*[ -/]*[@-~]"));
    text.remove(ansi);
    text.remove(QLatin1Char('\r'));
    return text;
}


QVariantMap AppController::instance(const QString &id) const {
    const Instance item = m_instances.item(id);
    QVariantMap map;
    if (item.id.isEmpty())
        return map;
    map.insert(QStringLiteral("id"), item.id);
    map.insert(QStringLiteral("name"), item.name);
    map.insert(QStringLiteral("port"), item.port);
    map.insert(QStringLiteral("profile"), item.profile);
    map.insert(QStringLiteral("dshHome"), item.dshHome);
    map.insert(QStringLiteral("workspace"), item.workspace);
    map.insert(QStringLiteral("autostart"), item.autostart);
    map.insert(QStringLiteral("status"), item.status);
    map.insert(QStringLiteral("pid"), item.pid);
    map.insert(QStringLiteral("lastError"), item.lastError);
    map.insert(QStringLiteral("logPath"), item.logPath);
    return map;
}

QString AppController::defaultDshHome() const {
    return resolveHome({});
}

QString AppController::resolveHome(const QString &dshHome) const {
    const QString given = dshHome.trimmed();
    if (!given.isEmpty())
        return QDir::cleanPath(given);
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("DSH_HOME"));
    if (!env.trimmed().isEmpty())
        return QDir::cleanPath(env.trimmed());
    return QDir::cleanPath(QDir::homePath() + QStringLiteral("/.dsh"));
}

QString AppController::profileDirFor(const QString &id, QString *error) const {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty()) {
        if (error)
            *error = QStringLiteral("找不到泊位");
        return {};
    }
    return resolveHome(item.dshHome) + QStringLiteral("/profiles/") + item.profile;
}

QVariantMap AppController::listPlugins(const QString &id) const {
    QString error;
    const QString dir = profileDirFor(id, &error);
    if (dir.isEmpty()) {
        QVariantMap result;
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), error);
        result.insert(QStringLiteral("plugins"), QVariantList());
        return result;
    }
    return PluginOps::readPlugins(dir);
}

QVariantMap AppController::listPluginsForProfile(const QString &home, const QString &profile) const {
    const QString dir = profileDirForHome(home, profile);
    return PluginOps::readPlugins(dir);
}

bool AppController::profileBusy() const {
    return m_profileBusy;
}

bool AppController::pluginBusy() const {
    return m_pluginBusy;
}

void AppController::setPluginBusy(bool busy) {
    if (m_pluginBusy == busy)
        return;
    m_pluginBusy = busy;
    emit pluginBusyChanged();
}

void AppController::setProfileBusy(bool busy) {
    if (m_profileBusy == busy)
        return;
    m_profileBusy = busy;
    emit profileBusyChanged();
}

void AppController::uninstallPlugin(const QString &id, const QString &name, bool removePackage) {
    QString error;
    const QString dir = profileDirFor(id, &error);
    if (dir.isEmpty()) {
        emit pluginUninstallFinished(id, false, error);
        return;
    }
    const Instance target = m_instances.item(id);
    if (target.id.isEmpty()) {
        emit pluginUninstallFinished(id, false, QStringLiteral("找不到泊位"));
        return;
    }
    uninstallFromProfile(id, target.dshHome, target.profile, name, removePackage);
}

void AppController::uninstallPluginForProfile(const QString &home, const QString &profile,
                                              const QString &name, bool removePackage) {
    // id 为空：按 (home, profile) 直接操作，结果信号照发 pluginUninstallFinished（id 为空串）
    uninstallFromProfile(QString(), home, profile, name, removePackage);
}

QStringList AppController::detectProfiles(const QString &dshHome) const {
    const QString home = resolveHome(dshHome);
    QDir dir(home + QStringLiteral("/profiles"));
    if (!dir.exists())
        return {};
    QStringList names;
    const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        const QString path = info.absoluteFilePath();
        if (QFile::exists(path + QStringLiteral("/package.json"))
            || QFile::exists(path + QStringLiteral("/cordis.patch.yml")))
            names.push_back(info.fileName());
    }
    return names;
}

QStringList AppController::knownProfiles() const {
    return m_knownProfiles;
}

void AppController::refreshProfiles() {
    const QStringList found = detectProfiles({});
    if (found != m_knownProfiles) {
        m_knownProfiles = found;
        emit profilesChanged();
    }
    rebuildTree();
}

int AppController::importDetectedProfiles() {
    refreshProfiles();
    const QString home = defaultDshHome();
    int added = 0;
    for (const QString &name : m_knownProfiles) {
        bool exists = false;
        for (const Instance &item : m_instances.items()) {
            if (item.profile == name && resolveHome(item.dshHome) == home) {
                exists = true;
                break;
            }
        }
        if (exists)
            continue;
        Instance item;
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        int port = 3080;
        while (m_instances.containsPort(port))
            port += 1;
        item.port = port;
        item.name = name;
        item.profile = name;
        item.dshHome = home;
        item.status = QStringLiteral("stopped");
        item.logPath = m_settings.dataDir() + QStringLiteral("/logs/") + item.id + QStringLiteral(".log");
        m_instances.upsert(item);
        ++added;
    }
    if (added > 0)
        save();
    emit notice(added > 0
                    ? QStringLiteral("已识别 %1 个 profile，新增 %2 个泊位").arg(m_knownProfiles.size()).arg(added)
                    : QStringLiteral("已识别 %1 个 profile，没有新泊位").arg(m_knownProfiles.size()));
    return added;
}

QString AppController::profileDirForHome(const QString &home, const QString &profile) const {
    return resolveHome(home) + QStringLiteral("/profiles/") + profile;
}

// 卸载插件共用流程（id 为空表示按 (home, profile) 直接操作）：
// 1. 忙 / 引用泊位在跑就拒绝；2. 有 pnpm 才能拆包；3. 先改 bundles 再跑 dsh plugin remove
void AppController::uninstallFromProfile(const QString &id, const QString &home, const QString &profile,
                                         const QString &name, bool removePackage) {
    // 1. 已有卸载或其它 profile 包操作在进行
    if (m_pluginBusy || m_profileBusy) {
        emit pluginUninstallFinished(id, false, QStringLiteral("有其它 profile 操作在进行"));
        return;
    }

    const QString resolvedHome = resolveHome(home);
    const QString dir = resolvedHome + QStringLiteral("/profiles/") + profile;

    // 2. 同一 DSH_HOME + profile 的泊位必须都已停止（删包/改 bundles 才不会被进程占用）
    QStringList busyNames;
    for (const Instance &item : m_instances.items()) {
        if (item.profile != profile || resolveHome(item.dshHome) != resolvedHome)
            continue;
        if (item.status == QLatin1String("running") || item.status == QLatin1String("starting")
            || item.status == QLatin1String("stopping"))
            busyNames.push_back(item.name);
    }
    if (!busyNames.isEmpty()) {
        emit pluginUninstallFinished(id, false,
                                     QStringLiteral("先停止：") + busyNames.join(QStringLiteral("、")));
        return;
    }

    // 3. 要卸载插件包时先确认本机有 pnpm，没有就不做任何修改
    if (removePackage && QStandardPaths::findExecutable(QStringLiteral("pnpm")).isEmpty()) {
        emit pluginUninstallFinished(id, false, QStringLiteral("需要安装 pnpm"));
        return;
    }

    // 4. 从 dsh.profile.bundles 移除（不在其中时 removeFromBundles 直接成功、不写文件）
    QString error;
    if (!PluginOps::removeFromBundles(dir, name, &error)) {
        emit pluginUninstallFinished(id, false, error);
        return;
    }

    // 5. 只改配置
    if (!removePackage) {
        emit pluginUninstallFinished(id, true, QStringLiteral("已从配置移除"));
        return;
    }

    // 6. 异步执行 dsh plugin --profile <p> remove <name>；失败不回滚 bundles
    setPluginBusy(true);
    auto *process = new QProcess(this);
    m_pluginProcess = process;
    process->setProgram(m_settings.resolvedDshExecutable());
    process->setArguments({
        QStringLiteral("plugin"),
        QStringLiteral("--profile"), profile,
        QStringLiteral("remove"), name
    });
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!resolvedHome.isEmpty())
        env.insert(QStringLiteral("DSH_HOME"), resolvedHome);
    process->setProcessEnvironment(env);
    process->setWorkingDirectory(dir);
    process->setProcessChannelMode(QProcess::MergedChannels);

#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
    });
#endif

    const QString failPrefix = QStringLiteral("bundles 已移除，pnpm 失败");
    connect(process, &QProcess::finished, this,
            [this, process, id, failPrefix](int code, QProcess::ExitStatus status) {
        const QString summary = tailLines(QString::fromLocal8Bit(process->readAll()), 20);
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (m_pluginProcess == process)
            m_pluginProcess = nullptr;
        process->deleteLater();
        setPluginBusy(false);
        QString message = ok ? QStringLiteral("已卸载") : failPrefix;
        if (!summary.isEmpty())
            message += QLatin1Char('\n') + summary;
        emit pluginUninstallFinished(id, ok, message);
    });
    // 启动失败时不会有 finished 信号，这里单独收尾
    connect(process, &QProcess::errorOccurred, this,
            [this, process, id, failPrefix](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart)
            return;
        const QString reason = process->errorString();
        if (m_pluginProcess == process)
            m_pluginProcess = nullptr;
        process->deleteLater();
        setPluginBusy(false);
        emit pluginUninstallFinished(id, false, failPrefix + QLatin1Char('\n') + reason);
    });
    process->start();
}

void AppController::startProfileProcess(const QString &program, const QStringList &args, const QString &dshHome,
                                        const QString &workingDir, const QString &failPrefix,
                                        const QString &finishProfileName) {
    auto *process = new QProcess(this);
    m_profileProcess = process;
    m_profileFinishName = finishProfileName;
    process->setProgram(program);
    process->setArguments(args);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!dshHome.isEmpty())
        env.insert(QStringLiteral("DSH_HOME"), dshHome);
    process->setProcessEnvironment(env);
    if (!workingDir.isEmpty())
        process->setWorkingDirectory(workingDir);
    process->setProcessChannelMode(QProcess::MergedChannels);

#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *procArgs) {
        procArgs->flags |= CREATE_NO_WINDOW;
        procArgs->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        procArgs->startupInfo->wShowWindow = SW_HIDE;
    });
#endif

    connect(process, &QProcess::finished, this,
            [this, process, failPrefix](int code, QProcess::ExitStatus status) {
        const QString summary = tailLines(QString::fromLocal8Bit(process->readAll()), 20);
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (m_profileProcess == process)
            m_profileProcess = nullptr;
        process->deleteLater();
        setProfileBusy(false);
        // 成功后目录内容已变化（新建/复制出 profile），树的探测结果与 selectedProfile 刷新都依赖它
        if (ok)
            refreshProfiles();
        emit profileOpFinished(ok, ok ? QStringLiteral("完成")
                                      : failPrefix + (summary.isEmpty() ? QString()
                                            : QLatin1Char('\n') + summary), m_profileFinishName);
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, failPrefix](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart)
            return;
        const QString reason = process->errorString();
        if (m_profileProcess == process)
            m_profileProcess = nullptr;
        process->deleteLater();
        setProfileBusy(false);
        emit profileOpFinished(false, failPrefix + QLatin1Char('\n') + reason, m_profileFinishName);
    });
    process->start();
}

QVariantMap AppController::profileInfo(const QString &home, const QString &name) const {
    return ProfileOps::readProfileInfo(profileDirForHome(home, name));
}

void AppController::createProfile(const QString &name, const QString &fromTemplate) {
    if (m_profileBusy || m_pluginBusy)
        return;
    const QString trimmed = name.trimmed();
    const QString invalid = ProfileOps::validateProfileName(trimmed);
    if (!invalid.isEmpty()) {
        emit profileOpFinished(false, invalid, trimmed);
        return;
    }
    const QString dir = profileDirForHome({}, trimmed);
    if (QFileInfo(dir).exists()) {
        emit profileOpFinished(false, QStringLiteral("目录已存在：%1")
                                     .arg(QDir::toNativeSeparators(dir)), trimmed);
        return;
    }
    // dsh 的官方创建路径：--from-default-profile 初始化目录，--dump-config 打印配置树即退出，
    // 不会拉起任何 app（boot-free）
    setProfileBusy(true);
    startProfileProcess(m_settings.resolvedDshExecutable(), {
        QStringLiteral("--profile"), trimmed,
        QStringLiteral("--from-default-profile"), fromTemplate,
        QStringLiteral("--dump-config")
    }, defaultDshHome(), {}, QStringLiteral("profile 创建失败"), trimmed);
}

void AppController::deleteProfile(const QString &home, const QString &name) {
    if (m_profileBusy || m_pluginBusy)
        return;
    if (!ProfileOps::validateProfileName(name).isEmpty()) {
        // 名字不合法通常意味着已不存在，直接按成功收尾让 UI 清掉选中态
        emit profileOpFinished(true, QString(), name);
        return;
    }
    const QString resolvedHome = resolveHome(home);
    const QString dir = resolvedHome + QStringLiteral("/profiles/") + name;
    // 被任何泊位引用（无论运行与否）都拒绝删除，提示先删泊位
    QStringList referenced;
    for (const Instance &item : m_instances.items()) {
        if (item.profile == name && resolveHome(item.dshHome) == resolvedHome)
            referenced.push_back(item.name);
    }
    if (!referenced.isEmpty()) {
        emit profileOpFinished(false, QStringLiteral("先删除引用它的泊位：")
                                     + referenced.join(QStringLiteral("、")), name);
        return;
    }
    QString error;
    if (!ProfileOps::removeProfileTree(dir, &error)) {
        emit profileOpFinished(false, error, name);
        return;
    }
    refreshProfiles();
    emit profileOpFinished(true, QStringLiteral("已删除 profile「%1」").arg(name), name);
}

void AppController::renameProfile(const QString &home, const QString &oldName, const QString &newName) {
    if (m_profileBusy || m_pluginBusy)
        return;
    const QString renamed = newName.trimmed();
    if (renamed == oldName) {
        emit profileOpFinished(true, QString(), renamed);
        return;
    }
    const QString invalid = ProfileOps::validateProfileName(renamed);
    if (!invalid.isEmpty()) {
        emit profileOpFinished(false, invalid, oldName);
        return;
    }
    const QString resolvedHome = resolveHome(home);
    const QString srcDir = resolvedHome + QStringLiteral("/profiles/") + oldName;
    const QString dstDir = resolvedHome + QStringLiteral("/profiles/") + renamed;
    if (!QFileInfo(srcDir).isDir()) {
        emit profileOpFinished(false, QStringLiteral("profile 目录不存在：%1")
                                     .arg(QDir::toNativeSeparators(srcDir)), oldName);
        return;
    }
    if (QFileInfo(dstDir).exists()) {
        emit profileOpFinished(false, QStringLiteral("目标目录已存在：%1")
                                     .arg(QDir::toNativeSeparators(dstDir)), oldName);
        return;
    }
    // 相关泊位在跑时改目录名，进程会占用 node_modules 导致 rename 失败，先拒绝
    QStringList busyNames;
    for (const Instance &item : m_instances.items()) {
        if (item.profile != oldName || resolveHome(item.dshHome) != resolvedHome)
            continue;
        if (item.status == QLatin1String("running") || item.status == QLatin1String("starting")
            || item.status == QLatin1String("stopping"))
            busyNames.push_back(item.name);
    }
    if (!busyNames.isEmpty()) {
        emit profileOpFinished(false, QStringLiteral("先停止：")
                                     + busyNames.join(QStringLiteral("、")), oldName);
        return;
    }
    if (!QDir().rename(srcDir, dstDir)) {
        emit profileOpFinished(false, QStringLiteral("重命名目录失败（可能被占用）"), oldName);
        return;
    }
    // 同步引用它的泊位（instances.json 里存的是 profile 名）
    for (Instance &item : m_instances.mutableItems()) {
        if (item.profile == oldName && resolveHome(item.dshHome) == resolvedHome)
            item.profile = renamed;
    }
    refreshProfiles();
    emit profileOpFinished(true, QStringLiteral("已重命名为「%1」").arg(renamed), renamed);
}

void AppController::copyProfile(const QString &home, const QString &srcName, const QString &newName) {
    if (m_profileBusy || m_pluginBusy)
        return;
    const QString target = newName.trimmed();
    const QString invalid = ProfileOps::validateProfileName(target);
    if (!invalid.isEmpty()) {
        emit profileOpFinished(false, invalid, srcName);
        return;
    }
    const QString resolvedHome = resolveHome(home);
    const QString srcDir = resolvedHome + QStringLiteral("/profiles/") + srcName;
    const QString dstDir = resolvedHome + QStringLiteral("/profiles/") + target;
    if (!QFileInfo(srcDir).isDir()) {
        emit profileOpFinished(false, QStringLiteral("profile 目录不存在：%1")
                                     .arg(QDir::toNativeSeparators(srcDir)), srcName);
        return;
    }
    if (QFileInfo(dstDir).exists()) {
        emit profileOpFinished(false, QStringLiteral("目标目录已存在：%1")
                                     .arg(QDir::toNativeSeparators(dstDir)), srcName);
        return;
    }
    QString error;
    if (!ProfileOps::copyProfileFiles(srcDir, dstDir, &error)) {
        emit profileOpFinished(false, error, srcName);
        return;
    }
    // 找不到 pnpm 时配置已复制完，不再装依赖（纯 dsh 内置 bundle 的 profile 本就不需要）
    if (QStandardPaths::findExecutable(QStringLiteral("pnpm")).isEmpty()) {
        emit profileOpFinished(true, QStringLiteral("已复制配置文件；未找到 pnpm，依赖未重建"), target);
        return;
    }
    // 异步在新目录跑 dsh plugin install（转发 pnpm install），从 lockfile 重建依赖
    setProfileBusy(true);
    startProfileProcess(m_settings.resolvedDshExecutable(), {
        QStringLiteral("plugin"),
        QStringLiteral("--profile"), target,
        QStringLiteral("install")
    }, resolvedHome, dstDir, QStringLiteral("目录已复制，但依赖安装失败"), target);
}

QString AppController::createInstanceFor(const QString &home, const QString &profile) {
    Instance item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    int port = 3080;
    while (m_instances.containsPort(port))
        port += 1;
    item.port = port;
    // 命名沿用「泊位 N」惯例，名字里带上 profile 方便区分
    item.name = QStringLiteral("%1 · 泊位 %2").arg(profile, QString::number(m_instances.rowCount() + 1));
    item.profile = profile.trimmed().isEmpty() ? QStringLiteral("web") : profile.trimmed();
    // 默认 home 的泊位 dshHome 留空，保持数据整洁（resolveHome(空) == defaultHome）
    item.dshHome = resolveHome(home) == defaultDshHome() ? QString() : resolveHome(home);
    item.status = QStringLiteral("stopped");
    item.logPath = m_settings.dataDir() + QStringLiteral("/logs/") + item.id + QStringLiteral(".log");
    m_instances.upsert(item);
    save();
    return item.id;
}

void AppController::openProfileDir(const QString &home, const QString &name) {
    const QString dir = profileDirForHome(home, name);
    if (!QFileInfo(dir).isDir()) {
        emit notice(QStringLiteral("profile 目录不存在：%1").arg(QDir::toNativeSeparators(dir)));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QString AppController::statusText(const QString &status) const {
    if (status == QLatin1String("running"))
        return QStringLiteral("运行中");
    if (status == QLatin1String("starting"))
        return QStringLiteral("启动中");
    if (status == QLatin1String("stopping"))
        return QStringLiteral("停止中");
    if (status == QLatin1String("failed"))
        return QStringLiteral("失败");
    return QStringLiteral("已停止");
}

// 全量重建泊位树：home 归一成 resolve 后的路径（profile 面板的操作也按归一值进行），
// knownProfiles 属于默认 home；目录缺失的组合进 missingKeys 只影响 DirExistsRole 展示
void AppController::rebuildTree() {
    QList<Instance> normalized;
    for (const Instance &item : m_instances.items()) {
        Instance copy = item;
        copy.dshHome = resolveHome(item.dshHome);
        normalized.push_back(copy);
    }
    const QString home = defaultDshHome();
    QSet<QString> missing;
    QSet<QString> seen;
    auto checkDir = [&](const QString &profileHome, const QString &profileName) {
        const QString key = profileHome + QChar(u'\u0001') + profileName;
        if (seen.contains(key))
            return;
        seen.insert(key);
        if (!QFileInfo(profileHome + QStringLiteral("/profiles/") + profileName).isDir())
            missing.insert(key);
    };
    for (const Instance &item : normalized)
        checkDir(item.dshHome, item.profile);
    for (const QString &name : m_knownProfiles)
        checkDir(home, name);
    m_profileTree.rebuild(normalized, m_knownProfiles, home, missing);
}

void AppController::load() {
    QFile file(instancesPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QList<Instance> items;
    const auto array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        Instance item;
        item.id = obj.value(QStringLiteral("id")).toString();
        if (item.id.isEmpty())
            continue;
        item.name = obj.value(QStringLiteral("name")).toString();
        item.port = obj.value(QStringLiteral("port")).toInt(3080);
        item.profile = obj.value(QStringLiteral("profile")).toString(QStringLiteral("web"));
        item.dshHome = obj.value(QStringLiteral("dshHome")).toString();
        item.workspace = obj.value(QStringLiteral("workspace")).toString();
        item.autostart = obj.value(QStringLiteral("autostart")).toBool(false);
        item.status = QStringLiteral("stopped");
        item.logPath = obj.value(QStringLiteral("logPath")).toString();
        if (item.logPath.isEmpty())
            item.logPath = m_settings.dataDir() + QStringLiteral("/logs/") + item.id + QStringLiteral(".log");
        items.push_back(item);
    }
    m_instances.setItems(items);
}

void AppController::save() const {
    QDir().mkpath(m_settings.dataDir());
    QJsonArray array;
    for (const Instance &item : m_instances.items()) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), item.id);
        obj.insert(QStringLiteral("name"), item.name);
        obj.insert(QStringLiteral("port"), item.port);
        obj.insert(QStringLiteral("profile"), item.profile);
        obj.insert(QStringLiteral("dshHome"), item.dshHome);
        obj.insert(QStringLiteral("workspace"), item.workspace);
        obj.insert(QStringLiteral("autostart"), item.autostart);
        obj.insert(QStringLiteral("logPath"), item.logPath);
        array.append(obj);
    }
    QFile file(instancesPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
}

QString AppController::instancesPath() const {
    return m_settings.dataDir() + QStringLiteral("/instances.json");
}

void AppController::applyStatus(const QString &id, const QString &status, qint64 pid, const QString &error) {
    Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return;
    item.status = status;
    item.pid = pid;
    item.lastError = error;
    m_instances.upsert(item);
    if (!error.isEmpty())
        emit notice(item.name + QStringLiteral("：") + error);
    if (status == QLatin1String("running") && m_settings.openUiOnStart())
        openUi(id);
}
