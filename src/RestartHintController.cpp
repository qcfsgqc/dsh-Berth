#include "RestartHintController.h"

#include "core/AffectedSet.h"

#include <utility>

RestartHintController::RestartHintController(Ops ops, QObject *parent)
    : QObject(parent), m_ops(std::move(ops)) {}

bool RestartHintController::isActive(const QString &status) {
    return status == QLatin1String("starting") || status == QLatin1String("running");
}

QStringList RestartHintController::names() const {
    QStringList out;
    for (const QString &id : m_ids)
        out.append(m_ops.nameOf(id));
    return out;
}

QVariantMap RestartHintController::pending() const {
    QVariantMap out;
    for (const QString &id : m_pending)
        out.insert(id, true);
    return out;
}

void RestartHintController::raise(const QString &home, const QString &profile, const QStringList &ids) {
    Q_UNUSED(home);
    Q_UNUSED(profile);
    if (ids.isEmpty())
        return;
    // 正在显示时合并进当前列表，不另起第二个提示
    m_ids = m_visible ? AffectedSet::merge(m_ids, ids) : AffectedSet::merge({}, ids);
    m_visible = true;
    emit hintChanged();
}

void RestartHintController::hide() {
    m_visible = false;
    m_ids.clear();
    emit hintChanged();
}

void RestartHintController::addPending(const QString &id) {
    if (id.isEmpty() || m_pending.contains(id))
        return;
    m_pending.insert(id);
    emit pendingChanged();
}

void RestartHintController::later() {
    if (!m_visible)
        return;
    for (const QString &id : std::as_const(m_ids))
        addPending(id);
    hide();
}

void RestartHintController::restartAll() {
    if (!m_visible)
        return;
    if (m_running) {
        // 上一轮尚未结束：本次列表先挂"待重启"，避免丢失
        later();
        return;
    }
    m_queue = m_ids;
    m_done = 0;
    m_succeeded = 0;
    m_skipped = 0;
    m_failures.clear();
    m_current.clear();
    m_running = true;
    hide();
    emit progressChanged();
    next();
}

void RestartHintController::next() {
    while (m_running && m_current.isEmpty() && m_done < m_queue.size()) {
        const QString id = m_queue.at(m_done);
        // 此时已不在启动中/运行中的泊位跳过
        if (!isActive(m_ops.statusOf(id))) {
            ++m_skipped;
            ++m_done;
            continue;
        }
        m_current = id;
        emit progressChanged();
        QString reason;
        // restart 内部会先停止（同步发出 stopped 状态），期间不按失败处理
        m_launching = true;
        const bool ok = m_ops.restart(id, &reason);
        m_launching = false;
        if (!ok) {
            markFailed(reason.isEmpty() ? QStringLiteral("启动被拒绝") : reason);
            continue;
        }
        const QString s = m_ops.statusOf(id);
        if (s == QLatin1String("running") || s == QLatin1String("external"))
            markSucceeded();
        else if (!isActive(s))
            markFailed(QStringLiteral("启动失败"));
        else
            return; // 等待进入运行中或失败
    }
    if (m_running && m_current.isEmpty() && m_done >= m_queue.size())
        finish();
}

void RestartHintController::markSucceeded() {
    m_current.clear();
    ++m_succeeded;
    ++m_done;
    emit progressChanged();
}

void RestartHintController::markFailed(const QString &reason) {
    QVariantMap entry;
    entry.insert(QStringLiteral("name"), m_ops.nameOf(m_current));
    entry.insert(QStringLiteral("reason"), reason);
    m_failures.append(entry);
    // 失败泊位显示"待重启"标记
    addPending(m_current);
    m_current.clear();
    ++m_done;
    emit progressChanged();
}

void RestartHintController::onStatus(const QString &id, const QString &status, const QString &error) {
    // 进入运行中即移除"待重启"标记；被停止时保留
    if (status == QLatin1String("running") && m_pending.remove(id))
        emit pendingChanged();
    if (!m_running || m_launching || id != m_current)
        return;
    if (status == QLatin1String("running") || status == QLatin1String("external"))
        markSucceeded();
    else if (!isActive(status))
        markFailed(error.isEmpty() ? QStringLiteral("进入运行中前退出") : error);
    else
        return;
    next();
}

void RestartHintController::onRemoved(const QString &id) {
    if (m_pending.remove(id))
        emit pendingChanged();
    if (m_visible && m_ids.removeAll(id) > 0) {
        if (m_ids.isEmpty())
            hide();
        else
            emit hintChanged();
    }
    if (m_running && id == m_current) {
        markFailed(QStringLiteral("泊位已删除"));
        next();
    }
}

void RestartHintController::finish() {
    QStringList lines;
    QString head = QStringLiteral("全部重启完成：成功 %1，失败 %2").arg(m_succeeded).arg(m_failures.size());
    if (m_skipped > 0)
        head += QStringLiteral("，跳过 %1（已不在运行）").arg(m_skipped);
    lines << head;
    for (const QVariant &v : std::as_const(m_failures)) {
        const QVariantMap f = v.toMap();
        lines << QStringLiteral("· %1：%2").arg(f.value(QStringLiteral("name")).toString(),
                                                f.value(QStringLiteral("reason")).toString());
    }

    QVariantMap summary;
    summary.insert(QStringLiteral("succeeded"), m_succeeded);
    summary.insert(QStringLiteral("failed"), int(m_failures.size()));
    summary.insert(QStringLiteral("skipped"), m_skipped);
    summary.insert(QStringLiteral("failures"), m_failures);
    summary.insert(QStringLiteral("text"), lines.join(QLatin1Char('\n')));

    m_running = false;
    m_queue.clear();
    m_done = 0;
    m_failures.clear();
    emit progressChanged();
    emit finished(summary);
}
