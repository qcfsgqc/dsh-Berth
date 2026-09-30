#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class AppController;
class QSystemTrayIcon;

// 系统通知与主窗口内提示：经主托盘 QSystemTrayIcon::showMessage 发送（Win10/11 显示为 toast）。
// - 崩溃：进程意外退出（含退出码）；自动重启达到上限时并入同一条（含已重启次数）
// - 就绪："启动中→运行中"每次启动只发一次；主窗口在前台且选中该泊位时改为窗口内提示（5 秒）
// - 更新：notifyUpdate 由 Update_Center 的升级任务结束时调用（32.2）
// 三类各受设置开关控制。点击通知恢复并前置主窗口并选中泊位（已删除时只提示）。
// 托盘不可见或不支持消息时，内容写入泊位日志（更新写入 berth.log）。
// 另外接管主窗口不可见时 notice 的托盘气泡（原在 TrayController）。
class Notifier : public QObject {
    Q_OBJECT

public:
    enum class Kind { Crash, Ready, Update, Plain };

    struct Callbacks {
        std::function<void()> showMain;                        // 恢复（含最小化）并前置主窗口
        std::function<bool()> mainVisible;                     // 主窗口可见且未最小化
        std::function<bool()> mainForeground;                  // 主窗口可见、未最小化且处于激活状态
        std::function<QString()> selectedId;                   // 主窗口当前选中的泊位 id
        std::function<void(const QString &)> selectInstance;   // 在主窗口中选中泊位
        std::function<void(const QString &, int)> showInWindow; // 主窗口内提示（文本，毫秒）
    };

    Notifier(AppController *app, QSystemTrayIcon *tray, Callbacks callbacks, QObject *parent = nullptr);

    // 发送一条通知；kind 对应的开关关闭时直接返回（Plain 不受开关控制）
    void notify(Kind kind, const QString &instanceId, const QString &title, const QString &body);
    // 更新通知：item 为 dsh 或插件名；成功时 detail 为新版本号，失败时为原因摘要
    Q_INVOKABLE void notifyUpdate(const QString &item, bool ok, const QString &detail);

private:
    struct PendingCrash {
        bool exited = false;
        int exitCode = 0;
        int gaveUpAttempts = -1; // >= 0 表示已达自动重启上限
    };

    bool enabled(Kind kind) const;
    bool canShowMessage() const;
    void handleStatus(const QString &id, const QString &status);
    void queueCrash(const QString &id);
    void flushCrashes();
    void flushNotices();
    void onMessageClicked();
    // 通知发不出去时写日志：instanceId 非空写泊位日志，否则写 berth.log
    void writeFallback(const QString &instanceId, const QString &title, const QString &body);

    AppController *m_app;
    QSystemTrayIcon *m_tray;
    Callbacks m_callbacks;
    QHash<QString, QString> m_lastStatus;     // 泊位 id → 上一次状态
    QHash<QString, PendingCrash> m_crashes;   // 本轮事件内待合并的崩溃事件
    QStringList m_pendingNotices;             // 延迟到下一轮事件循环的 notice 气泡
    bool m_crashFlushQueued = false;
    bool m_noticeFlushQueued = false;
    QString m_lastMessageId;                  // 最近一条托盘消息对应的泊位 id（空表示无）
    Kind m_lastMessageKind = Kind::Plain;
};
