#include "Notifier.h"

#include "AppController.h"
#include "Settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSystemTrayIcon>
#include <QTimer>

#include <utility>

namespace {
constexpr int kReadyInWindowMs = 5000;
constexpr int kGoneHintMs = 2800;
const QString kAppTitle = QStringLiteral("DSH Berth");

void appendLine(const QString &path, const QString &message) {
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;
    const QString line = QStringLiteral("[Berth %1] %2\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), message);
    file.write(line.toUtf8());
}
} // namespace

Notifier::Notifier(AppController *app, QSystemTrayIcon *tray, Callbacks callbacks, QObject *parent)
    : QObject(parent), m_app(app), m_tray(tray), m_callbacks(std::move(callbacks))
{
    for (const Instance &item : m_app->instances()->items())
        m_lastStatus.insert(item.id, item.status);

    connect(m_app, &AppController::instanceStatusChanged, this, &Notifier::handleStatus);
    connect(m_app, &AppController::instanceCrashed, this, [this](const QString &id, int exitCode) {
        PendingCrash &p = m_crashes[id];
        p.exited = true;
        p.exitCode = exitCode;
        queueCrash(id);
    });
    connect(m_app, &AppController::restartGaveUp, this, [this](const QString &id, int attempts) {
        m_crashes[id].gaveUpAttempts = attempts;
        queueCrash(id);
    });
    // 启动前自检隔离了插件：每次启动 1 条汇总通知（不受三类开关控制）
    connect(m_app, &AppController::pluginsQuarantined, this, [this](const QString &id, const QStringList &summary) {
        const QString name = m_app->instance(id).value(QStringLiteral("name")).toString();
        notify(Kind::Plain, id, QStringLiteral("%1：%2 个插件已被隔离").arg(name.isEmpty() ? id : name).arg(summary.size()),
               summary.join(QStringLiteral("、")));
    });
    // 主窗口不可见时 notice 走托盘气泡；延迟一轮事件循环，同一轮里出现崩溃通知时丢弃（避免重复）
    connect(m_app, &AppController::notice, this, [this](const QString &message) {
        m_pendingNotices.append(message);
        if (!m_noticeFlushQueued) {
            m_noticeFlushQueued = true;
            QTimer::singleShot(0, this, &Notifier::flushNotices);
        }
    });
    if (m_tray)
        connect(m_tray, &QSystemTrayIcon::messageClicked, this, &Notifier::onMessageClicked);
}

bool Notifier::enabled(Kind kind) const {
    const Settings *s = m_app->settings();
    switch (kind) {
    case Kind::Crash:
        return s->notifyCrash();
    case Kind::Ready:
        return s->notifyReady();
    case Kind::Update:
        return s->notifyUpdate();
    case Kind::Plain:
        return true;
    }
    return true;
}

bool Notifier::canShowMessage() const {
    return m_tray && m_tray->isVisible() && QSystemTrayIcon::supportsMessages();
}

void Notifier::notify(Kind kind, const QString &instanceId, const QString &title, const QString &body) {
    if (!enabled(kind))
        return;
    if (!canShowMessage()) {
        writeFallback(instanceId, title, body);
        return;
    }
    m_lastMessageId = instanceId;
    m_lastMessageKind = kind;
    const QSystemTrayIcon::MessageIcon icon =
        kind == Kind::Crash ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information;
    m_tray->showMessage(title, body, icon);
}

void Notifier::notifyUpdate(const QString &item, bool ok, const QString &detail) {
    const QString body = ok ? QStringLiteral("%1 已升级到 %2").arg(item, detail)
                            : QStringLiteral("%1 升级失败：%2").arg(item, detail);
    notify(Kind::Update, QString(), QStringLiteral("升级完成"), body);
}

void Notifier::handleStatus(const QString &id, const QString &status) {
    const QString prev = m_lastStatus.value(id);
    m_lastStatus.insert(id, status);
    // 只在"启动中→运行中"时发，同一次启动只会经过一次
    if (prev != QLatin1String("starting") || status != QLatin1String("running"))
        return;
    if (!enabled(Kind::Ready))
        return;
    const Instance item = m_app->instances()->item(id);
    if (item.id.isEmpty())
        return;
    const QString body = QStringLiteral("%1 已就绪，端口 %2").arg(item.name).arg(item.port);

    const bool foreground = m_callbacks.mainForeground && m_callbacks.mainForeground();
    const bool selected = m_callbacks.selectedId && m_callbacks.selectedId() == id;
    if (foreground && selected && m_callbacks.showInWindow) {
        m_callbacks.showInWindow(body, kReadyInWindowMs);
        return;
    }
    notify(Kind::Ready, id, QStringLiteral("泊位已就绪"), body);
}

void Notifier::queueCrash(const QString &) {
    // 同一轮里由崩溃引出的 notice（"进程意外退出"等）不再单独弹气泡
    m_pendingNotices.clear();
    // AutoRestarter 与本类都挂在 crashed 上，先后顺序不定：合并到下一轮事件循环统一发送
    if (!m_crashFlushQueued) {
        m_crashFlushQueued = true;
        QTimer::singleShot(0, this, &Notifier::flushCrashes);
    }
}

void Notifier::flushCrashes() {
    m_crashFlushQueued = false;
    const QHash<QString, PendingCrash> crashes = std::exchange(m_crashes, {});
    for (auto it = crashes.cbegin(); it != crashes.cend(); ++it) {
        const Instance item = m_app->instances()->item(it.key());
        if (item.id.isEmpty())
            continue;
        const PendingCrash &p = it.value();
        QString body;
        if (p.exited)
            body = QStringLiteral("%1 意外退出（退出码 %2）").arg(item.name).arg(p.exitCode);
        else
            body = QStringLiteral("%1 启动失败").arg(item.name);
        if (p.gaveUpAttempts >= 0)
            body += QStringLiteral("；已自动重启 %1 次，不再重启").arg(p.gaveUpAttempts);
        notify(Kind::Crash, item.id, QStringLiteral("泊位崩溃"), body);
    }
}

void Notifier::flushNotices() {
    m_noticeFlushQueued = false;
    const QStringList messages = std::exchange(m_pendingNotices, {});
    if (messages.isEmpty())
        return;
    // 主窗口可见时由窗口内 toast 显示
    if (m_callbacks.mainVisible && m_callbacks.mainVisible())
        return;
    if (!canShowMessage())
        return;
    m_lastMessageId.clear();
    m_lastMessageKind = Kind::Plain;
    m_tray->showMessage(kAppTitle, messages.join(QLatin1Char('\n')));
}

void Notifier::onMessageClicked() {
    if (m_callbacks.showMain)
        m_callbacks.showMain();
    if (m_lastMessageKind != Kind::Crash && m_lastMessageKind != Kind::Ready)
        return;
    const QString id = m_lastMessageId;
    if (id.isEmpty())
        return;
    if (m_app->instances()->item(id).id.isEmpty()) {
        // 泊位已删除：不改变选中项，只提示
        if (m_callbacks.showInWindow)
            m_callbacks.showInWindow(QStringLiteral("该泊位已不存在"), kGoneHintMs);
        return;
    }
    if (m_callbacks.selectInstance)
        m_callbacks.selectInstance(id);
}

void Notifier::writeFallback(const QString &instanceId, const QString &title, const QString &body) {
    const QString message = QStringLiteral("通知（系统通知不可用）：%1 — %2").arg(title, body);
    QString path;
    if (!instanceId.isEmpty())
        path = m_app->instances()->item(instanceId).logPath;
    if (path.isEmpty())
        path = QDir(m_app->settings()->dataDir()).filePath(QStringLiteral("berth.log"));
    appendLine(path, message);
}
