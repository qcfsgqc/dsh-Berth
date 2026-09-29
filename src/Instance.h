#pragma once

#include <QString>

struct Instance {
    QString id;
    QString name;
    int port = 3080;
    QString profile = QStringLiteral("web");
    QString dshHome;
    QString workspace;
    bool autostart = false;
    QString status = QStringLiteral("stopped");
    qint64 pid = 0;
    QString lastError;
    QString logPath;
};
