#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringDecoder>
#include <QStringList>

#include <functional>

class QProcess;
class QProcessEnvironment;
class QTimer;

// 单次运行的参数
struct ProcessOptions {
    // 追加/覆盖到子进程环境；在系统环境与注入钩子之后应用，优先级最高
    QHash<QString, QString> env;
    // 工作目录；空表示继承 Berth 的当前目录
    QString cwd;
    // 超时毫秒；<= 0 表示不限时
    int timeoutMs = 0;
    // 只保留输出末尾的非空行数；<= 0 表示不保留
    int tailLines = 20;
};

// 一次后台运行。由 ProcessRunner::run 创建，发出 finished 后自动 deleteLater。
// finished 只发一次；启动失败时也经由 finished 通知（failedToStart() 为真，code = -1）。
class ProcessTask : public QObject {
    Q_OBJECT
public:
    ~ProcessTask() override;

    bool isRunning() const;
    bool failedToStart() const { return m_failedToStart; }
    // 启动失败 / 崩溃 / 超时时的原因；正常退出时为空
    QString errorString() const { return m_errorString; }
    QString tail() const;
    // 结束整个 Job（连带子孙进程）；之后照常发 finished(ok=false)
    void kill();

signals:
    void finished(bool ok, int code, const QString &tail, bool timedOut);

private:
    friend class ProcessRunner;
    ProcessTask(int tailLines, QObject *parent);

    void start(const QString &program, const QStringList &args, const QProcessEnvironment &env,
               const QString &cwd, int timeoutMs);
    void appendOutput(const QByteArray &bytes);
    void pushLine(QString line);
    void flushPending();
    void finish(bool ok, int code);
    void onTimeout();
    // Runner 析构时调用：不再发任何信号，只负责把进程树杀干净
    void abandon();

    void attachJob();
    void terminateJob();
    void closeJob();

    QProcess *m_process = nullptr;
    QTimer *m_timer = nullptr;
    // Windows Job Object 句柄（HANDLE），KILL_ON_JOB_CLOSE
    void *m_job = nullptr;
    int m_tailLimit = 20;
    QStringDecoder m_decoder;
    QString m_pending;
    QStringList m_lines;
    QString m_errorString;
    bool m_failedToStart = false;
    bool m_timedOut = false;
    bool m_done = false;
};

// 统一的后台子进程执行：CREATE_NO_WINDOW、每次运行一个 Job Object、超时、保留输出尾部。
// 析构时关闭全部 Job 句柄，连带结束仍在运行的进程树。
// 泊位 dsh 进程不经过这里，仍由 Supervisor 自己的 Job 管理。
class ProcessRunner : public QObject {
    Q_OBJECT
public:
    // 环境注入钩子：每次 run 时调用，返回要追加到子进程环境的变量（后续由 ProxyManager 提供）
    using EnvHook = std::function<QHash<QString, QString>()>;

    explicit ProcessRunner(QObject *parent = nullptr);
    ~ProcessRunner() override;

    void setEnvHook(EnvHook hook);

    // 立即开始运行；调用方在返回后连接 finished 即可（启动失败也会在事件循环里再通知）
    ProcessTask *run(const QString &program, const QStringList &args,
                     const ProcessOptions &options = {});

private:
    EnvHook m_envHook;
    QList<ProcessTask *> m_tasks;
};
