#pragma once

#include "Instance.h"

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

// 一次泊位进程启动所需的全部参数，由调用方（启动流水线）组装好再交给 Supervisor
struct LaunchSpec {
    QString id;                  // 泊位 id
    QString exe;                 // 可执行文件；空时回落为 "dsh"
    QStringList args;            // 完整参数（--profile <p> --port <n> --no-open + 额外参数）
    QProcessEnvironment env;     // 完整子进程环境
    QString cwd;                 // 工作目录；空时不设置
    int port = 0;                // 探活端口
    QString logPath;             // stdout/stderr 追加写入的日志文件
    QStringList secrets;         // 敏感值：写 Berth 标记行（如"启动:"命令行）前脱敏
};

// 状态字符串：stopped / starting / running / stopping / failed / external / crashStopped。
// external（运行中（外部））由 attachExternal 产生；crashStopped（崩溃已停止）由自动重启逻辑设置，
// Supervisor 自身在非请求退出时只发 failed + crashed。
class Supervisor : public QObject {
    Q_OBJECT

public:
    explicit Supervisor(QObject *parent = nullptr);
    ~Supervisor() override;

    // 进程成功创建返回 true；已在管理中或创建失败返回 false（失败时已发 failed）
    bool start(const LaunchSpec &spec);
    // 请求停止：设置"请求停止"标志并结束整棵进程树；外部进程只解除关联
    void stop(const QString &id);
    // 请求停止并直接结束整个 Job（不等待），退出后由 finished 回调发 stopped
    void forceKill(const QString &id);
    // 关联一个不由 Berth 创建的进程（复用现有服务）；不纳入 Job，只记录 pid/端口
    void attachExternal(const QString &id, qint64 pid, int port);
    bool isRunning(const QString &id) const;
    bool isExternal(const QString &id) const;
    // Windows Job Object 句柄（HANDLE），供指标采集；没有时返回 nullptr
    void *jobHandle(const QString &id) const;

signals:
    void statusChanged(const QString &id, const QString &status, qint64 pid, const QString &error);
    // 进程在未请求停止的情况下退出（不管退出码）
    void crashed(const QString &id, int exitCode);
    // 由 Berth 创建的 dsh 进程已启动并纳入 Job（attachExternal 不发）；供 ProcessMetrics 计启动次数
    void processStarted(const QString &id);
    // 由 Berth 创建的 dsh 进程已退出（请求停止与意外退出都发；析构时不发）；crash 为未请求的退出
    void processExited(const QString &id, int exitCode, bool crash);

private:
    struct External {
        qint64 pid = 0;
        int port = 0;
    };

    void probe(const QString &id, int port);
    void attachJob(const QString &id, QProcess *process);
    void releaseJob(const QString &id);
    void killTree(const QString &id, QProcess *process);
    static void markStopRequested(QProcess *process);
    static bool stopRequested(const QProcess *process);

    QHash<QString, QProcess *> m_processes;
    QHash<QString, QTimer *> m_probes;
    // Windows Job Object 句柄（HANDLE），用来连带子孙进程一起结束
    QHash<QString, void *> m_jobs;
    QHash<QString, External> m_externals;
};
