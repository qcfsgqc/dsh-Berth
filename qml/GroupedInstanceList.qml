import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 泊位按分组显示的可折叠列表 + 搜索框（需求 9.4、9.5）。
// 分组来自 berth.groupedInstances(keyword)：空白分组名归入「未分组」，搜索按名称或标签，隐藏空分组
ColumnLayout {
    id: root

    property string selectedId: ""
    signal instanceClicked(string id)

    // 生效的关键字（输入后 300ms 防抖）
    property string keyword: ""
    property var groups: []
    // 折叠状态：分组显示名 → true
    property var collapsed: ({})

    function statusColor(s) {
        if (s === "running" || s === "external") return "#107c10"
        if (s === "starting" || s === "stopping") return "#ca5010"
        if (s === "failed" || s === "crashStopped") return "#c42b1c"
        return "#666666"
    }

    // 顶部汇总面板的状态筛选（需求 24.5、24.6）：""、"running"（运行中/外部）、"failed"（失败/崩溃已停止）
    property string statusFilter: ""
    onStatusFilterChanged: reload()

    function matchesFilter(s) {
        if (statusFilter === "running") return s === "running" || s === "external"
        if (statusFilter === "failed") return s === "failed" || s === "crashStopped"
        return true
    }

    function reload() {
        const all = berth.groupedInstances(keyword)
        if (!statusFilter.length) {
            groups = all
            return
        }
        const out = []
        for (let i = 0; i < all.length; ++i) {
            const items = all[i].items.filter(function(it) { return root.matchesFilter(it.status) })
            if (items.length > 0)
                out.push(Object.assign({}, all[i], { items: items, count: items.length }))
        }
        groups = out
    }
    function scheduleReload() { Qt.callLater(root.reload) }

    // 分组内全部泊位 id（不受搜索过滤影响），按列表显示顺序
    function groupIds(name) {
        const all = berth.groupedInstances("")
        for (let i = 0; i < all.length; ++i) {
            if (all[i].name === name)
                return all[i].items.map(function(it) { return it.id })
        }
        return []
    }

    function toggle(name) {
        const c = Object.assign({}, collapsed)
        if (c[name]) delete c[name]; else c[name] = true
        collapsed = c
    }

    spacing: 6

    TextField {
        id: searchField
        Layout.fillWidth: true
        placeholderText: "搜索名称或标签"
        onTextChanged: debounce.restart()
    }
    Timer {
        id: debounce
        interval: 300
        onTriggered: {
            root.keyword = searchField.text
            root.reload()
        }
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 4
        model: root.groups
        ScrollBar.vertical: ScrollBar {}

        delegate: Column {
            id: groupDelegate
            required property var modelData
            readonly property bool isCollapsed: !!root.collapsed[modelData.name]
            width: list.width
            spacing: 2

            // 分组标题：分组名与泊位数量，点击折叠/展开；右侧为启动分组/停止分组（含「未分组」，需求 13.2）
            ItemDelegate {
                width: parent.width
                height: 30
                onClicked: root.toggle(groupDelegate.modelData.name)
                contentItem: RowLayout {
                    spacing: 6
                    Label {
                        text: groupDelegate.isCollapsed ? "▸" : "▾"
                        color: "#666666"
                        font.pixelSize: 10
                    }
                    Label {
                        Layout.fillWidth: true
                        text: groupDelegate.modelData.name
                        font.bold: true
                        color: groupDelegate.modelData.ungrouped ? "#666666" : root.palette.text
                        elide: Text.ElideRight
                    }
                    Label {
                        text: groupDelegate.modelData.count + " 台"
                        color: "#999999"
                        font.pixelSize: 11
                    }
                    ToolButton {
                        text: "▶"
                        font.pixelSize: 11
                        implicitWidth: 26
                        implicitHeight: 24
                        enabled: !berth.batch.busy && groupDelegate.modelData.count > 0
                        ToolTip.visible: hovered
                        ToolTip.text: "启动分组"
                        onClicked: berth.batch.startSelected(root.groupIds(groupDelegate.modelData.name))
                    }
                    ToolButton {
                        text: "■"
                        font.pixelSize: 11
                        implicitWidth: 26
                        implicitHeight: 24
                        enabled: !berth.batch.busy && groupDelegate.modelData.count > 0
                        ToolTip.visible: hovered
                        ToolTip.text: "停止分组"
                        onClicked: berth.batch.stopSelected(root.groupIds(groupDelegate.modelData.name))
                    }
                }
            }

            Repeater {
                model: groupDelegate.isCollapsed ? [] : groupDelegate.modelData.items
                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    readonly property bool isSelected: root.selectedId === modelData.id
                    width: groupDelegate.width
                    height: 48
                    highlighted: isSelected
                    onClicked: root.instanceClicked(modelData.id)
                    background: Rectangle {
                        radius: 4
                        color: row.isSelected ? "#cce4f7" : row.hovered ? "#eef4fc" : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        Item { Layout.preferredWidth: 8 }
                        BerthIcon {
                            kind: row.modelData.iconKind
                            value: row.modelData.iconValue
                            size: 20
                            showDefaultForNone: false
                        }
                        Column {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                width: parent.width
                                text: row.modelData.name
                                elide: Text.ElideRight
                            }
                            Row {
                                spacing: 6
                                Label {
                                    text: row.modelData.profile + "  ·  " + row.modelData.port + "  ·"
                                    color: "#555555"
                                    font.pixelSize: 12
                                }
                                Label {
                                    text: berth.statusText(row.modelData.status)
                                    color: root.statusColor(row.modelData.status)
                                    font.pixelSize: 12
                                    font.bold: row.modelData.status === "running"
                                }
                                Label {
                                    visible: row.modelData.tags.length > 0
                                    text: row.modelData.tags.join("、")
                                    color: "#0078d4"
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }
        }

        Label {
            anchors.centerIn: parent
            visible: root.groups.length === 0
            text: (root.keyword.trim().length || root.statusFilter.length) ? "没有匹配的泊位" : "还没有泊位"
            color: "#999999"
            font.pixelSize: 12
        }
    }

    // 模型任何变化（新增、删除、保存、状态）都重新分组
    Connections {
        target: berth.instances
        function onDataChanged() { root.scheduleReload() }
        function onRowsInserted() { root.scheduleReload() }
        function onRowsRemoved() { root.scheduleReload() }
        function onModelReset() { root.scheduleReload() }
    }
    Component.onCompleted: reload()
}
