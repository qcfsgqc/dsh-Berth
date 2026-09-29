#pragma once

#include "Instance.h"

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Supervisor : public QObject {
    Q_OBJECT

public:
    explicit Supervisor(QObject *parent = nullptr);
    ~Supervisor() override;

    void start(const Instance &instance, const QString &dshExecutable);
    void stop(const QString &id);
    bool isRunning(const QString &id) const;

signals:
    void statusChanged(const QString &id, const QString &status, qint64 pid, const QString &error);

private:
    void probe(const QString &id, int port);
    void attachProcess(const QString &id, QProcess *process);

    QHash<QString, QProcess *> m_processes;
    QHash<QString, QTimer *> m_probes;
};
