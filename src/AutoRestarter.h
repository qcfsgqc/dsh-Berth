#pragma once

#include "core/RestartState.h"

#include <QHash>
#include <QObject>
#include <QString>

class AppController;
class Supervisor;
class QTimer;

// 崩溃自动重启：每个泊位持有一个 RestartState，用 QTimer 执行退避等待与"运行满 120 秒清零"。
// - 监听 Supervisor::crashed / statusChanged；重启走 AppController::launch(id, LaunchReason::AutoRestart)
// - launch 返回 ok=false，或自动重启的进程在进入"运行中"前退出（Supervisor 同样发 crashed），都算一次 crash
// - "运行中（外部）"期间不安排重启
// 配置（开关、baseSec/maxSec/maxAttempts）在每次 crash 时从泊位与设置读取。
class AutoRestarter : public QObject {
    Q_OBJECT

public:
    AutoRestarter(AppController *controller, Supervisor *supervisor, QObject *parent = nullptr);
    ~AutoRestarter() override;

    // 用户手动停止：取消计划、n 清零、移除倒计时
    void onManualStop(const QString &id);
    // 用户手动启动（含重启按钮）：先调用本方法将 n 清零，再执行启动
    void onManualStart(const QString &id);
    // 泊位的"崩溃自动重启"开关变化；关闭时取消计划并清零
    void onToggle(const QString &id, bool on);
    // 取消尚未执行的重启计划并移除倒计时（n 不变）
    void cancel(const QString &id);
    // 泊位被删除：释放全部状态与定时器
    void forget(const QString &id);

signals:
    // 退避等待中每秒发一次：sec 为剩余秒数（向上取整），n/N 为当前次数与上限。
    // sec == 0 表示倒计时已移除（重启开始、取消或清零）
    void countdown(const QString &id, int sec, int n, int N);
    // 已达上限，不再重启；attempts 为已自动重启的次数
    void gaveUp(const QString &id, int attempts);

private:
    struct Entry {
        RestartState state;
        QTimer *restart = nullptr; // 退避等待结束后执行重启（单次）
        QTimer *tick = nullptr;    // 每秒刷新倒计时
        QTimer *reset = nullptr;   // 进入"运行中"后 120 秒清零（单次）
        qint64 deadlineMs = 0;     // 重启时刻（QDateTime::currentMSecsSinceEpoch 基准）
    };

    Entry &entry(const QString &id);
    void handleCrash(const QString &id);
    void handleStatus(const QString &id, const QString &status);
    void fireRestart(const QString &id);
    void emitTick(const QString &id);
    // 停掉退避与倒计时定时器；有倒计时时发 countdown(id, 0, ...)
    void stopPending(Entry &e, const QString &id);

    AppController *m_controller;
    QHash<QString, Entry> m_entries;
};
