import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

Item {
    id: pane

    // 状态色：运行绿、过渡橙、失败红、停止灰
    function statusColor(s) {
        if (s === "running") return "#107c10"
        if (s === "starting" || s === "stopping") return "#ca5010"
        if (s === "failed") return "#c42b1c"
        return "#666666"
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        Rectangle {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#e5e5e5"
            border.width: 1
            radius: 4

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                Button {
                    text: "新建泊位"
                    Layout.fillWidth: true
                    onClicked: {
                        const id = berth.createInstance()
                        list.currentIndex = berth.instances.rowCount() - 1
                        pane.selectedId = id
                    }
                }
                Button {
                    text: "识别已有 profile"
                    Layout.fillWidth: true
                    onClicked: {
                        berth.importDetectedProfiles()
                        if (berth.instances.rowCount() > 0 && pane.selectedId.length === 0) {
                            list.currentIndex = 0
                            pane.selectedId = berth.instances.data(berth.instances.index(0, 0), 257)
                        }
                    }
                }
                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: berth.instances
                    currentIndex: -1
                    delegate: ItemDelegate {
                        id: row
                        width: list.width
                        required property string id
                        required property string name
                        required property int port
                        required property string profile
                        required property string status
                        required property int index
                        highlighted: list.currentIndex === index
                        onClicked: {
                            list.currentIndex = index
                            pane.selectedId = id
                        }
                        background: Rectangle {
                            radius: 4
                            color: row.highlighted ? "#cce4f7" : row.hovered ? "#eef4fc" : "transparent"
                        }
                        contentItem: Column {
                            spacing: 2
                            Label { text: name; font.pixelSize: 14 }
                            Row {
                                spacing: 6
                                Label {
                                    text: profile + "  ·  " + port + "  ·"
                                    color: "#555555"
                                    font.pixelSize: 12
                                }
                                Label {
                                    text: berth.statusText(status)
                                    color: pane.statusColor(status)
                                    font.pixelSize: 12
                                    font.bold: status === "running"
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#e5e5e5"
            border.width: 1
            radius: 4

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10
                visible: pane.selectedId.length > 0

                Label {
                    text: nameField.text.length ? nameField.text : "未选择"
                    font.pixelSize: 20
                    font.bold: true
                }
                Label {
                    text: berth.statusText(pane.currentStatus) + (pane.currentPid > 0 ? "  ·  pid " + pane.currentPid : "")
                    color: pane.statusColor(pane.currentStatus)
                    font.bold: pane.currentStatus === "running"
                }
                Label {
                    visible: pane.currentError.length > 0
                    text: pane.currentError
                    color: "#c42b1c"
                    font.bold: true
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    Layout.fillWidth: true
                    Label { text: "名称" }
                    TextField { id: nameField; Layout.fillWidth: true; placeholderText: "泊位名" }
                    Label { text: "端口" }
                    TextField { id: portField; Layout.fillWidth: true; placeholderText: "3080"; inputMethodHints: Qt.ImhDigitsOnly }
                    Label { text: "Profile" }
                    ComboBox {
                        id: profileBox
                        Layout.fillWidth: true
                        editable: true
                        model: berth.detectProfiles(homeField.text)
                        onEditTextChanged: profileField.text = editText
                    }
                    TextField { id: profileField; visible: false }
                    Label { text: "DSH_HOME" }
                    TextField {
                        id: homeField
                        Layout.fillWidth: true
                        placeholderText: "留空则用默认 ~/.dsh"
                        onEditingFinished: profileBox.model = berth.detectProfiles(text)
                    }
                    Label { text: "工作区" }
                    TextField { id: workspaceField; Layout.fillWidth: true; placeholderText: "启动时的工作目录" }
                    Item {}
                    CheckBox { id: autoBox; text: "打开控台时自动启动" }
                }

                RowLayout {
                    Button {
                        text: "保存"
                        onClicked: berth.updateInstance(pane.selectedId, nameField.text, parseInt(portField.text),
                                                        profileBox.editText, homeField.text, workspaceField.text, autoBox.checked)
                    }
                    Button { text: "启动"; onClicked: berth.startInstance(pane.selectedId) }
                    Button { text: "停止"; onClicked: berth.stopInstance(pane.selectedId) }
                    Button { text: "重启"; onClicked: berth.restartInstance(pane.selectedId) }
                    Button { text: "打开界面"; onClicked: berth.openUi(pane.selectedId) }
                    Button { text: "日志"; onClicked: berth.openLog(pane.selectedId) }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "删除"
                        onClicked: {
                            berth.removeInstance(pane.selectedId)
                            pane.selectedId = ""
                            list.currentIndex = -1
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            Label {
                anchors.centerIn: parent
                visible: pane.selectedId.length === 0
                text: "新建泊位，或识别已有 dsh profile"
                color: "#666666"
            }
        }
    }

    property string selectedId: ""
    property string currentStatus: "stopped"
    property int currentPid: 0
    property string currentError: ""

    function syncForm() {
        if (!selectedId)
            return
        const item = berth.instance(selectedId)
        if (!item.id)
            return
        nameField.text = item.name
        portField.text = String(item.port)
        profileField.text = item.profile
        profileBox.editText = item.profile
        homeField.text = item.dshHome
        profileBox.model = berth.detectProfiles(item.dshHome)
        workspaceField.text = item.workspace
        autoBox.checked = item.autostart
        currentStatus = item.status
        currentPid = item.pid
        currentError = item.lastError || ""
    }

    onSelectedIdChanged: syncForm()
    Connections {
        target: berth.instances
        function onDataChanged() { pane.syncForm() }
    }
}
