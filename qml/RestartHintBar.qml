import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 插件变动后的重启提示条：列出受影响泊位，提供"全部重启"和"稍后"；
// "全部重启"进行中显示进度。关闭（×）等同于"稍后"。
Rectangle {
    id: bar
    readonly property var hint: berth.restartHint

    visible: hint.visible || hint.busy
    implicitHeight: visible ? row.implicitHeight + 16 : 0
    color: "#fff4ce"
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#e5d18a" }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 8
        spacing: 8

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: bar.hint.busy
                  ? "正在重启泊位 " + Math.min(bar.hint.done + 1, bar.hint.total) + "/" + bar.hint.total + "…"
                  : "插件已变更，以下泊位需要重启才能生效：" + bar.hint.names.join("、")
        }
        Button {
            visible: bar.hint.visible
            text: "全部重启"
            enabled: !bar.hint.busy
            onClicked: bar.hint.restartAll()
        }
        Button {
            visible: bar.hint.visible
            text: "稍后"
            onClicked: bar.hint.later()
        }
        ToolButton {
            visible: bar.hint.visible
            text: "×"
            Accessible.name: "关闭重启提示"
            onClicked: bar.hint.later()
        }
    }
}
