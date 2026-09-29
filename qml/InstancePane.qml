import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: pane

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        Rectangle {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#000000"
            border.width: 1

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
                        contentItem: Column {
                            spacing: 2
                            Label { text: name; color: highlighted ? "#ffffff" : "#000000"; font.pixelSize: 14 }
                            Label {
                                text: profile + "  ·  " + port + "  ·  " + berth.statusText(status)
                                color: highlighted ? "#ffffff" : "#000000"
                                font.pixelSize: 12
                                font.bold: status === "running"
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
            border.color: "#000000"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10
                visible: pane.selectedId.length > 0

                Label {
                    text: nameField.text.length ? nameField.text : "未选择"
                    color: "#000000"
                    font.pixelSize: 20
                    font.bold: true
                }
                Label {
                    text: berth.statusText(pane.currentStatus) + (pane.currentPid > 0 ? "  ·  pid " + pane.currentPid : "")
                    color: "#000000"
                    font.bold: pane.currentStatus === "running"
                }
                Label {
                    visible: pane.currentError.length > 0
                    text: pane.currentError
                    color: "#000000"
                    font.bold: true
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    Layout.fillWidth: true
                    Label { text: "名称"; color: "#000000" }
                    TextField { id: nameField; Layout.fillWidth: true; placeholderText: "泊位名"; color: "#000000" }
                    Label { text: "端口"; color: "#000000" }
                    TextField { id: portField; Layout.fillWidth: true; placeholderText: "3080"; inputMethodHints: Qt.ImhDigitsOnly; color: "#000000" }
                    Label { text: "Profile"; color: "#000000" }
                    ComboBox {
                        id: profileBox
                        Layout.fillWidth: true
                        editable: true
                        model: berth.detectProfiles(homeField.text)
                        onEditTextChanged: profileField.text = editText
                    }
                    TextField { id: profileField; visible: false }
                    Label { text: "DSH_HOME"; color: "#000000" }
                    TextField {
                        id: homeField
                        Layout.fillWidth: true
                        placeholderText: "留空则用默认 ~/.dsh"
                        color: "#000000"
                        onEditingFinished: profileBox.model = berth.detectProfiles(text)
                    }
                    Label { text: "工作区"; color: "#000000" }
                    TextField { id: workspaceField; Layout.fillWidth: true; placeholderText: "启动时的工作目录"; color: "#000000" }
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
                color: "#000000"
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
