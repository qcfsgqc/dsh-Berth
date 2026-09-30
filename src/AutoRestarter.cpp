#include "AutoRestarter.h"

#include "AppController.h"
#include "Supervisor.h"

#include <QDateTime>
#include <QTimer>

#include <algorithm>
#include <cmath>

AutoRestarter::AutoRestarter(AppController *controller, Supervisor *supervisor, QObject *parent)
    : QObject(parent), m_controller(controller) {
    connect(supervisor, &Supervisor::crashed, this,
            [this](const QString &id, int) { handleCrash(id); });
    connect(supervisor, &Supervisor::statusChanged, this,
            [this](const QString &id, const QString &status, qint64, const QString &) {
                handleStatus(id, status);
            });
}

AutoRestarter::~AutoRestarter() = default;

AutoRestarter::Entry &AutoRestarter::entry(const QString &id) {
    auto it = m_entries.find(id);
    if (it != m_entries.end())
        return it.value();

    Entry e;
    e.restart = new QTimer(this);
    e.restart->setSingleShot(true);
    connect(e.restart, &QTimer::timeout, this, [this, id]() { fireRestart(id); });

    e.tick = new QTimer(this);
    e.tick->setInterval(1000);
    connect(e.tick, &QTimer::timeout, this, [this, id]() { emitTick(id); });

    e.reset = new QTimer(this);
    e.reset->setSingleShot(true);
    connect(e.reset, &QTimer::timeout, this, [this, id]() {
        auto found = m_entries.find(id);
        if (found != m_entries.end())
            found->state.onRunningFor(RestartState::kResetAfterSec);
    });
    return m_entries.insert(id, e).value();
}

void AutoRestarter::stopPending(Entry &e, const QString &id) {
    const bool hadCountdown = e.restart->isActive() || e.tick->isActive();
    e.restart->stop();
    e.tick->stop();
    e.deadlineMs = 0;
    if (hadCountdown)
        emit countdown(id, 0, e.state.n, e.state.N);
}

void AutoRestarter::cancel(const QString &id) {
    auto it = m_entries.find(id);
    if (it == m_entries.end())
        return;
    stopPending(it.value(), id);
}

void AutoRestarter::onManualStop(const QString &id) {
    Entry &e = entry(id);
    stopPending(e, id);
    e.reset->stop();
    e.state.onManualStop();
}

void AutoRestarter::onManualStart(const QString &id) {
    Entry &e = entry(id);
    stopPending(e, id);
    e.state.onManualStart();
}

void AutoRestarter::onToggle(const QString &id, bool on) {
    Entry &e = entry(id);
    if (!on)
        stopPending(e, id);
    e.state.onToggle(on);
}

void AutoRestarter::forget(const QString &id) {
    auto it = m_entries.find(id);
    if (it == m_entries.end())
        return;
    stopPending(it.value(), id);
    delete it->restart;
    delete it->tick;
    delete it->reset;
    m_entries.erase(it);
}

void AutoRestarter::handleCrash(const QString &id) {
    const Instance item = m_controller->instances()->item(id);
    if (item.id.isEmpty())
        return;

    Entry &e = entry(id);
    e.reset->stop();
    stopPending(e, id);

    // 每次 crash 时读取最新配置：设置页的间隔/上限与泊位开关
    const AutoRestartSettings &cfg = m_controller->settings()->data().autoRestart;
    e.state.configure(cfg.maxAttempts, cfg.baseSec, cfg.maxSec);
    if (e.state.enabled != item.autoRestart)
        e.state.onToggle(item.autoRestart);

    const RestartState::Action action = e.state.onCrash();
    switch (action.kind) {
    case RestartState::Action::None:
        return;
    case RestartState::Action::GiveUp:
        emit gaveUp(id, e.state.n);
        return;
    case RestartState::Action::Schedule:
        e.deadlineMs = QDateTime::currentMSecsSinceEpoch() + action.ms;
        e.restart->start(action.ms);
        e.tick->start();
        emitTick(id);
        return;
    }
}

void AutoRestarter::handleStatus(const QString &id, const QString &status) {
    Entry &e = entry(id);
    if (status == QLatin1String("external")) {
        // 外部运行期间不安排任何自动重启
        e.state.onExternal(true);
        stopPending(e, id);
        e.reset->stop();
        return;
    }
    if (e.state.external)
        e.state.onExternal(false);

    if (status == QLatin1String("running")) {
        // 自最近一次进入"运行中"起连续运行满 120 秒后清零
        e.reset->start(RestartState::kResetAfterSec * 1000);
        return;
    }
    // 离开"运行中"：连续运行计时作废（退避中的重启计划不受影响）
    e.reset->stop();
}

void AutoRestarter::fireRestart(const QString &id) {
    {
        Entry &e = entry(id);
        // 重启开始时移除倒计时
        stopPending(e, id);
    }
    // launch 会同步触发 statusChanged，期间不持有 m_entries 的引用
    const LaunchResult result = m_controller->launch(id, LaunchReason::AutoRestart);
    // 启动命令失败（端口占用、进程创建失败等）也算一次 crash
    if (!result.ok)
        handleCrash(id);
}

void AutoRestarter::emitTick(const QString &id) {
    auto it = m_entries.find(id);
    if (it == m_entries.end() || !it->restart->isActive())
        return;
    const qint64 remaining = it->deadlineMs - QDateTime::currentMSecsSinceEpoch();
    // 剩余秒数向上取整；等待中至少显示 1 秒
    const int sec = std::max(1, static_cast<int>(std::ceil(remaining / 1000.0)));
    emit countdown(id, sec, it->state.n, it->state.N);
}
