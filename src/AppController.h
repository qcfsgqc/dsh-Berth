#pragma once

#include "InstanceModel.h"
#include "Settings.h"
#include "Supervisor.h"

#include <QObject>

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(InstanceModel *instances READ instances CONSTANT)
    Q_PROPERTY(Settings *settings READ settings CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString defaultHome READ defaultDshHome NOTIFY profilesChanged)
    Q_PROPERTY(QStringList knownProfiles READ knownProfiles NOTIFY profilesChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    InstanceModel *instances();
    Settings *settings();
    QString version() const;

    Q_INVOKABLE QString createInstance();
    Q_INVOKABLE void removeInstance(const QString &id);
    Q_INVOKABLE void updateInstance(const QString &id, const QString &name, int port,
                                    const QString &profile, const QString &dshHome,
                                    const QString &workspace, bool autostart);
    Q_INVOKABLE void startInstance(const QString &id);
    Q_INVOKABLE void stopInstance(const QString &id);
    Q_INVOKABLE void restartInstance(const QString &id);
    Q_INVOKABLE void openUi(const QString &id);
    Q_INVOKABLE void openLog(const QString &id);
    Q_INVOKABLE QString statusText(const QString &status) const;
    Q_INVOKABLE QVariantMap instance(const QString &id) const;
    Q_INVOKABLE QString defaultDshHome() const;
    Q_INVOKABLE QStringList detectProfiles(const QString &dshHome) const;
    Q_INVOKABLE int importDetectedProfiles();
    QStringList knownProfiles() const;

signals:
    void notice(const QString &message);
    void profilesChanged();

private:
    void load();
    void save() const;
    QString instancesPath() const;
    void applyStatus(const QString &id, const QString &status, qint64 pid, const QString &error);

    QString resolveHome(const QString &dshHome) const;
    void refreshProfiles();

    InstanceModel m_instances;
    Settings m_settings;
    Supervisor m_supervisor;
    QStringList m_knownProfiles;
};
