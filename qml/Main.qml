import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 960
    height: 640
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: "DSH Berth"
    // 用系统原生控件外观，只把窗口底色从系统灰改成白
    color: "#ffffff"
    palette.window: "#ffffff"

    function showWindow() {
        visible = true
        raise()
        requestActivate()
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
                    model: ["泊位", "设置"]
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
        background: Rectangle { color: "#323232"; radius: 4 }
        Label { id: toastLabel; color: "#ffffff" }
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
