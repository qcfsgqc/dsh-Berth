import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

Item {
    id: pane

    // 状态色：运行绿（含外部）、过渡橙、失败红（含崩溃已停止）、停止灰
    function statusColor(s) {
        if (s === "running" || s === "external") return "#107c10"
        if (s === "starting" || s === "stopping") return "#ca5010"
        if (s === "failed" || s === "crashStopped") return "#c42b1c"
        return "#666666"
    }

    // 退避等待中的倒计时文字；不在等待中返回空串
    function countdownText(id) {
        const c = id ? berth.restartCountdowns[id] : undefined
        return c ? "将在 " + c.sec + " 秒后重启（第 " + c.n + "/" + c.N + " 次）" : ""
    }

    property string selectedId: ""
    // 顶部汇总面板的状态筛选（""/"running"/"failed"）；只作用于分组列表，开启时切到分组视图
    property string statusFilter: ""
    onStatusFilterChanged: if (statusFilter.length) listMode.currentIndex = 0
    property string currentStatus: "stopped"
    property int currentPid: 0
    property string currentError: ""
    // 在外部工具中打开 workspace：三个操作的启用状态与原因（打开详情、保存后重新检测）及最近一次启动失败信息
    property var workspaceAvail: ({})
    property string workspaceOpenError: ""
    function refreshWorkspaceOpen() {
        workspaceOpenError = ""
        workspaceAvail = selectedId.length > 0 ? berth.workspaceOpener.availability(selectedId) : ({})
    }
    // dsh 版本绑定：已保存值与选择器当前值（空为系统 dsh）
    property string savedDshVersion: ""
    property string selectedDshVersion: ""
    // 选择器选项：系统 dsh + 全部已安装版本；已保存/当前选中的版本不在仓库中时追加并标记"缺失"
    readonly property var dshVersionOptions: {
        const installed = berth.versions.versions
        const out = [{ text: "系统 dsh", value: "" }]
        const seen = {}
        for (let i = 0; i < installed.length; ++i) {
            out.push({ text: installed[i].version, value: installed[i].version })
            seen[installed[i].version] = true
        }
        const extra = [savedDshVersion, selectedDshVersion]
        for (let j = 0; j < extra.length; ++j) {
            const v = extra[j]
            if (v.length && !seen[v]) {
                out.push({ text: v + "（缺失）", value: v })
                seen[v] = true
            }
        }
        return out
    }

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
                Button {
                    text: "导入 Bundle"
                    Layout.fillWidth: true
                    enabled: !berth.bundles.busy
                    onClicked: bundleDialogs.openImport(pane.selectedProfileHome)
                }


                // 批量启停：目标为空或批量进行中时禁用。"启动分组/停止分组"待分组功能（22.4）开启
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 6
                    rowSpacing: 6
                    visible: !berth.batch.busy
                    Button {
                        text: "启动全部"
                        Layout.fillWidth: true
                        enabled: berth.batch.allCount > 0
                        onClicked: berth.batch.startAll()
                    }
                    Button {
                        text: "停止全部"
                        Layout.fillWidth: true
                        enabled: berth.batch.allCount > 0
                        onClicked: berth.batch.stopAll()
                    }
                    Button {
                        text: "启动所选"
                        Layout.fillWidth: true
                        enabled: pane.selectedId.length > 0
                        onClicked: berth.batch.startSelected([pane.selectedId])
                    }
                    Button {
                        text: "停止所选"
                        Layout.fillWidth: true
                        enabled: pane.selectedId.length > 0
                        onClicked: berth.batch.stopSelected([pane.selectedId])
                    }
                }
                // 批量进行中：进度 + 取消
                RowLayout {
                    Layout.fillWidth: true
                    visible: berth.batch.busy
                    spacing: 6
                    Label {
                        Layout.fillWidth: true
                        text: (berth.batch.kind === "stop" ? "批量停止 " : "批量启动 ")
                              + berth.batch.done + "/" + berth.batch.total
                    }
                    Button {
                        text: "取消"
                        onClicked: berth.batch.cancel()
                    }
                }

                // 列表视图：按分组（可折叠 + 搜索）或按 Profile 树
                TabBar {
                    id: listMode
                    Layout.fillWidth: true
                    TabButton { text: "分组" }
                    TabButton { text: "Profile" }
                }

                GroupedInstanceList {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: listMode.currentIndex === 0
                    selectedId: pane.selectedId
                    statusFilter: pane.statusFilter
                    onInstanceClicked: id => pane.selectInstance(id)
                }

                TreeView {
                    id: tree
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: listMode.currentIndex === 1
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

                        readonly property string countdown: treeRow.model.nodeType === 1
                                                            ? pane.countdownText(treeRow.model.id) : ""

                        implicitWidth: tree.width
                        implicitHeight: treeRow.model.nodeType === 0 ? 44 : (treeRow.countdown.length > 0 ? 68 : 52)

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
                                    // 插件变动后选择"稍后"或重启失败：下次进入运行中时移除
                                    Label {
                                        visible: !!berth.restartHint.pending[treeRow.model.id]
                                        text: "待重启"
                                        color: "#b45309"
                                        font.pixelSize: 12
                                        font.bold: true
                                    }
                                }
                                Label {
                                    visible: treeRow.countdown.length > 0
                                    text: treeRow.countdown
                                    color: "#ca5010"
                                    font.pixelSize: 12
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
                    visible: pane.selectedId.length > 0 && !!berth.restartHint.pending[pane.selectedId]
                    text: "待重启"
                    color: "#b45309"
                    font.bold: true
                }
                Label {
                    readonly property string countdown: pane.countdownText(pane.selectedId)
                    visible: countdown.length > 0
                    text: countdown
                    color: "#ca5010"
                    font.bold: true
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
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            Repeater {
                                model: [
                                    { target: "vscode", text: "在 VS Code 中打开" },
                                    { target: "cursor", text: "在 Cursor 中打开" },
                                    { target: "explorer", text: "在资源管理器中打开" }
                                ]
                                // 包一层 Item：禁用的按钮不接收悬停，由外层 HoverHandler 显示禁用原因
                                delegate: Item {
                                    required property var modelData
                                    readonly property var state_: pane.workspaceAvail[modelData.target] || ({})
                                    implicitWidth: openBtn.implicitWidth
                                    implicitHeight: openBtn.implicitHeight
                                    Button {
                                        id: openBtn
                                        anchors.fill: parent
                                        text: parent.modelData.text
                                        enabled: parent.state_.enabled === true
                                        onClicked: pane.workspaceOpenError =
                                            berth.workspaceOpener.open(pane.selectedId, parent.modelData.target)
                                    }
                                    HoverHandler { id: openHover }
                                    ToolTip.visible: openHover.hovered && !openBtn.enabled && (state_.reason || "").length > 0
                                    ToolTip.text: state_.reason || ""
                                    ToolTip.delay: 300
                                }
                            }
                        }
                        Label {
                            visible: pane.workspaceOpenError.length > 0
                            text: pane.workspaceOpenError
                            color: "#c42b1c"
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                    Label { text: "dsh 版本" }
                    ComboBox {
                        id: versionBox
                        Layout.fillWidth: true
                        model: pane.dshVersionOptions
                        textRole: "text"
                        valueRole: "value"
                        onActivated: index => pane.selectedDshVersion = pane.dshVersionOptions[index].value
                        // 选项列表或选中值变化时重新定位（用户选择会打断普通绑定，故用 Binding）
                        Binding on currentIndex {
                            value: {
                                const opts = pane.dshVersionOptions
                                for (let i = 0; i < opts.length; ++i)
                                    if (opts[i].value === pane.selectedDshVersion)
                                        return i
                                return 0
                            }
                        }
                    }
                    Item {}
                    CheckBox { id: autoBox; text: "打开控台时自动启动" }
                    Item {}
                    CheckBox { id: autoRestartBox; text: "崩溃自动重启" }
                }

                // 图标、分组、标签、备注
                InstanceMetaEditor {
                    id: metaEditor
                    Layout.fillWidth: true
                }

                // 环境变量表与额外启动参数；行多时在限定高度内滚动
                ScrollView {
                    id: envScroll
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(envEditor.implicitHeight, 260)
                    contentWidth: availableWidth
                    clip: true
                    EnvArgsEditor {
                        id: envEditor
                        width: envScroll.availableWidth
                    }
                }

                RowLayout {
                    Button {
                        text: "保存"
                        // instances.json 只读模式（schema 过高或损坏）时禁止保存
                        enabled: !berth.instancesReadOnly
                        onClicked: {
                            // 分组/标签/备注不合法时整体不保存，提示原因（需求 9.6）
                            const meta = metaEditor.values()
                            const check = berth.checkInstanceMeta(meta.group, meta.tags, meta.notes)
                            if (!check.ok) {
                                metaEditor.error = check.error
                                return
                            }
                            // 被拒绝时表格内容保留；成功后按已保存内容重载
                            if (berth.updateInstance(pane.selectedId, nameField.text, parseInt(portField.text),
                                                     profileBox.editText, homeField.text, workspaceField.text, autoBox.checked,
                                                     autoRestartBox.checked, envEditor.envRows(), envEditor.argList(),
                                                     pane.selectedDshVersion)) {
                                const saved = berth.instance(pane.selectedId)
                                // workspace 可能已改，重新校验三个打开操作（需求 10.7）
                                pane.refreshWorkspaceOpen()
                                envEditor.load(saved.env, saved.extraArgs)
                                pane.savedDshVersion = saved.dshVersion || ""
                                pane.selectedDshVersion = pane.savedDshVersion
                                // 图标（本地图片此时复制到 icons/）、分组、标签、备注
                                const r = berth.setInstanceMeta(pane.selectedId, meta.iconKind, meta.iconValue,
                                                                meta.group, meta.tags, meta.notes)
                                if (r.ok)
                                    metaEditor.load(berth.instance(pane.selectedId))
                                else
                                    metaEditor.error = r.error
                            }
                        }
                    }
                    Button { text: "启动"; onClicked: berth.startInstance(pane.selectedId) }
                    Button { text: "停止"; onClicked: berth.stopInstance(pane.selectedId) }
                    Button { text: "重启"; onClicked: berth.restartInstance(pane.selectedId) }
                    Button { text: "打开界面"; onClicked: berth.openUi(pane.selectedId) }
                    Button { text: "浏览器打开"; onClicked: berth.openInBrowser(pane.selectedId) }

                    Button { text: "日志"; onClicked: logViewer.openFor(pane.selectedId, nameField.text) }
                    Button {
                        text: "插件"
                        onClicked: pluginsDialog.openForInstance(pane.selectedId)
                    }
                    Button {
                        text: "导出"
                        enabled: !berth.bundles.busy
                        onClicked: bundleDialogs.exportInstance(pane.selectedId)
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

                // 导入后依赖重建失败："依赖未就绪" + 错误摘要 + 重试
                RowLayout {
                    readonly property var pending: (berth.bundles.depsPending, pane.selectedId.length
                                                    ? bundleDialogs.pendingFor(pane.selectedId, "", "") : null)
                    Layout.fillWidth: true
                    visible: !!pending
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: "#c42b1c"
                        text: parent.pending ? "依赖未就绪：" + (parent.pending.error || "") : ""
                    }
                    Button {
                        text: berth.bundles.busy ? "重试中…" : "重试"
                        enabled: !berth.bundles.busy
                        onClicked: bundleDialogs.retry(parent.pending.key)
                    }
                }

                // 启动前自检隔离的插件（quarantine.json）；在"插件"对话框中可解除隔离

                Label {
                    id: quarantineLabel
                    property int rev: 0
                    readonly property var items: (rev, pane.selectedId.length ? berth.quarantinedFor(pane.selectedId) : [])
                    Layout.fillWidth: true
                    visible: items.length > 0
                    color: "#c42b1c"
                    wrapMode: Text.Wrap
                    text: {
                        const parts = []
                        for (let i = 0; i < items.length; ++i)
                            parts.push(items[i].name + "（" + items[i].reasonText + "）")
                        return "已隔离的插件：" + parts.join("、") + "。可在「插件」中解除隔离"
                    }
                    Connections {
                        target: berth.quarantine
                        function onChanged() { quarantineLabel.rev++ }
                    }
                }

                // 会话统计（需求 21）：读取失败时三项显示"暂无数据"，悬停显示原因
                ColumnLayout {
                    id: sessionStats
                    property int rev: 0
                    readonly property var st: (rev, pane.selectedId.length ? berth.stats.sessionStatsFor(pane.selectedId) : ({}))
                    readonly property bool failed: st.state === "error"
                    readonly property bool ready: st.state === "ok"
                    function valueText(v) {
                        if (failed)
                            return "暂无数据"
                        if (ready)
                            return String(v)
                        return st.loading ? "读取中…" : "—"
                    }
                    Layout.fillWidth: true
                    spacing: 2

                    RowLayout {
                        spacing: 16
                        HoverHandler { id: sessionHover }
                        ToolTip.visible: sessionHover.hovered && sessionStats.failed
                        ToolTip.text: sessionStats.st.error || ""
                        Label { text: "会话"; font.bold: true }
                        Label { text: "总数：" + sessionStats.valueText(sessionStats.st.total) }
                        Label { text: "活跃：" + sessionStats.valueText(sessionStats.st.active) }
                        Label { text: "最近活动：" + sessionStats.valueText(sessionStats.st.lastActivity) }
                        Label {
                            visible: sessionStats.st.loading === true && (sessionStats.ready || sessionStats.failed)
                            text: "刷新中…"
                            color: "#6b6b6b"
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: "#6b6b6b"
                        font.pixelSize: 11
                        text: "口径：会话数为 DSH_HOME/sessions 下（配置了工作区时只算该工作区）的会话目录数；"
                              + "活跃为日志文件 30 分钟内有写入的会话（估算）；最近活动取日志最后修改时间。运行中每 30 秒刷新"
                    }
                    Connections {
                        target: berth.stats
                        function onSessionStatsChanged(id) {
                            if (id === pane.selectedId)
                                sessionStats.rev++
                        }
                    }
                }

                // 进程运行指标（需求 23）：当前 CPU/内存、趋势图、累计数据与重置
                MetricsPanel {
                    instanceId: pane.selectedId
                    status: pane.currentStatus
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
                    Button {
                        text: "导出"
                        enabled: !berth.bundles.busy
                        onClicked: bundleDialogs.exportProfile(pane.selectedProfileHome, pane.selectedProfileName)
                    }

                    Item { Layout.fillWidth: true }
                    Button { text: "刷新"; onClicked: pane.reloadProfileInfo() }
                }

                RowLayout {
                    Button { text: "重命名"; onClicked: pane.editProfile("rename") }
                    Button { text: "复制"; onClicked: pane.editProfile("copy") }
                    Item { Layout.fillWidth: true }
                    Button { text: "删除"; onClicked: confirmProfileDelete.open() }
                }

                // 导入后依赖重建失败（profile 类型）："依赖未就绪" + 重试
                RowLayout {
                    readonly property var pending: (berth.bundles.depsPending, pane.selectedProfileName.length
                                                    ? bundleDialogs.pendingFor("", pane.selectedProfileHome, pane.selectedProfileName) : null)
                    Layout.fillWidth: true
                    visible: !!pending
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: "#c42b1c"
                        text: parent.pending ? "依赖未就绪：" + (parent.pending.error || "") : ""
                    }
                    Button {
                        text: berth.bundles.busy ? "重试中…" : "重试"
                        enabled: !berth.bundles.busy
                        onClicked: bundleDialogs.retry(parent.pending.key)
                    }
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
    LogViewer { id: logViewer }
    ProfileDialog { id: profileDialog }
    BundleDialogs { id: bundleDialogs }


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

    // force：切换泊位时无条件重载环境变量表；状态刷新时不覆盖未保存的编辑
    function syncForm(force) {
        if (!selectedId)
            return
        const item = berth.instance(selectedId)
        if (!item.id)
            return
        if (force === true || !envEditor.dirty)
            envEditor.load(item.env, item.extraArgs)
        if (force === true || !metaEditor.dirty)
            metaEditor.load(item)
        nameField.text = item.name
        portField.text = String(item.port)
        profileField.text = item.profile
        profileBox.editText = item.profile
        homeField.text = item.dshHome
        profileBox.model = berth.detectProfiles(item.dshHome)
        workspaceField.text = item.workspace
        autoBox.checked = item.autostart
        autoRestartBox.checked = item.autoRestart === true
        // 切换泊位时重载版本绑定；状态刷新时不覆盖尚未保存的选择
        if (force === true || selectedDshVersion === savedDshVersion)
            selectedDshVersion = item.dshVersion || ""
        savedDshVersion = item.dshVersion || ""
        currentStatus = item.status
        currentPid = item.pid
        currentError = item.lastError || ""
    }

    onSelectedIdChanged: {
        syncForm(true)
        // 打开详情时重新检测 VS Code / Cursor 并校验 workspace（需求 10.2）
        refreshWorkspaceOpen()
        // 打开详情时读取一次会话统计（运行中的泊位另由后端 30 秒定时刷新）
        if (selectedId.length > 0)
            berth.refreshSessionStats(selectedId)
        logView.text = ""
        terminal.refresh(true)
    }
    Connections {
        target: berth.instances
        function onDataChanged() { pane.syncForm() }
    }
}
