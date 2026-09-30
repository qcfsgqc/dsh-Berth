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
                        if (berth.instances.rowCount() === 0)
                            pane.doCreate("")
                        else
                            newDialog.open()
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
                    Button { text: "浏览器打开"; onClicked: berth.openInBrowser(pane.selectedId) }

                    Button { text: "日志"; onClicked: berth.openLog(pane.selectedId) }
                    Button {
                        text: "插件"
                        onClicked: {
                            pluginsDialog.instanceId = pane.selectedId
                            pluginsDialog.open()
                        }
                    }
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
                // 终端面板：实时显示泊位日志尾部（dsh 的 stdout/stderr + Berth 标记）
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "终端输出"; font.bold: true }
                    Item { Layout.fillWidth: true }
                    CheckBox { id: followBox; text: "自动滚动"; checked: true }
                    Button { text: "刷新"; onClicked: terminal.refresh(true) }
                }
                Rectangle {
                    id: terminal
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 120
                    color: "#1e1e1e"
                    radius: 4

                    function refresh(forceBottom) {
                        if (!pane.selectedId)
                            return
                        const text = berth.readLog(pane.selectedId)
                        if (text === logView.text)
                            return
                        const stick = forceBottom || followBox.checked || logFlick.atYEnd
                        logView.text = text
                        if (stick)
                            Qt.callLater(function() {
                                logFlick.contentY = Math.max(0, logFlick.contentHeight - logFlick.height)
                            })
                    }

                    Flickable {
                        id: logFlick
                        anchors.fill: parent
                        anchors.margins: 8
                        clip: true
                        contentWidth: width
                        contentHeight: logView.contentHeight
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}

                        TextEdit {
                            id: logView
                            width: logFlick.width - 12
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            textFormat: TextEdit.PlainText
                            color: "#d4d4d4"
                            selectionColor: "#264f78"
                            font.family: "Consolas"
                            font.pixelSize: 12
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: logView.text.length === 0
                        text: "暂无输出"
                        color: "#808080"
                    }

                    Timer {
                        interval: 700
                        repeat: true
                        running: pane.visible && pane.selectedId.length > 0
                        onTriggered: terminal.refresh(false)
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: pane.selectedId.length === 0
                text: "新建泊位，或识别已有 dsh profile"
                color: "#666666"
            }
        }
    }

    PluginsDialog { id: pluginsDialog }

    // 新建泊位：询问空白新建，还是从某个已有泊位复制配置
    Dialog {
        id: newDialog
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(400, parent ? parent.width - 48 : 400)
        standardButtons: Dialog.Ok | Dialog.Cancel
        title: "新建泊位"

        onOpened: blankRadio.checked = true

        contentItem: ColumnLayout {
            spacing: 8

            ButtonGroup { id: newModeGroup }

            RadioButton {
                id: blankRadio
                ButtonGroup.group: newModeGroup
                text: "空白新建"
                checked: true
            }
            Label {
                text: "默认 profile「web」，端口自动分配"
                color: "#555555"
                font.pixelSize: 12
                Layout.leftMargin: 32
            }

            RadioButton {
                id: copyRadio
                ButtonGroup.group: newModeGroup
                text: "从已有泊位复制配置"
                enabled: copyBox.count > 0
            }
            ComboBox {
                id: copyBox
                enabled: copyRadio.checked
                Layout.fillWidth: true
                Layout.leftMargin: 32
                textRole: "name"
                model: berth.instances
                onActivated: copyRadio.checked = true
                delegate: ItemDelegate {
                    id: copyItem
                    required property var model
                    required property int index
                    width: copyBox.width
                    highlighted: copyBox.highlightedIndex === index
                    contentItem: Column {
                        spacing: 1
                        Label { text: copyItem.model.name; font.pixelSize: 13 }
                        Label {
                            text: copyItem.model.profile + "  ·  端口 " + copyItem.model.port
                            color: "#555555"
                            font.pixelSize: 12
                        }
                    }
                }
            }
            Label {
                text: "复制名称（加“ - 副本”）、profile、DSH_HOME、工作区与自动启动；端口重新分配，从停止状态开始"
                color: "#555555"
                font.pixelSize: 12
                Layout.leftMargin: 32
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        onAccepted: {
            if (blankRadio.checked || copyBox.count === 0) {
                pane.doCreate("")
            } else {
                const idx = copyBox.currentIndex
                pane.doCreate(berth.instances.data(berth.instances.index(idx, 0), 257))
            }
        }
    }

    property string selectedId: ""
    property string currentStatus: "stopped"
    property int currentPid: 0
    property string currentError: ""

    // 新建：sourceId 为空则空白新建，否则从该泊位复制配置
    function doCreate(sourceId) {
        const id = berth.createInstanceFrom(sourceId)
        list.currentIndex = berth.instances.rowCount() - 1
        pane.selectedId = id
    }

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

    onSelectedIdChanged: {
        syncForm()
        logView.text = ""
        terminal.refresh(true)
    }
    Connections {
        target: berth.instances
        function onDataChanged() { pane.syncForm() }
    }
}
