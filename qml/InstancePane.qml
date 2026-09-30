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

    property string selectedId: ""
    property string currentStatus: "stopped"
    property int currentPid: 0
    property string currentError: ""

    // profile 选择状态（与 selectedId 互斥）
    property string selectedProfileKey: ""
    property string selectedProfileHome: ""
    property string selectedProfileName: ""
    // berth.profileInfo() 的返回值缓存
    property var profileDetail: null

    function selectInstance(id) {
        clearProfileSelection()
        selectedId = id
    }

    function selectProfileKey(home, name) {
        selectedId = ""
        logView.text = ""
        selectedProfileHome = home
        selectedProfileName = name
        selectedProfileKey = home + "\u0001" + name
        currentStatus = "stopped"
        currentPid = 0
        currentError = ""
        reloadProfileInfo()
    }

    function clearProfileSelection() {
        selectedProfileKey = ""
        selectedProfileHome = ""
        selectedProfileName = ""
        profileDetail = null
    }

    function reloadProfileInfo() {
        if (selectedProfileName.length === 0) {
            profileDetail = null
            return
        }
        profileDetail = berth.profileInfo(selectedProfileHome, selectedProfileName)
    }

    // 打开重命名 / 复制对话框
    function editProfile(mode) {
        profileDialog.mode = mode
        profileDialog.home = selectedProfileHome
        profileDialog.profileName = selectedProfileName
        profileDialog.open()
    }

    // 从当前 profile 新建泊位并切到新泊位的详情
    function createInstanceForProfile() {
        const id = berth.createInstanceFor(selectedProfileHome, selectedProfileName)
        if (id) {
            logView.text = ""
            clearProfileSelection()
            selectedId = id
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        // 左栏：新建按钮 + profile / 泊位树
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
                    text: "新建 Profile"
                    Layout.fillWidth: true
                    onClicked: {
                        profileDialog.mode = "create"
                        profileDialog.open()
                    }
                }

                TreeView {
                    id: tree
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: berth.profileTree
                    // 显示树形结构，不要水平滚动
                    columnWidthProvider: function() { return tree.width }

                    // 模型 reset 后展开态会丢失；ProfileTreeModel 是一次性构建的两层树，
                    // 展开态不持久化，每次重置后恢复为"全部 profile 行默认展开"
                    Connections {
                        target: berth.profileTree
                        function onModelReset() { Qt.callLater(tree.restoreExpansion) }
                    }

                    function restoreExpansion() {
                        // -1 表示所有根节点；depth 1 即展开到泊位行
                        tree.expandRecursively(-1, 1)
                    }

                    delegate: ItemDelegate {
                        id: treeRow

                        required property TreeView treeView
                        required property int row
                        required property int depth
                        required property bool expanded
                        required property var model

                        // 高亮依据 pane 的选择状态而非 currentIndex，模型 reset 后不丢
                        readonly property bool isHighlighted: treeRow.model.nodeType === 0
                            ? (pane.selectedId.length === 0
                               && pane.selectedProfileKey === (treeRow.model.home + "\u0001" + treeRow.model.name))
                            : (pane.selectedId.length > 0 && pane.selectedId === treeRow.model.id)

                        implicitWidth: tree.width
                        implicitHeight: treeRow.model.nodeType === 0 ? 44 : 52

                        highlighted: treeRow.isHighlighted

                        onClicked: {
                            if (treeRow.model.nodeType === 0) {
                                pane.selectProfileKey(treeRow.model.home, treeRow.model.name)
                            } else {
                                pane.selectInstance(treeRow.model.id)
                            }
                        }

                        background: Rectangle {
                            radius: 4
                            color: treeRow.isHighlighted ? "#cce4f7" : treeRow.hovered ? "#eef4fc" : "transparent"
                        }

                        contentItem: Item {
                            implicitHeight: treeRow.implicitHeight

                            // 泊位行正文
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 6 + treeRow.depth * 14 + (treeRow.model.nodeType === 0 ? 0 : 14)
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                spacing: 2

                                Row {
                                    spacing: 4
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: treeRow.model.nodeType === 0
                                        text: treeRow.expanded ? "▾" : "▸"
                                        color: "#666666"
                                        font.pixelSize: 10
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: treeRow.treeView.toggleExpanded(treeRow.row)
                                        }
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: {
                                            const base = treeRow.model.name || ""
                                            return treeRow.model.nodeType === 0 && !treeRow.model.dirExists
                                                   ? base + "（目录缺失）"
                                                   : base
                                        }
                                        font.pixelSize: treeRow.model.nodeType === 0 ? 14 : 14
                                        font.bold: treeRow.model.nodeType === 0
                                        color: treeRow.model.nodeType === 0 && !treeRow.model.dirExists ? "#b45309" : pane.palette.text
                                    }
                                }
                                Row {
                                    visible: treeRow.model.nodeType === 1
                                    spacing: 6
                                    Label {
                                        text: treeRow.model.profile + "  ·  " + treeRow.model.port + "  ·"
                                        color: "#555555"
                                        font.pixelSize: 12
                                    }
                                    Label {
                                        text: berth.statusText(treeRow.model.status)
                                        color: pane.statusColor(treeRow.model.status)
                                        font.pixelSize: 12
                                        font.bold: treeRow.model.status === "running"
                                    }
                                }
                            }

                            // profile 行右侧的泊位数
                            Label {
                                visible: treeRow.model.nodeType === 0
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: treeRow.model.instanceCount > 0
                                      ? treeRow.model.instanceCount + " 台泊位"
                                      : "无泊位"
                                color: "#999999"
                                font.pixelSize: 11
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: berth.profileTree.rowCount() === 0
                        text: "还没有 profile 或泊位"
                        color: "#999999"
                        font.pixelSize: 12
                    }
                }
            }
        }

        // 右侧泊位详情面板
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#e5e5e5"
            border.width: 1
            radius: 4
            visible: pane.selectedId.length > 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

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
                        onClicked: pluginsDialog.openForInstance(pane.selectedId)
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "删除"
                        onClicked: {
                            berth.removeInstance(pane.selectedId)
                            pane.selectedId = ""
                        }
                    }
                }

                // 终端面板：实时显示泊位日志尾部
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
                        running: pane.visible && pane.selectedId.length > 0 && pane.selectedProfileKey.length === 0
                        onTriggered: terminal.refresh(false)
                    }
                }
            }
        }

        // 右侧 profile 详情面板
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#e5e5e5"
            border.width: 1
            radius: 4
            visible: pane.selectedProfileKey.length > 0 && pane.selectedId.length === 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                Label {
                    text: pane.selectedProfileName + "  (profile)"
                    font.pixelSize: 20
                    font.bold: true
                }
                Label {
                    text: pane.selectedProfileHome
                    color: "#555555"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }

                Label {
                    visible: pane.profileDetail && pane.profileDetail.exists === false
                    text: "profile 目录不存在（可能已被删除或未初始化）"
                    color: "#b45309"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    Layout.fillWidth: true
                    Label { text: "路径" }
                    Label {
                        text: pane.profileDetail ? pane.profileDetail.dir : "—"
                        color: "#555555"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                    }
                    Label { text: "用户插件数" }
                    Label {
                        text: pane.profileDetail ? String(pane.profileDetail.pluginCount) : "—"
                    }
                    Label { text: "bundles 数量" }
                    Label {
                        text: pane.profileDetail && pane.profileDetail.bundles
                              ? String(pane.profileDetail.bundles.length) : "—"
                    }
                    Label { text: "bundles" }
                    Label {
                        Layout.fillWidth: true
                        visible: pane.profileDetail && pane.profileDetail.bundles && pane.profileDetail.bundles.length > 0
                        text: pane.profileDetail.bundles ? pane.profileDetail.bundles.join("、") : "（无）"
                        color: "#555555"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                    Label {
                        visible: pane.profileDetail && pane.profileDetail.bundles && pane.profileDetail.bundles.length === 0
                        text: "（无）"
                        color: "#999999"
                        font.pixelSize: 12
                    }
                }

                RowLayout {
                    Button { text: "新建泊位"; onClicked: pane.createInstanceForProfile() }
                    Button { text: "打开目录"; onClicked: berth.openProfileDir(pane.selectedProfileHome, pane.selectedProfileName) }
                    Button { text: "插件"; onClicked: pluginsDialog.openForProfile(pane.selectedProfileHome, pane.selectedProfileName) }
                    Item { Layout.fillWidth: true }
                    Button { text: "刷新"; onClicked: pane.reloadProfileInfo() }
                }

                RowLayout {
                    Button { text: "重命名"; onClicked: pane.editProfile("rename") }
                    Button { text: "复制"; onClicked: pane.editProfile("copy") }
                    Item { Layout.fillWidth: true }
                    Button { text: "删除"; onClicked: confirmProfileDelete.open() }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // 未选择任何 profile / 泊位时的占位提示
    Label {
        anchors.centerIn: parent
        visible: pane.selectedId.length === 0 && pane.selectedProfileKey.length === 0
        text: "创建 profile，或从已有 profile 选择泊位"
        color: "#666666"
    }

    PluginsDialog { id: pluginsDialog }
    ProfileDialog { id: profileDialog }

    Connections {
        target: berth
        function onProfileOpFinished(ok, message, name) { pane.reloadProfileInfo() }
    }

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

    // 删除 profile 的二级确认
    Dialog {
        id: confirmProfileDelete
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 48 : 440)
        standardButtons: Dialog.Ok | Dialog.Cancel
        title: "删除 profile"

        onAccepted: {
            berth.deleteProfile(pane.selectedProfileHome, pane.selectedProfileName)
            pane.clearProfileSelection()
        }

        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: "将删除 profile「" + pane.selectedProfileName + "」目录及其全部数据（含已安装插件与配置），不可恢复。"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    // 新建：sourceId 为空则空白新建，否则从该泊位复制配置
    function doCreate(sourceId) {
        const id = berth.createInstanceFrom(sourceId)
        pane.selectInstance(id)
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
