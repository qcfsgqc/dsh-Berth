#include "AutostartRegistry.h"

#include "core/RunValue.h"

#include <QCoreApplication>
#include <QSettings>

namespace {
const QString kRunKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kValueName = QStringLiteral("DSH Berth");

QString statusText(QSettings::Status s) {
    switch (s) {
    case QSettings::AccessError:
        return QStringLiteral("没有写入 Windows 启动项的权限");
    case QSettings::FormatError:
        return QStringLiteral("Windows 启动项格式错误");
    default:
        return QString();
    }
}
} // namespace

AutostartRegistry::AutostartRegistry(QObject *parent) : QObject(parent) {}

bool AutostartRegistry::isRegistered() const {
    QSettings run(kRunKey, QSettings::NativeFormat);
    const QString value = run.value(kValueName).toString();
    return RunValue::matches(value, QCoreApplication::applicationFilePath());
}

bool AutostartRegistry::set(bool on, QString *err) {
    QSettings run(kRunKey, QSettings::NativeFormat);
    const QString exe = QCoreApplication::applicationFilePath();
    if (on)
        run.setValue(kValueName, RunValue::make(exe));
    else
        run.remove(kValueName);
    run.sync();

    QString reason = statusText(run.status());
    // 写入后回读确认，状态码正常但值没落地时也按失败处理
    if (reason.isEmpty()) {
        QSettings check(kRunKey, QSettings::NativeFormat);
        const bool present = check.contains(kValueName);
        if (on && !RunValue::matches(check.value(kValueName).toString(), exe))
            reason = QStringLiteral("写入 Windows 启动项后回读不一致");
        else if (!on && present)
            reason = QStringLiteral("移除 Windows 启动项失败");
    }
    if (!reason.isEmpty()) {
        if (err)
            *err = reason;
        return false;
    }
    return true;
}

QString AutostartRegistry::apply(bool on) {
    QString err;
    const bool ok = set(on, &err);
    emit registeredChanged();
    return ok ? QString() : (on ? QStringLiteral("开启开机自启失败：") : QStringLiteral("关闭开机自启失败：")) + err;
}

void AutostartRegistry::refresh() {
    emit registeredChanged();
}
