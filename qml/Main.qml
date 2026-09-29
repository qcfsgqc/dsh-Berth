import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 960
    height: 640
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: "DSH Berth"
    color: "#ffffff"
    palette.window: "#ffffff"
    palette.windowText: "#000000"
    palette.base: "#ffffff"
    palette.text: "#000000"
    palette.button: "#ffffff"
    palette.buttonText: "#000000"
    palette.highlight: "#000000"
    palette.highlightedText: "#ffffff"
    palette.mid: "#000000"
    palette.dark: "#000000"
    palette.light: "#ffffff"
    palette.alternateBase: "#ffffff"

    function showWindow() {
        visible = true
        raise()
        requestActivate()
    }

    header: ToolBar {
        background: Rectangle { color: "#ffffff"; border.color: "#000000"; border.width: 1 }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 12
            Label {
                text: "DSH Berth"
                color: "#000000"
                font.pixelSize: 16
                font.bold: true
            }
            Label {
                text: "非官方控台"
                color: "#000000"
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Label {
                text: berth.version
                color: "#000000"
                font.pixelSize: 12
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 196
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#000000"
            border.width: 1
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6
                Button {
                    Layout.fillWidth: true
                    text: "泊位"
                    highlighted: stack.currentIndex === 0
                    onClicked: stack.currentIndex = 0
                }
                Button {
                    Layout.fillWidth: true
                    text: "设置"
                    highlighted: stack.currentIndex === 1
                    onClicked: stack.currentIndex = 1
                }
                Item { Layout.fillHeight: true }
                Label {
                    text: "只听 127.0.0.1"
                    color: "#000000"
                    font.pixelSize: 11
                }
            }
        }

        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0
            InstancePane {}
            SettingsPane {}
        }
    }

    Popup {
        id: toast
        x: (parent.width - width) / 2
        y: parent.height - height - 24
        padding: 12
        modal: false
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: "#ffffff"; border.color: "#000000"; border.width: 1 }
        Label { id: toastLabel; color: "#000000" }
        Timer { id: toastTimer; interval: 2800; onTriggered: toast.close() }
    }

    Connections {
        target: berth
        function onNotice(message) {
            toastLabel.text = message
            toast.open()
            toastTimer.restart()
        }
    }
}
