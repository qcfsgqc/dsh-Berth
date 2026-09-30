#include "BatchLauncher.h"

#include <QHash>

#include <algorithm>
#include <utility>

namespace {
// 批量停止的活动判定：在 Active_State 之外还包括"运行中（外部）"（停止即解除关联）
bool isStopActive(const QString &status) {
    return BatchPlan::isActiveState(status) || status == QLatin1String("external");
}
} // namespace

BatchLauncher::BatchLauncher(Ops ops, QObject *parent) : QObject(parent), m_ops(std::move(ops)) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &BatchLauncher::launchNext);
}

int BatchLauncher::allCount() const {
    return m_ops.allIds ? int(m_ops.allIds().size()) : 0;
}

void BatchLauncher::startAll() { beginStart(m_ops.allIds(), false); }
void BatchLauncher::stopAll() { beginStop(m_ops.allIds()); }
void BatchLauncher::startSelected(const QStringList &ids) { beginStart(ids, false); }
void BatchLauncher::stopSelected(const QStringList &ids) { beginStop(ids); }
void BatchLauncher::startAutostart() { beginStart(m_ops.autostartIds(), true); }

void BatchLauncher::beginStart(const QStringList &targets, bool autostart) {
    // 进行中禁止发起其它批量；目标为空不做任何启停
    if (busy() || targets.isEmpty())
        return;
    QHash<QString, QString> statuses;
    for (const QString &id : targets) {
        const QString s = m_ops.statusOf(id);
        // 外部服务也视为已在运行，跳过
        statuses.insert(id, s == QLatin1String("external") ? QStringLiteral("running") : s);
    }
    m_plan = BatchPlan::plan(targets, statuses, m_ops.intervalSec());
    m_kind = QStringLiteral("start");
    m_autostart = autostart;
    m_total = int(targets.size());
    m_done = int(m_plan.skipped.size());
    m_next = 0;
    m_clock.start();
    emit stateChanged();
    scheduleNext();
}

void BatchLauncher::beginStop(const QStringList &targets) {
    if (busy() || targets.isEmpty())
        return;
    m_kind = QStringLiteral("stop");
    m_total = int(targets.size());
    m_done = 0;
    QStringList active;
    for (const QString &id : targets) {
        if (isStopActive(m_ops.statusOf(id))) {
            active.append(id);
            m_inFlight.insert(id);
        } else {
            ++m_done; // 非活动目标直接计入已完成
        }
    }
    m_succeeded = m_done;
    emit stateChanged();
    // 同时对全部活动泊位发起停止（不错峰、不等待前一个）
    for (const QString &id : active) {
        m_ops.stop(id);
        // 停止可能同步完成（状态经 onStatus 已收尾）；仍在途且已非活动的在此收尾
        if (m_inFlight.contains(id) && !isStopActive(m_ops.statusOf(id)))
            markSucceeded(id);
    }
    maybeFinish();
}

void BatchLauncher::scheduleNext() {
    if (!busy())
        return;
    if (m_cancelled || m_next >= m_plan.steps.size()) {
        maybeFinish();
        return;
    }
    const qint64 delay = std::max<qint64>(0, m_plan.steps.at(m_next).atMs - m_clock.elapsed());
    m_timer.start(int(delay));
}

void BatchLauncher::launchNext() {
    if (!busy() || m_cancelled || m_next >= m_plan.steps.size())
        return;
    const QString id = m_plan.steps.at(m_next++).id;
    QString reason;
    if (!m_ops.start(id, m_autostart, &reason)) {
        // 端口冲突、版本缺失等被拒绝：计为失败，按间隔继续后面的泊位
        markFailed(id, reason.isEmpty() ? QStringLiteral("启动被拒绝") : reason);
    } else {
        const QString s = m_ops.statusOf(id);
        if (s == QLatin1String("running") || s == QLatin1String("external"))
            markSucceeded(id);
        else if (BatchPlan::isActiveState(s))
            m_inFlight.insert(id); // 等待进入运行中或被判定失败
        else
            markFailed(id, QStringLiteral("启动失败"));
    }
    emit stateChanged();
    scheduleNext();
}

void BatchLauncher::onStatus(const QString &id, const QString &status, const QString &error) {
    if (!busy() || !m_inFlight.contains(id))
        return;
    if (m_kind == QLatin1String("start")) {
        if (status == QLatin1String("running") || status == QLatin1String("external"))
            markSucceeded(id);
        else if (!BatchPlan::isActiveState(status))
            markFailed(id, error.isEmpty() ? QStringLiteral("进入运行中前退出") : error);
        else
            return;
    } else {
        if (isStopActive(status))
            return;
        markSucceeded(id);
    }
    emit stateChanged();
    maybeFinish();
}

void BatchLauncher::onRemoved(const QString &id) {
    if (!busy() || !m_inFlight.contains(id))
        return;
    markFailed(id, QStringLiteral("泊位已删除"));
    emit stateChanged();
    maybeFinish();
}

void BatchLauncher::cancel() {
    if (!busy() || m_cancelled)
        return;
    m_cancelled = true;
    m_timer.stop();
    // 只有批量启动存在未发起的泊位；已发起的继续完成。
    // 以实际发起下标为准（而非按时间推算），避免定时器延迟造成遗漏
    if (m_kind == QLatin1String("start")) {
        for (int i = m_next; i < m_plan.steps.size(); ++i)
            m_unprocessed.append(m_ops.nameOf(m_plan.steps.at(i).id));
        m_next = int(m_plan.steps.size());
    }
    emit stateChanged();
    maybeFinish();
}

void BatchLauncher::markSucceeded(const QString &id) {
    m_inFlight.remove(id);
    ++m_succeeded;
    ++m_done;
}

void BatchLauncher::markFailed(const QString &id, const QString &reason) {
    m_inFlight.remove(id);
    QVariantMap entry;
    entry.insert(QStringLiteral("name"), m_ops.nameOf(id));
    entry.insert(QStringLiteral("reason"), reason);
    m_failures.append(entry);
    ++m_done;
}

void BatchLauncher::maybeFinish() {
    if (!busy())
        return;
    if (m_kind == QLatin1String("start") && !m_cancelled && m_next < m_plan.steps.size())
        return;
    if (!m_inFlight.isEmpty())
        return;

    const bool isStart = m_kind == QLatin1String("start");
    const QString title = !isStart ? QStringLiteral("批量停止")
                        : m_autostart ? QStringLiteral("错峰自启") : QStringLiteral("批量启动");
    QStringList lines;
    if (isStart) {
        QString head = QStringLiteral("%1%2：成功 %3，失败 %4")
                           .arg(title, m_cancelled ? QStringLiteral("已取消") : QStringLiteral("完成"))
                           .arg(m_succeeded)
                           .arg(m_failures.size());
        if (!m_plan.skipped.isEmpty())
            head += QStringLiteral("，跳过 %1（已在运行）").arg(m_plan.skipped.size());
        lines << head;
    } else {
        lines << QStringLiteral("%1完成：共 %2 个").arg(title).arg(m_total);
    }
    for (const QVariant &v : std::as_const(m_failures)) {
        const QVariantMap f = v.toMap();
        lines << QStringLiteral("· %1：%2").arg(f.value(QStringLiteral("name")).toString(),
                                                f.value(QStringLiteral("reason")).toString());
    }
    if (!m_unprocessed.isEmpty())
        lines << QStringLiteral("因取消未处理：") + m_unprocessed.join(QStringLiteral("、"));

    QVariantMap summary;
    summary.insert(QStringLiteral("kind"), m_kind);
    summary.insert(QStringLiteral("succeeded"), m_succeeded);
    summary.insert(QStringLiteral("failed"), int(m_failures.size()));
    summary.insert(QStringLiteral("failures"), m_failures);
    summary.insert(QStringLiteral("unprocessed"), m_unprocessed);
    summary.insert(QStringLiteral("cancelled"), m_cancelled);
    summary.insert(QStringLiteral("text"), lines.join(QLatin1Char('\n')));

    reset();
    emit stateChanged();
    emit finished(summary);
}

void BatchLauncher::reset() {
    m_timer.stop();
    m_kind.clear();
    m_autostart = false;
    m_cancelled = false;
    m_done = 0;
    m_total = 0;
    m_succeeded = 0;
    m_plan = {};
    m_next = 0;
    m_inFlight.clear();
    m_failures.clear();
    m_unprocessed.clear();
}
