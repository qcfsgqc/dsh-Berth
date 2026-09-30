#include "AppController.h"

#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QStyle>
#include <QtWebView/QtWebView>

int main(int argc, char *argv[]) {
    // QtWebView 必须在创建 QApplication 之前初始化（WebView2 后端）
    QtWebView::initialize();
    // QSystemTrayIcon / QMenu 属于 QtWidgets，必须用 QApplication
    QApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("dsh-Berth"));
    QGuiApplication::setOrganizationName(QStringLiteral("dsh-Berth"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("DSH Berth"));
    QGuiApplication::setApplicationVersion(QStringLiteral(BERTH_VERSION));
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Windows"));

    AppController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("berth"), &controller);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Berth", "Main");

    QMenu trayMenu;
    QAction *showAction = trayMenu.addAction(QStringLiteral("打开 DSH Berth"));
    QAction *quitAction = trayMenu.addAction(QStringLiteral("退出"));
    // 托盘图标必须设置 icon，否则 Qt 拒绝显示，关窗后就再也没有退出入口
    const QIcon appIcon(QStringLiteral(":/assets/berth-logo.png"));
    QApplication::setWindowIcon(appIcon);
    QSystemTrayIcon tray(appIcon);
    tray.setToolTip(QStringLiteral("DSH Berth"));
    tray.setContextMenu(&trayMenu);
    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray.show();
    // 托盘没能显示时，关闭窗口就直接退出，避免进程隐身残留
    if (!tray.isVisible())
        QGuiApplication::setQuitOnLastWindowClosed(true);

    QObject::connect(showAction, &QAction::triggered, &engine, [&engine]() {
        const auto roots = engine.rootObjects();
        if (roots.isEmpty())
            return;
        QMetaObject::invokeMethod(roots.first(), "showWindow");
    });
    QObject::connect(quitAction, &QAction::triggered, &app, &QGuiApplication::quit);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &engine,
                     [&engine](QSystemTrayIcon::ActivationReason reason) {
                         if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick)
                             return;
                         const auto roots = engine.rootObjects();
                         if (roots.isEmpty())
                             return;
                         QMetaObject::invokeMethod(roots.first(), "showWindow");
                     });

    return app.exec();
}
