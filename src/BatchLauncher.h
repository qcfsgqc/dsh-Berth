#pragma once

#include "core/BatchPlan.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <functional>

// 批量启停：启动全部 / 停止全部 / 启动所选 / 停止所选，以及主窗口初始化后的错峰自启。
// 启动按 BatchPlan 错峰逐个发起（不等上一个进入运行中）；停止对全部活动泊位同时发起。
// 通过回调访问泊位数据与启动流水线，不直接依赖 AppController。
class BatchLauncher : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(int done READ done NOTIFY stateChanged)
    Q_PROPERTY(int total READ total NOTIFY stateChanged)
    // "start" / "stop"；空闲时为空
    Q_PROPERTY(QString kind READ kind NOTIFY stateChanged)
    // 全部泊位数；为 0 时界面禁用"启动全部/停止全部"
    Q_PROPERTY(int allCount READ allCount NOTIFY allCountChanged)

public:
    struct Ops {
        std::function<QStringList()> allIds;                    // 泊位列表当前显示顺序
        std::function<QStringList()> autostartIds;              // autostart 为真的泊位，同样顺序
        std::function<QString(const QString &)> statusOf;       // 状态字符串
        std::function<QString(const QString &)> nameOf;         // 泊位名
        std::function<int()> intervalSec;                       // 错峰间隔（秒）
        // 走启动流水线；拒绝时返回 false 并写 reason
        std::function<bool(const QString &id, bool autostart, QString *reason)> start;
        std::function<void(const QString &id)> stop;
    };

    explicit BatchLauncher(Ops ops, QObject *parent = nullptr);

    bool busy() const { return !m_kind.isEmpty(); }
    int done() const { return m_done; }
    int total() const { return m_total; }
    QString kind() const { return m_kind; }
    int allCount() const;

    Q_INVOKABLE void startAll();
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void startSelected(const QStringList &ids);
    Q_INVOKABLE void stopSelected(const QStringList &ids);
    // 主窗口初始化完成后调用：错峰启动 autostart 泊位；没有时什么都不做
    Q_INVOKABLE void startAutostart();
    // 取消：未发起的泊位不再发起并保持原状态，已发起的继续完成
    Q_INVOKABLE void cancel();

    // 泊位状态变化（由 AppController::applyStatus 转发）
    void onStatus(const QString &id, const QString &status, const QString &error);
    // 泊位被删除：在途的按失败收尾，避免批量一直挂起
    void onRemoved(const QString &id);
    // 泊位增删后调用，刷新 allCount
    void notifyTargetsChanged() { emit allCountChanged(); }

signals:
    void stateChanged();
    void allCountChanged();
    // summary：{kind, succeeded, failed, failures:[{name, reason}], unprocessed:[name], cancelled, text}
    void finished(const QVariantMap &summary);

private:
    void beginStart(const QStringList &targets, bool autostart);
    void beginStop(const QStringList &targets);
    void scheduleNext();
    void launchNext();
    void markSucceeded(const QString &id);
    void markFailed(const QString &id, const QString &reason);
    void maybeFinish();
    void reset();

    Ops m_ops;
    QString m_kind;
    bool m_autostart = false;
    bool m_cancelled = false;
    int m_done = 0;
    int m_total = 0;
    int m_succeeded = 0;
    BatchPlan::Plan m_plan;
    int m_next = 0;                 // 下一个待发起的 step 下标
    QSet<QString> m_inFlight;       // 已发起、尚未完成的泊位
    QVariantList m_failures;        // [{name, reason}]
    QStringList m_unprocessed;      // 因取消未处理的泊位名
    QElapsedTimer m_clock;
    QTimer m_timer;
};
