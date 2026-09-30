import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 960
    height: 640
    minimumWidth: 760
    minimumHeight: 480
    // 带 --autostart 且 startMinimized 为真时只显示托盘（berthStartHidden 由 main.cpp 注入）
    visible: !berthStartHidden
    title: "DSH Berth"
    // 用系统原生控件外观，只把窗口底色从系统灰改成白
    color: "#ffffff"
    palette.window: "#ffffff"

    function showWindow() {
        visible = true
        // 已最小化时先还原（托盘、系统通知点击等场景）
        if (visibility === Window.Minimized)
            showNormal()
        raise()
        requestActivate()
    }

    // —— 供 Notifier 调用（main.cpp 经 invokeMethod） ——
    function selectedInstanceId() {
        return instancePane.selectedId
    }
    function selectInstance(id) {
        stack.currentIndex = 0
        instancePane.selectInstance(id)
    }
    function showToast(message, ms) {
        toastLabel.text = message
        toastTimer.interval = ms > 0 ? ms : 2800
        toast.open()
        toastTimer.restart()
    }

    // 关闭到托盘开启：接受关闭（quitOnLastWindowClosed=false，只隐藏）；
    // 关闭时：没有 Active_State 泊位直接退出，有则弹确认框
    property bool quitInProgress: false
    onClosing: function(close) {
        if (quitInProgress || berth.settings.closeToTray)
            return
        close.accepted = false
        const names = berth.activeInstanceNames()
        if (names.length === 0) {
            quitInProgress = true
            Qt.quit()
            return
        }
        quitConfirmDialog.names = names
        quitConfirmDialog.open()
    }

    header: Rectangle {
        implicitHeight: 44
        color: "#ffffff"
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#e5e5e5" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10
            Label {
                text: "DSH Berth"
                font.pixelSize: 16
                font.bold: true
            }
            Label {
                text: "非官方控台"
                color: "#666666"
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Label {
                text: berth.version
                color: "#666666"
                font.pixelSize: 12
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            color: "#ffffff"
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#e5e5e5" }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4
                Repeater {
                    model: ["泊位", "设置", "dsh 版本", "扩展", "环境", "用量", "更新"]
                    ItemDelegate {
                        id: navItem
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        highlighted: stack.currentIndex === index
                        onClicked: stack.currentIndex = index
                        background: Rectangle {
                            radius: 4
                            color: navItem.highlighted ? win.palette.highlight
                                 : navItem.hovered ? "#eef4fc" : "transparent"
                        }
                        contentItem: Label {
                            text: navItem.modelData
                            color: navItem.highlighted ? win.palette.highlightedText : win.palette.text
                            font.pixelSize: 14
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Label {
                    text: "只听 127.0.0.1"
                    color: "#666666"
                    font.pixelSize: 11
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 顶部汇总面板（需求 24）：点击"运行中"/"失败"切换泊位列表筛选，并切到泊位页
            SummaryPanel {
                Layout.fillWidth: true
                statusFilter: instancePane.statusFilter
                // 可用更新数（需求 24.7/24.8）：完成过一次检查才显示，点击打开更新页
                updateCountEnabled: berth.updates.lastCheckAt.length > 0
                updateCount: berth.updates.updateCount
                onUpdateCountClicked: stack.currentIndex = stack.updatePageIndex
                onFilterToggled: kind => {
                    instancePane.statusFilter = instancePane.statusFilter === kind ? "" : kind
                    if (instancePane.statusFilter.length)
                        stack.currentIndex = 0
                }
            }

            // 插件变动后的重启提示（无受影响泊位时不显示）
            RestartHintBar { Layout.fillWidth: true }

            StackLayout {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0
                // 与左侧导航顺序一一对应
                readonly property int updatePageIndex: 6
                InstancePane { id: instancePane }
                SettingsPane {}
                VersionsPage {}
                ExtensionsPage {}
                EnvPage {}
                UsagePage { id: usagePage; active: usagePage.StackLayout.isCurrentItem }
                UpdatePage {}
            }
        }
    }

    Popup {
        id: toast
        x: (parent.width - width) / 2
        y: parent.height - height - 24
        padding: 12
        modal: false
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: "#323232"; radius: 4 }
        Label { id: toastLabel; color: "#ffffff" }
        Timer { id: toastTimer; interval: 2800; onTriggered: toast.close() }
    }

    // 泊位 id → WebWindow；每个泊位最多一个界面窗口
    property var webWindows: ({})
    Component { id: webWindowComponent; WebWindow {} }

    // 手动启动/重启时端口被占用的冲突对话框
    PortConflictDialog { id: portConflictDialog }

    // 批量启停汇总：有失败或被取消时弹窗列出明细，否则只给 toast
    Dialog {
        id: batchSummaryDialog
        property string summaryText: ""
        title: "批量操作结果"
        anchors.centerIn: parent
        width: Math.min(win.width - 80, 480)
        modal: true
        standardButtons: Dialog.Ok
        Label {
            width: parent.width
            text: batchSummaryDialog.summaryText
            wrapMode: Text.Wrap
        }
    }

    // 退出确认：列出活动泊位；取消或关闭对话框时窗口与泊位状态不变
    Dialog {
        id: quitConfirmDialog
        property var names: []
        title: "退出 DSH Berth"
        anchors.centerIn: parent
        width: Math.min(win.width - 80, 480)
        modal: true
        closePolicy: win.quitInProgress ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
        ColumnLayout {
            width: parent.width
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: win.quitInProgress ? "正在停止泊位，最多等待 10 秒后强制结束并退出…"
                                         : "以下泊位仍在运行，退出前将停止它们："
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: quitConfirmDialog.names.map(function(n) { return "• " + n }).join("\n")
            }
        }
        footer: DialogButtonBox {
            Button {
                text: "停止并退出"
                enabled: !win.quitInProgress
                onClicked: {
                    win.quitInProgress = true
                    berth.stopActiveAndQuit()
                }
            }
            Button {
                text: "取消"
                enabled: !win.quitInProgress
                onClicked: quitConfirmDialog.close()
            }
        }
    }

    // 主窗口初始化完成后按错峰间隔依次启动 autostart 泊位（没有时不发起、不显示进度）
    Component.onCompleted: {
        Qt.callLater(function() { berth.batch.startAutostart() })
        if (visible)
            autoCheckEnvOnce()
    }

    // 主窗口第一次显示时在后台检测一次环境（托盘启动时推迟到第一次显示）
    property bool envAutoChecked: false
    function autoCheckEnvOnce() {
        if (envAutoChecked)
            return
        envAutoChecked = true
        Qt.callLater(function() {
            if (!berth.env.checkedOnce && !berth.env.checking)
                berth.env.checkAll()
            // 需求 14.2：主窗口显示后后台执行一次全部更新检查
            berth.updates.checkAll()
        })
    }
    onVisibleChanged: if (visible) autoCheckEnvOnce()

    Connections {
        target: berth.batch
        function onFinished(summary) {
            if (summary.failed > 0 || summary.cancelled) {
                batchSummaryDialog.summaryText = summary.text
                batchSummaryDialog.open()
            } else {
                win.showToast(summary.text, 2800)
            }
        }
    }

    // "全部重启"汇总：有失败时弹窗列出泊位名与原因，否则只给 toast
    Connections {
        target: berth.restartHint
        function onFinished(summary) {
            if (summary.failed > 0) {
                batchSummaryDialog.summaryText = summary.text
                batchSummaryDialog.open()
            } else {
                win.showToast(summary.text, 2800)
            }
        }
    }

    Connections {
        target: berth
        function onPortConflict(id, name, port, pid, processName, processPath) {
            portConflictDialog.show(id, name, port, pid, processName, processPath)
        }
        function onNotice(message) {
            win.showToast(message, 2800)
        }
        function onUiRequested(id, url) {
            let w = win.webWindows[id]
            if (!w) {
                w = webWindowComponent.createObject(null, {
                    instanceId: id,
                    instanceName: berth.instance(id).name || ""
                })
                if (!w)
                    return
                win.webWindows[id] = w
                w.closing.connect(function() {
                    delete win.webWindows[id]
                    w.destroy()
                })
            }
            w.load(url)
        }
    }
}
