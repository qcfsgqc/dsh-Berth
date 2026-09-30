#pragma once

#include <QObject>
#include <QString>

// 读写 HKCU\Software\Microsoft\Windows\CurrentVersion\Run 下的 "DSH Berth" 启动项。
// 只动这一个值，不影响其他启动项；不需要管理员权限。
class AutostartRegistry : public QObject {
    Q_OBJECT
    // 按注册表实际状态：值存在、指向当前 exe 且带 --autostart 时为 true
    Q_PROPERTY(bool registered READ isRegistered NOTIFY registeredChanged)

public:
    explicit AutostartRegistry(QObject *parent = nullptr);

    bool isRegistered() const;
    // 写入（on=true）或移除（on=false）启动项；失败返回 false 并写 err
    bool set(bool on, QString *err);

    // 供 QML 调用：成功返回空串，失败返回原因；无论成败都发 registeredChanged，
    // 让开关回到注册表实际状态
    Q_INVOKABLE QString apply(bool on);
    // 重新读取注册表（设置页打开时调用）
    Q_INVOKABLE void refresh();

signals:
    void registeredChanged();
};
