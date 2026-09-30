#include "TrayController.h"

#include "AppController.h"
#include "Settings.h"
#include "core/TrayRules.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QUrl>

namespace {

QIcon defaultIcon() { return QIcon(QStringLiteral(":/assets/berth-logo.png")); }

// 内置图标集，与 qml/BerthIcon.qml 的 builtins 保持一致
bool builtinGlyph(const QString &name, QString *glyph, QColor *color)
{
    struct B { const char *name; char16_t glyph; const char *color; };
    static const B table[] = {
        {"star", u'\u2605', "#e3a008"},    {"bolt", u'\u26A1', "#ca5010"},
        {"cloud", u'\u2601', "#0078d4"},   {"heart", u'\u2665', "#c42b1c"},
        {"gear", u'\u2699', "#555555"},    {"flag", u'\u2691', "#107c10"},
        {"diamond", u'\u25C6', "#8764b8"}, {"dot", u'\u25CF', "#038387"},
    };
    for (const B &b : table) {
        if (name == QLatin1String(b.name)) {
            *glyph = QString(QChar(b.glyph));
            *color = QColor(QLatin1String(b.color));
            return true;
        }
    }
    return false;
}

QIcon renderGlyph(const QString &glyph, const QColor &color)
{
    QIcon icon;
    for (int size : {16, 24, 32, 48}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        QFont font = QApplication::font();
        font.setPixelSize(qRound(size * 0.85));
        p.setFont(font);
        p.setPen(color);
        p.drawText(pm.rect(), Qt::AlignCenter, glyph);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace

// 泊位自定义图标：内置字形绘制成位图，文件从 icons/ 读取；无图标、未知内置名、
// 文件缺失或无法解析时退回默认图标（需求 9.7、17.7）
QIcon TrayController::iconFor(const Instance &item) const
{
    if (item.icon.kind == QLatin1String("builtin")) {
        QString glyph;
        QColor color;
        if (builtinGlyph(item.icon.value, &glyph, &color))
            return renderGlyph(glyph, color);
    } else if (item.icon.kind == QLatin1String("file")) {
        const QString url = m_app->iconFileUrl(item.icon.value);
        if (!url.isEmpty()) {
            const QPixmap pm(QUrl(url).toLocalFile());
            if (!pm.isNull())
                return QIcon(pm);
        }
    }
    return defaultIcon();
}

TrayController::TrayController(AppController *app, Callbacks callbacks, QObject *parent)
    : QObject(parent), m_app(app), m_callbacks(std::move(callbacks))
{
    QAction *showAction = m_menu.addAction(QStringLiteral("打开 DSH Berth"));
    m_menu.addSeparator();
    m_placeholder = m_menu.addAction(QStringLiteral("暂无泊位"));
    m_placeholder->setEnabled(false);
    m_instancesEnd = m_menu.addSeparator();
    m_startAll = m_menu.addAction(QStringLiteral("启动全部"));
    m_stopAll = m_menu.addAction(QStringLiteral("停止全部"));
    m_menu.addSeparator();
    QAction *quitAction = m_menu.addAction(QStringLiteral("退出"));

    connect(showAction, &QAction::triggered, this, [this]() {
        if (m_callbacks.showMain)
            m_callbacks.showMain();
    });
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
    connect(m_startAll, &QAction::triggered, this, [this]() { m_app->batch()->startAll(); });
    connect(m_stopAll, &QAction::triggered, this, [this]() { m_app->batch()->stopAll(); });

    // 泊位增删改、状态变化都会经模型信号到达；菜单打开时同样就地刷新
    InstanceModel *model = m_app->instances();
    connect(model, &QAbstractItemModel::dataChanged, this, &TrayController::syncInstances);
    connect(model, &QAbstractItemModel::rowsInserted, this, &TrayController::syncInstances);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &TrayController::syncInstances);
    connect(model, &QAbstractItemModel::rowsMoved, this, &TrayController::syncInstances);
    connect(model, &QAbstractItemModel::modelReset, this, &TrayController::syncInstances);
    connect(model, &QAbstractItemModel::layoutChanged, this, &TrayController::syncInstances);
    connect(&m_menu, &QMenu::aboutToShow, this, &TrayController::syncInstances);

    BatchLauncher *batch = m_app->batch();
    connect(batch, &BatchLauncher::stateChanged, this, &TrayController::updateBatchActions);
    connect(batch, &BatchLauncher::allCountChanged, this, &TrayController::updateBatchActions);

    // 开关独立托盘图标时立即增删
    connect(m_app->settings(), &Settings::trayInstanceIconsChanged, this,
            &TrayController::syncInstanceIcons);

    // 端口冲突对话框在主窗口里：从托盘启动遇到冲突时先把主窗口带出来
    connect(m_app, &AppController::portConflict, this, [this]() {
        if (m_callbacks.showMain)
            m_callbacks.showMain();
    });
    // 主窗口不可见时 notice 改用托盘气泡：由 Notifier 统一经 m_tray 发出（与系统通知去重、共用点击处理）

    // 托盘图标必须设置 icon，否则 Qt 拒绝显示
    m_tray.setIcon(QIcon(QStringLiteral(":/assets/berth-logo.png")));
    m_tray.setToolTip(QStringLiteral("DSH Berth"));
    m_tray.setContextMenu(&m_menu);
    connect(&m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick)
                    return;
                if (m_callbacks.showMain)
                    m_callbacks.showMain();
            });

    syncInstances();
    if (QSystemTrayIcon::isSystemTrayAvailable())
        m_tray.show();
}

TrayController::~TrayController()
{
    // 先隐藏再销毁，避免通知区域残留图标
    m_tray.hide();
    m_tray.setContextMenu(nullptr);
    for (QSystemTrayIcon *icon : std::as_const(m_instanceIcons))
        destroyIcon(icon);
    m_instanceIcons.clear();
    clearEntries();
}

void TrayController::syncInstances()
{
    const QList<Instance> items = m_app->instances()->items();
    bool sameIds = items.size() == m_entries.size();
    for (int i = 0; sameIds && i < items.size(); ++i)
        sameIds = items.at(i).id == m_entries.at(i).id;

    if (!sameIds) {
        clearEntries();
        for (const Instance &item : items)
            m_entries.append(makeEntry(item.id));
    }
    for (Entry &entry : m_entries)
        updateEntry(entry);

    m_placeholder->setVisible(m_entries.isEmpty());
    updateBatchActions();
    syncInstanceIcons();
}

void TrayController::syncInstanceIcons()
{
    const bool enabled = m_app->settings()->trayInstanceIcons()
                         && QSystemTrayIcon::isSystemTrayAvailable();

    // 只有 Berth 自己托管的"运行中"才显示；"运行中（外部）"等其它状态都移除
    QHash<QString, Instance> running;
    if (enabled) {
        for (const Instance &item : m_app->instances()->items()) {
            if (item.status == QLatin1String("running"))
                running.insert(item.id, item);
        }
    }

    for (auto it = m_instanceIcons.begin(); it != m_instanceIcons.end();) {
        if (running.contains(it.key())) {
            ++it;
        } else {
            destroyIcon(it.value());
            it = m_instanceIcons.erase(it);
        }
    }

    for (auto it = running.cbegin(); it != running.cend(); ++it) {
        const QString id = it.key();
        QSystemTrayIcon *icon = m_instanceIcons.value(id);
        if (!icon) {
            icon = new QSystemTrayIcon(this);
            connect(icon, &QSystemTrayIcon::activated, this,
                    [this, id](QSystemTrayIcon::ActivationReason reason) {
                        // 左键单击：打开界面；已打开时 WebWindow 还原并前置，不另开窗口
                        if (reason == QSystemTrayIcon::Trigger)
                            m_app->openUi(id);
                    });
            m_instanceIcons.insert(id, icon);
        }
        // 图标设置变化时才重新生成（状态刷新很频繁）
        const QString iconKey = it.value().icon.kind + QLatin1Char('|') + it.value().icon.value;
        if (icon->property("berthIconKey").toString() != iconKey || icon->icon().isNull()) {
            icon->setIcon(iconFor(it.value()));
            icon->setProperty("berthIconKey", iconKey);
        }
        const QString tip = QStringLiteral("%1 · 端口 %2").arg(it.value().name).arg(it.value().port);
        if (icon->toolTip() != tip)
            icon->setToolTip(tip);
        if (!icon->isVisible())
            icon->show();
    }
}

void TrayController::destroyIcon(QSystemTrayIcon *icon)
{
    icon->hide();
    // 可能正处于该图标的 activated 回调链中，延迟删除
    icon->deleteLater();
}

TrayController::Entry TrayController::makeEntry(const QString &id)
{
    Entry entry;
    entry.id = id;
    entry.menu = new QMenu(&m_menu);
    entry.start = entry.menu->addAction(QStringLiteral("启动"));
    entry.stop = entry.menu->addAction(QStringLiteral("停止"));
    entry.restart = entry.menu->addAction(QStringLiteral("重启"));
    entry.openUi = entry.menu->addAction(QStringLiteral("打开界面"));
    entry.openBrowser = entry.menu->addAction(QStringLiteral("在浏览器中打开"));

    // 与主窗口调用同一组 AppController 方法（启动/重启经 launch 流水线与端口冲突处理）
    connect(entry.start, &QAction::triggered, this, [this, id]() { m_app->startInstance(id); });
    connect(entry.stop, &QAction::triggered, this, [this, id]() { m_app->stopInstance(id); });
    connect(entry.restart, &QAction::triggered, this, [this, id]() { m_app->restartInstance(id); });
    connect(entry.openUi, &QAction::triggered, this, [this, id]() { m_app->openUi(id); });
    connect(entry.openBrowser, &QAction::triggered, this,
            [this, id]() { m_app->openInBrowser(id); });

    m_menu.insertMenu(m_instancesEnd, entry.menu);
    return entry;
}

void TrayController::updateEntry(Entry &entry)
{
    const Instance item = m_app->instances()->item(entry.id);
    entry.menu->setTitle(QStringLiteral("%1 — %2").arg(item.name,
                                                        InstanceModel::statusText(item.status)));
    const TrayRules::MenuFlags flags = TrayRules::flagsFor(item.status);
    entry.start->setEnabled(flags.start);
    entry.stop->setEnabled(flags.stop);
    entry.restart->setEnabled(flags.restart);
    entry.openUi->setEnabled(flags.openUi);
    entry.openBrowser->setEnabled(flags.openBrowser);
}

void TrayController::clearEntries()
{
    for (const Entry &entry : std::as_const(m_entries)) {
        m_menu.removeAction(entry.menu->menuAction());
        // 子菜单可能正处于打开状态，延迟删除
        entry.menu->deleteLater();
    }
    m_entries.clear();
}

void TrayController::updateBatchActions()
{
    const BatchLauncher *batch = m_app->batch();
    const bool enabled = !m_entries.isEmpty() && !batch->busy();
    m_startAll->setEnabled(enabled);
    m_stopAll->setEnabled(enabled);
}
