#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

// 插件变动后的重启提示（Restart_Hint）与"待重启"标记。
// - raise：PluginManager::restartHintRequested 触发；提示正在显示时合并进当前列表（同一泊位只列一次）
// - restartAll：按列表顺序逐个重启，前一个进入运行中或失败后才开始下一个；跳过已不在启动中/运行中的泊位；
//   结束后发 finished 汇总，失败泊位加"待重启"标记
// - later：关闭提示并给列表中每个泊位加"待重启"标记；泊位下一次进入运行中时移除（被停止时保留）
// 通过回调访问泊位数据与启动流水线，不直接依赖 AppController。
class RestartHintController : public QObject {
    Q_OBJECT
    // 提示条是否显示
    Q_PROPERTY(bool visible READ visible NOTIFY hintChanged)
    // 提示中的泊位 id 与名称（按泊位列表顺序）
    Q_PROPERTY(QStringList ids READ ids NOTIFY hintChanged)
    Q_PROPERTY(QStringList names READ names NOTIFY hintChanged)
    // "全部重启"进行中
    Q_PROPERTY(bool busy READ busy NOTIFY progressChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    // 带"待重启"标记的泊位：id → true
    Q_PROPERTY(QVariantMap pending READ pending NOTIFY pendingChanged)

public:
    struct Ops {
        std::function<QString(const QString &)> statusOf;
        std::function<QString(const QString &)> nameOf;
        // 停止后走启动流水线；拒绝时返回 false 并写 reason
        std::function<bool(const QString &id, QString *reason)> restart;
    };

    explicit RestartHintController(Ops ops, QObject *parent = nullptr);

    bool visible() const { return m_visible; }
    QStringList ids() const { return m_ids; }
    QStringList names() const;
    bool busy() const { return m_running; }
    int done() const { return m_done; }
    int total() const { return int(m_queue.size()); }
    QVariantMap pending() const;

    // 合并受影响泊位并显示提示；ids 为空时不做任何事
    void raise(const QString &home, const QString &profile, const QStringList &ids);
    Q_INVOKABLE void restartAll();
    // "稍后"或关闭提示
    Q_INVOKABLE void later();
    Q_INVOKABLE bool isPending(const QString &id) const { return m_pending.contains(id); }

    // 泊位状态变化（由 AppController::applyStatus 转发）
    void onStatus(const QString &id, const QString &status, const QString &error);
    // 泊位被删除：从提示与标记中移除，在途的按失败收尾
    void onRemoved(const QString &id);

signals:
    void hintChanged();
    void progressChanged();
    void pendingChanged();
    // summary：{succeeded, failed, skipped, failures:[{name, reason}], text}
    void finished(const QVariantMap &summary);

private:
    static bool isActive(const QString &status);
    void hide();
    void addPending(const QString &id);
    void next();
    void markSucceeded();
    void markFailed(const QString &reason);
    void finish();

    Ops m_ops;
    bool m_visible = false;
    QStringList m_ids;
    QSet<QString> m_pending;

    // "全部重启"队列
    bool m_running = false;
    bool m_launching = false;   // 正在调用 restart 回调（其间的 stopped 状态不算失败）
    QStringList m_queue;
    int m_done = 0;
    QString m_current;          // 已发起、等待进入运行中或失败的泊位
    int m_succeeded = 0;
    int m_skipped = 0;
    QVariantList m_failures;    // [{name, reason}]
};
