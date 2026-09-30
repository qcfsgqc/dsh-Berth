#pragma once

#include <QHash>
#include <QList>
#include <QMenu>
#include <QObject>
#include <QString>
#include <QSystemTrayIcon>

#include <functional>

class AppController;
class QAction;
class QIcon;
struct Instance;

// 主托盘图标与菜单：每个泊位一个子菜单（"泊位名 — 状态"，五项按 TrayRules 启用），
// 下方为启动全部 / 停止全部（批量进行中禁用）。菜单动作直接调用 AppController 的同名方法，
// 与主窗口走同一条流水线（含端口冲突处理）。监听 InstanceModel 变化就地刷新，菜单打开时同样生效。
// 设置 trayInstanceIcons 开启时，每个"运行中"泊位（不含外部）另有一个独立托盘图标，左键打开/前置该泊位界面。
// 独立图标使用泊位自定义图标（内置字形或 icons/ 下的图片），缺失时退回默认图标。
class TrayController : public QObject {
    Q_OBJECT

public:
    struct Callbacks {
        std::function<void()> showMain;   // 显示并前置主窗口
        std::function<bool()> mainVisible; // 主窗口当前是否可见
    };

    TrayController(AppController *app, Callbacks callbacks, QObject *parent = nullptr);
    ~TrayController() override;

    // 托盘图标是否成功显示（系统托盘不可用时为 false）
    bool isVisible() const { return m_tray.isVisible(); }
    // 主托盘图标，Notifier 经它发送系统通知
    QSystemTrayIcon *trayIcon() { return &m_tray; }

private:
    struct Entry {
        QString id;
        QMenu *menu = nullptr;
        QAction *start = nullptr;
        QAction *stop = nullptr;
        QAction *restart = nullptr;
        QAction *openUi = nullptr;
        QAction *openBrowser = nullptr;
    };

    // 泊位集合或顺序变化时重建子菜单，否则只更新标题与可用状态
    void syncInstances();
    Entry makeEntry(const QString &id);
    void updateEntry(Entry &entry);
    void clearEntries();
    void updateBatchActions();
    // 按设置与泊位状态增删独立托盘图标，并刷新悬停提示
    void syncInstanceIcons();
    // 先 hide() 再删除，避免通知区域残留
    static void destroyIcon(QSystemTrayIcon *icon);
    // 泊位自定义图标 → QIcon；无图标或加载失败时返回默认图标
    QIcon iconFor(const Instance &item) const;

    AppController *m_app;
    Callbacks m_callbacks;
    // m_menu 须在 m_tray 之前声明：析构时先销毁托盘图标，再销毁它引用的菜单
    QMenu m_menu;
    QSystemTrayIcon m_tray;
    QAction *m_placeholder = nullptr; // "暂无泊位"
    QAction *m_instancesEnd = nullptr; // 泊位区结束分隔线，子菜单插在它之前
    QAction *m_startAll = nullptr;
    QAction *m_stopAll = nullptr;
    QList<Entry> m_entries;
    QHash<QString, QSystemTrayIcon *> m_instanceIcons; // 泊位 id → 独立托盘图标
};
