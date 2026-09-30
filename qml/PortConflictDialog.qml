import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 启动前端口被占用时的冲突对话框：取消启动 / 改用空闲端口 / 复用现有服务（仅 dsh 特征探测命中时显示）
Dialog {
    id: dlg

    property string instanceId: ""
    property string instanceName: ""
    property int port: 0
    property string pid: ""
    property string processName: ""
    property string processPath: ""
    // 探测状态：probing（探测中）/ dsh（命中）/ other（未命中）
    property string probe: "probing"
    // 已选择某个选项时为 true，关闭时不再按"取消启动"收尾
    property bool handled: false

    function show(id, name, p, pidText, procName, procPath) {
        instanceId = id
        instanceName = name
        port = p
        pid = pidText
        processName = procName
        processPath = procPath
        probe = "probing"
        handled = false
        open()
    }

    title: "端口 " + port + " 已被占用"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, parent ? parent.width - 48 : 520)
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape

    onClosed: {
        if (!handled)
            berth.cancelConflict(instanceId)
    }

    Connections {
        target: berth
        function onConflictProbed(id, isDsh) {
            if (dlg.visible && id === dlg.instanceId)
                dlg.probe = isDsh ? "dsh" : "other"
        }
    }

    footer: DialogButtonBox {
        Button {
            text: "复用现有服务"
            flat: true
            highlighted: true
            visible: dlg.probe === "dsh"
            onClicked: {
                dlg.handled = true
                dlg.close()
                berth.reuseExisting(dlg.instanceId)
            }
        }
        Button {
            text: "改用空闲端口"
            flat: true
            onClicked: {
                dlg.handled = true
                dlg.close()
                berth.useFreePort(dlg.instanceId)
            }
        }
        Button {
            text: "取消启动"
            flat: true
            onClicked: dlg.close()
        }
    }

    contentItem: GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 6

        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "泊位「" + dlg.instanceName + "」的端口已被其它进程监听，未启动 dsh。"
        }
        Label { text: "端口"; color: "#555555" }
        Label { text: String(dlg.port); Layout.fillWidth: true }
        Label { text: "PID"; color: "#555555" }
        Label { text: dlg.pid; Layout.fillWidth: true }
        Label { text: "进程名"; color: "#555555" }
        Label { text: dlg.processName; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
        Label { text: "路径"; color: "#555555" }
        Label { text: dlg.processPath; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: "#555555"
            text: dlg.probe === "probing" ? "正在检测占用者是否为 dsh 服务…"
                : dlg.probe === "dsh" ? "占用者是 dsh 服务，可以直接复用（停止时只解除关联，不结束该进程）"
                : "占用者不是 dsh 服务，无法复用"
        }
    }
}
