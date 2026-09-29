#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

Settings::Settings(QObject *parent) : QObject(parent) {
    QDir().mkpath(dataDir());
    load();
}

QString Settings::dshExecutable() const { return m_dshExecutable; }

void Settings::setDshExecutable(const QString &value) {
    if (m_dshExecutable == value)
        return;
    m_dshExecutable = value;
    emit dshExecutableChanged();
}

QString Settings::resolvedDshExecutable() const {
    const QString exe = m_dshExecutable.trimmed().isEmpty() ? QStringLiteral("dsh") : m_dshExecutable.trimmed();
    // 用户填的是路径就原样使用
    if (exe.contains(QLatin1Char('/')) || exe.contains(QLatin1Char('\\')))
        return exe;
    // 裸命令名：npm 全局安装只有 dsh.cmd，CreateProcess 不会自动补 .cmd，需要先在 PATH 里找
    const QString found = QStandardPaths::findExecutable(exe);
    return found.isEmpty() ? exe : found;
}

QString Settings::nodeExecutable() const { return m_nodeExecutable; }

void Settings::setNodeExecutable(const QString &value) {
    if (m_nodeExecutable == value)
        return;
    m_nodeExecutable = value;
    emit nodeExecutableChanged();
}

QString Settings::dataDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool Settings::startMinimized() const { return m_startMinimized; }

void Settings::setStartMinimized(bool value) {
    if (m_startMinimized == value)
        return;
    m_startMinimized = value;
    emit startMinimizedChanged();
}

bool Settings::openUiOnStart() const { return m_openUiOnStart; }

void Settings::setOpenUiOnStart(bool value) {
    if (m_openUiOnStart == value)
        return;
    m_openUiOnStart = value;
    emit openUiOnStartChanged();
}

QString Settings::filePath() const {
    return dataDir() + QStringLiteral("/settings.json");
}

void Settings::load() {
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto obj = QJsonDocument::fromJson(file.readAll()).object();
    if (obj.contains(QStringLiteral("dshExecutable")))
        m_dshExecutable = obj.value(QStringLiteral("dshExecutable")).toString(m_dshExecutable);
    if (obj.contains(QStringLiteral("nodeExecutable")))
        m_nodeExecutable = obj.value(QStringLiteral("nodeExecutable")).toString();
    m_startMinimized = obj.value(QStringLiteral("startMinimized")).toBool(false);
    m_openUiOnStart = obj.value(QStringLiteral("openUiOnStart")).toBool(true);
}

void Settings::save() {
    QDir().mkpath(dataDir());
    QJsonObject obj;
    obj.insert(QStringLiteral("dshExecutable"), m_dshExecutable);
    obj.insert(QStringLiteral("nodeExecutable"), m_nodeExecutable);
    obj.insert(QStringLiteral("startMinimized"), m_startMinimized);
    obj.insert(QStringLiteral("openUiOnStart"), m_openUiOnStart);
    QFile file(filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}
