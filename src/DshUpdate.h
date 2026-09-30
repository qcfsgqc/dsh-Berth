#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class Settings;

// 通过 npm 检查并更新全局安装的 dsh（@deepseek-ai/dsh）。
// 仅适用于 npm 全局安装方式；从源码或 pnpm 等其它方式安装的 dsh 需要用户自行更新。
class DshUpdate : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString installedVersion READ installedVersion NOTIFY installedVersionChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)

public:
    explicit DshUpdate(QObject *parent = nullptr);

    // AppController 构造后注入（持有的是 AppController 里的 Settings 成员地址）
    void setSettings(Settings *settings);

    bool busy() const;
    QString installedVersion() const;
    QString latestVersion() const;

    // 查已安装版本：<resolvedDshExecutable()> --version
    Q_INVOKABLE void checkInstalled();
    // 查 npm 上的最新版本：npm view @deepseek-ai/dsh version
    Q_INVOKABLE void checkLatest();
    // 执行更新：npm install -g @deepseek-ai/dsh@latest；成功后自动复查本地版本
    Q_INVOKABLE void updateDsh();

signals:
    void busyChanged();
    void installedVersionChanged();
    void latestVersionChanged();
    void checkInstalledFinished(bool ok, const QString &version, const QString &error);
    void checkLatestFinished(bool ok, const QString &version, const QString &error);
    void updateFinished(bool ok, const QString &message);

private:
    // 同一时段只跑一个 QProcess；三个 Q_INVOKABLE 都经由 startTask 启动
    enum class Task { CheckInstalled, CheckLatest, Update };
    // 启动新任务；busy 时放入单槽队列，收尾后由 drainPending 续跑
    // （QML 会连发 checkInstalled + checkLatest，若直接丢弃会导致 npm 版本查不到）
    bool startTask(Task task, const QString &program, const QStringList &args);
    // 收尾后补跑队列中的任务（空闲进程不存在时是空操作）
    void drainPending();
    // checkInstalled 的结果统一从这里发出；更新成功后的自动复查会转成 updateFinished
    void reportCheckInstalled(bool ok, const QString &version, const QString &error);
    void setBusy(bool busy);
    void setInstalledVersion(const QString &version);
    void setLatestVersion(const QString &version);

    Settings *m_settings = nullptr;
    // 每个任务新建 QProcess，结束时置回空；同一时刻最多只有一个在跑
    QProcess *m_process = nullptr;
    // busy 时到达的任务队列槽：只保留最先排入的一个，再来的直接丢
    bool m_pending = false;
    Task m_pendingTask = Task::CheckInstalled;
    QString m_pendingProgram;
    QStringList m_pendingArgs;
    bool m_busy = false;
    // npm 安装成功后正在等 checkInstalled 的结果（最终消息走 updateFinished 发出）
    bool m_awaitPostUpdateCheck = false;
    QString m_installedVersion;
    QString m_latestVersion;
};
