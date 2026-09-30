#include "AppController.h"
#include "Notifier.h"
#include "TrayController.h"
#include "core/RunValue.h"

#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
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
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/assets/berth-logo.png")));

    // 开机自启带 --autostart；startMinimized 为真时只显示托盘，其余情况显示主窗口
    const bool autostartLaunch =
        QCoreApplication::arguments().contains(QLatin1String(RunValue::kAutostartArg), Qt::CaseInsensitive);

    AppController controller;
    const bool startHidden = autostartLaunch && controller.settings()->startMinimized();
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("berth"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("berthStartHidden"), startHidden);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Berth", "Main");

    TrayController::Callbacks callbacks;
    callbacks.showMain = [&engine]() {
        const auto roots = engine.rootObjects();
        if (roots.isEmpty())
            return;
        QMetaObject::invokeMethod(roots.first(), "showWindow");
    };
    callbacks.mainVisible = [&engine]() {
        const auto roots = engine.rootObjects();
        if (roots.isEmpty())
            return false;
        const auto *window = qobject_cast<QQuickWindow *>(roots.first());
        return window && window->isVisible() && window->visibility() != QWindow::Minimized;
    };
    TrayController tray(&controller, callbacks);

    const auto mainWindow = [&engine]() -> QQuickWindow * {
        const auto roots = engine.rootObjects();
        return roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
    };
    Notifier::Callbacks notifierCallbacks;
    notifierCallbacks.showMain = callbacks.showMain;
    notifierCallbacks.mainVisible = callbacks.mainVisible;
    notifierCallbacks.mainForeground = [mainWindow]() {
        const QQuickWindow *window = mainWindow();
        return window && window->isVisible() && window->visibility() != QWindow::Minimized
               && window->isActive();
    };
    notifierCallbacks.selectedId = [mainWindow]() {
        QQuickWindow *window = mainWindow();
        QVariant id;
        if (window)
            QMetaObject::invokeMethod(window, "selectedInstanceId", Q_RETURN_ARG(QVariant, id));
        return id.toString();
    };
    notifierCallbacks.selectInstance = [mainWindow](const QString &id) {
        if (QQuickWindow *window = mainWindow())
            QMetaObject::invokeMethod(window, "selectInstance", Q_ARG(QVariant, id));
    };
    notifierCallbacks.showInWindow = [mainWindow](const QString &message, int ms) {
        if (QQuickWindow *window = mainWindow())
            QMetaObject::invokeMethod(window, "showToast", Q_ARG(QVariant, message), Q_ARG(QVariant, ms));
    };
    Notifier notifier(&controller, tray.trayIcon(), std::move(notifierCallbacks));
    // 需求 19.3：每个升级任务结束发更新通知（成功附新版本号，失败附原因）
    QObject::connect(controller.updates(), &UpdateCenter::upgradeFinished, &notifier,
                     [&notifier](const QString &, const QString &label, bool ok, const QString &detail,
                                 const QString &newVersion) {
                         notifier.notifyUpdate(label, ok, ok && !newVersion.isEmpty() ? newVersion : detail);
                     });
    // 托盘没能显示时，关闭窗口就直接退出，避免进程隐身残留
    if (!tray.isVisible()) {
        QGuiApplication::setQuitOnLastWindowClosed(true);
        // 没有托盘可点时不能隐藏启动，改为显示主窗口
        if (startHidden) {
            const auto roots = engine.rootObjects();
            if (!roots.isEmpty())
                QMetaObject::invokeMethod(roots.first(), "showWindow");
        }
    }

    return app.exec();
}
