#include "AppController.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

AppController::AppController(QObject *parent)
    : QObject(parent), m_instances(this), m_settings(this), m_supervisor(this) {
    connect(&m_supervisor, &Supervisor::statusChanged, this, &AppController::applyStatus);
    load();
    refreshProfiles();
    if (m_instances.rowCount() == 0 && !m_knownProfiles.isEmpty())
        importDetectedProfiles();
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
    m_supervisor.start(item, m_settings.dshExecutable());
}

void AppController::stopInstance(const QString &id) {
    m_supervisor.stop(id);
}

void AppController::restartInstance(const QString &id) {
    stopInstance(id);
    startInstance(id);
}

void AppController::openUi(const QString &id) {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(item.port)));
}

void AppController::openLog(const QString &id) {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty() || item.logPath.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(item.logPath));
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
    if (found == m_knownProfiles)
        return;
    m_knownProfiles = found;
    emit profilesChanged();
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
