import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts
import QtWebEngine

// 单个泊位的 Web 界面窗口，内嵌 Qt WebEngine 加载 dsh web（带 token 的地址）
ApplicationWindow {
    id: webWin
    property string instanceId: ""
    property string instanceName: ""
    // 最近一次请求加载的地址；相同地址再次请求时只激活窗口，不打断当前页面
    property string requestedUrl: ""

    width: 1200
    height: 800
    minimumWidth: 640
    minimumHeight: 420
    color: "#ffffff"
    title: (view.title.length ? view.title : "dsh web") + " · " + instanceName + " · DSH Berth"

    function load(url) {
        if (url !== requestedUrl) {
            requestedUrl = url
            view.url = url
        }
        showWindow()
    }

    function showWindow() {
        visible = true
        raise()
        requestActivate()
    }

    header: Rectangle {
        implicitHeight: 40
        color: "#ffffff"
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#e5e5e5" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 6
            Button { text: "后退"; enabled: view.canGoBack; onClicked: view.goBack() }
            Button { text: "前进"; enabled: view.canGoForward; onClicked: view.goForward() }
            Button {
                text: view.loading ? "停止" : "刷新"
                onClicked: view.loading ? view.stop() : view.reload()
            }
            Button {
                text: "重新进入"
                // token 失效或页面走丢时，按最新日志里的地址重新加载
                onClicked: {
                    webWin.requestedUrl = ""
                    webWin.load(berth.uiUrl(webWin.instanceId))
                }
            }
            Label {
                Layout.fillWidth: true
                // 地址栏不显示 token
                text: String(view.url).replace(/([?&]token=)[^&#]*/, "$1***")
                color: "#666666"
                elide: Text.ElideMiddle
            }
            Button { text: "浏览器打开"; onClicked: berth.openInBrowser(webWin.instanceId) }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            height: 2
            width: parent.width * view.loadProgress / 100
            visible: view.loading
            color: "#0078d4"
        }
    }

    WebEngineView {
        id: view
        anchors.fill: parent
        // 页面要求开新窗口（外链等）时交给系统浏览器
        onNewWindowRequested: function(request) {
            Qt.openUrlExternally(request.requestedUrl)
        }
    }
}
