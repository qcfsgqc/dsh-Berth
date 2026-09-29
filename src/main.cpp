#include "AppController.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("dsh-Berth"));
    QGuiApplication::setOrganizationName(QStringLiteral("dsh-Berth"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("DSH Berth"));
    QGuiApplication::setApplicationVersion(QStringLiteral(BERTH_VERSION));
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

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
    QSystemTrayIcon tray;
    tray.setToolTip(QStringLiteral("DSH Berth"));
    tray.setContextMenu(&trayMenu);
    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray.show();

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
