import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 插件页：按 (DSH_HOME, profile) 筛选，启用开关、多选批量启用/禁用/卸载、单个卸载
Dialog {
    id: dlg

    // 筛选器选项：[{home, profile, label, key}]
    property var targets: []
    property int targetIndex: -1
    property string pluginHome: ""
    property string profileName: ""
    property bool showCore: false
    property bool loadOk: true
    property string loadError: ""
    property bool writable: false
    property var plugins: []
    // 多选，按选中顺序
    property var selected: []
    // 单个操作（开关/卸载）的结果提示
    property string resultText: ""
    property bool resultOk: true
    // 批量汇总
    property string summaryText: ""
    property bool summaryOk: true

    readonly property string curKey: profileName.length ? berth.plugins.keyOf(pluginHome, profileName) : ""
    readonly property var prog: curKey.length ? berth.plugins.progress[curKey] : undefined
    readonly property bool batchRunning: prog !== undefined
    // 批量进行中或单个卸载进行中时，本 profile 的开关、批量、卸载都不可用
    readonly property bool canWrite: loadOk && writable && !batchRunning && !berth.pluginBusy

    title: "插件"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(680, parent ? parent.width - 48 : 680)
    height: Math.min(560, parent ? parent.height - 48 : 560)
    standardButtons: Dialog.Close

    // 泊位入口：预选该泊位的 (home, profile)
    function openForInstance(id) {
        const item = berth.instance(id)
        openWith(item.dshHome || "", item.profile || "")
    }

    // profile 入口：预选该 (home, profile)
    function openForProfile(home, name) {
        openWith(home, name)
    }

    // 无上下文时默认选中第一个组合
    function openWith(home, name) {
        targets = berth.plugins.targets()
        let index = targets.length > 0 ? 0 : -1
        if (name.length) {
            const key = berth.plugins.keyOf(home, name)
            for (let i = 0; i < targets.length; ++i) {
                if (targets[i].key === key) {
                    index = i
                    break
                }
            }
        }
        selectTarget(index)
        market.init(targets, index)
        open()
        if (tabBar.currentIndex === 1)
            market.refresh()
    }

    function selectTarget(index) {
        targetIndex = index
        const t = index >= 0 && index < targets.length ? targets[index] : null
        pluginHome = t ? t.home : ""
        profileName = t ? t.profile : ""
        selected = []
        resultText = ""
        summaryText = ""
        reload()
    }

    function reload() {
        if (!profileName.length) {
            loadOk = true
            loadError = ""
            writable = false
            plugins = []
            selected = []
            return
        }
        const r = berth.plugins.list(pluginHome, profileName, showCore)
        loadOk = !!r.ok
        loadError = r.error || ""
        writable = !!r.writable
        plugins = r.plugins || []
        // 去掉已不在列表里的选中项（如关闭"显示核心包"后被隐藏的）
        const names = plugins.map(p => p.name)
        selected = selected.filter(n => names.indexOf(n) >= 0)
    }

    function toggleSelect(name, on) {
        const s = selected.slice()
        const i = s.indexOf(name)
        if (on && i < 0)
            s.push(name)
        else if (!on && i >= 0)
            s.splice(i, 1)
        selected = s
    }

    function runBatch(op) {
        summaryText = ""
        resultText = ""
        const r = berth.plugins.batch(pluginHome, profileName, op, selected)
        if (!r.ok) {
            summaryOk = false
            summaryText = r.error
        }
    }

    function opText(op) {
        return op === "enable" ? "批量启用" : op === "disable" ? "批量禁用"
             : op === "install" ? "安装" : "批量卸载"
    }

    Connections {
        target: berth
        function onPluginUninstallFinished(id, ok, message) {
            dlg.resultOk = ok
            dlg.resultText = message
            dlg.reload()
        }
    }

    Connections {
        target: berth.plugins
        function onBatchFinished(home, profile, summary) {
            if (berth.plugins.keyOf(home, profile) !== dlg.curKey)
                return
            let text = dlg.opText(summary.op) + "完成：成功 " + summary.succeeded + "，失败 " + summary.failed
            for (let i = 0; i < summary.failures.length; ++i)
                text += "\n" + summary.failures[i].name + "：" + summary.failures[i].reason
            dlg.summaryOk = summary.failed === 0
            dlg.summaryText = text
            dlg.selected = []
            dlg.reload()
        }
        function onPluginsChanged(home, profile) {
            if (dlg.visible && berth.plugins.keyOf(home, profile) === dlg.curKey && !dlg.batchRunning)
                dlg.reload()
        }
    }

    // 启动前自检新增隔离记录时刷新"已隔离"标记
    Connections {
        target: berth.quarantine
        function onChanged() {
            if (dlg.visible && !dlg.batchRunning)
                dlg.reload()
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            TabButton { text: "已安装" }
            TabButton { text: "市场" }
            // 切到市场时拉取目录
            onCurrentIndexChanged: if (currentIndex === 1) market.refresh()
        }

        StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: tabBar.currentIndex

        ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: "Profile" }
            ComboBox {
                id: targetBox
                Layout.fillWidth: true
                model: dlg.targets
                textRole: "label"
                currentIndex: dlg.targetIndex
                enabled: dlg.targets.length > 0
                onActivated: index => dlg.selectTarget(index)
            }
            CheckBox {
                text: "显示核心包"
                checked: dlg.showCore
                onToggled: {
                    dlg.showCore = checked
                    dlg.reload()
                }
            }
            BusyIndicator {
                visible: berth.pluginBusy || dlg.batchRunning
                running: visible
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }
            Button {
                text: "刷新"
                onClicked: dlg.reload()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "已选 " + dlg.selected.length + " 项"
                color: "#555555"
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "批量启用"
                enabled: dlg.canWrite && dlg.selected.length > 0
                onClicked: dlg.runBatch("enable")
            }
            Button {
                text: "批量禁用"
                enabled: dlg.canWrite && dlg.selected.length > 0
                onClicked: dlg.runBatch("disable")
            }
            Button {
                text: "批量卸载"
                enabled: dlg.canWrite && dlg.selected.length > 0
                onClicked: batchConfirm.open()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#e5e5e5"
            border.width: 1
            radius: 4

            ListView {
                id: pluginList
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                visible: dlg.loadOk && dlg.plugins.length > 0
                model: dlg.plugins
                spacing: 2
                delegate: Rectangle {
                    id: pluginRow
                    required property var modelData
                    width: pluginList.width
                    height: 40
                    radius: 4
                    color: rowHover.hovered ? "#eef4fc" : "transparent"
                    HoverHandler { id: rowHover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 8
                        spacing: 10
                        CheckBox {
                            checked: dlg.selected.indexOf(pluginRow.modelData.name) >= 0
                            enabled: dlg.canWrite
                            onToggled: dlg.toggleSelect(pluginRow.modelData.name, checked)
                            Accessible.name: "选择 " + pluginRow.modelData.name
                        }
                        Label {
                            text: pluginRow.modelData.name
                            font.pixelSize: 14
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: pluginRow.modelData.core
                            text: "核心"
                            color: "#0067c0"
                            font.pixelSize: 12
                        }
                        Label {
                            visible: !!pluginRow.modelData.quarantined
                            text: "已隔离" + (pluginRow.modelData.quarantineReason
                                           ? "：" + pluginRow.modelData.quarantineReason : "")
                            color: "#c42b1c"
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.maximumWidth: 220
                            ToolTip.visible: quarantineHover.hovered
                            ToolTip.text: text
                            HoverHandler { id: quarantineHover }
                        }
                        Button {
                            visible: !!pluginRow.modelData.quarantined
                            text: "解除隔离"
                            enabled: dlg.canWrite
                            onClicked: {
                                dlg.summaryText = ""
                                const r = berth.unquarantine(dlg.pluginHome, dlg.profileName,
                                                             pluginRow.modelData.name)
                                dlg.resultOk = !!r.ok
                                dlg.resultText = r.ok ? "已解除隔离并启用「" + pluginRow.modelData.name + "」"
                                                      : r.error
                                dlg.reload()
                            }
                        }
                        Label {
                            text: pluginRow.modelData.displayVersion
                            color: "#555555"
                            font.pixelSize: 12
                        }
                        Switch {
                            checked: pluginRow.modelData.enabled
                            enabled: dlg.canWrite
                            text: checked ? "启用" : "禁用"
                            Accessible.name: "启用 " + pluginRow.modelData.name
                            onToggled: {
                                dlg.summaryText = ""
                                const r = berth.plugins.setEnabled(dlg.pluginHome, dlg.profileName,
                                                                   pluginRow.modelData.name, checked)
                                dlg.resultOk = !!r.ok
                                dlg.resultText = r.ok ? "" : r.error
                                // 以文件为准重读：失败时开关退回操作前状态
                                dlg.reload()
                            }
                        }
                        Button {
                            text: "卸载"
                            enabled: dlg.canWrite
                            onClicked: {
                                confirm.pluginName = pluginRow.modelData.name
                                confirm.open()
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: dlg.loadOk && dlg.plugins.length === 0
                text: dlg.profileName.length ? "没有已安装的插件" : "没有可选的 profile"
                color: "#666666"
            }

            Label {
                anchors.fill: parent
                anchors.margins: 12
                visible: !dlg.loadOk
                text: "读取失败：" + dlg.loadError
                color: "#c42b1c"
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Label {
            visible: dlg.batchRunning
            text: dlg.batchRunning ? dlg.opText(dlg.prog.op) + "中：" + dlg.prog.done + "/" + dlg.prog.total : ""
            color: "#ca5010"
        }

        Label {
            visible: berth.pluginBusy
            text: "正在卸载，请稍候…"
            color: "#ca5010"
        }

        Label {
            visible: dlg.summaryText.length > 0 && !dlg.batchRunning
            text: dlg.summaryText
            color: dlg.summaryOk ? "#107c10" : "#c42b1c"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.maximumHeight: 140
            elide: Text.ElideRight
        }

        Label {
            visible: dlg.resultText.length > 0 && !berth.pluginBusy
            text: (dlg.resultOk ? "成功：" : "失败：") + dlg.resultText
            color: dlg.resultOk ? "#107c10" : "#c42b1c"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.maximumHeight: 140
            elide: Text.ElideRight
        }
        }

        MarketPage {
            id: market
        }
        }
    }

    // 批量卸载确认
    Dialog {
        id: batchConfirm
        title: "确认批量卸载"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 48 : 440)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: dlg.runBatch("uninstall")

        contentItem: Label {
            text: "从 profile「" + dlg.profileName + "」卸载选中的 " + dlg.selected.length
                  + " 个插件？会从配置移除并调用 pnpm 卸载插件包；失败的插件保持原样。"
            wrapMode: Text.Wrap
        }
    }

    // 单个卸载二级确认
    Dialog {
        id: confirm
        property string pluginName: ""

        title: "确认卸载"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 48 : 440)
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: removePackageBox.checked = false
        onAccepted: {
            dlg.resultText = ""
            dlg.summaryText = ""
            berth.uninstallPluginForProfile(dlg.pluginHome, dlg.profileName,
                                            pluginName, removePackageBox.checked)
        }

        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: "从 profile「" + dlg.profileName + "」卸载插件「" + confirm.pluginName + "」？"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            CheckBox {
                id: removePackageBox
                text: "同时卸载插件包"
                checked: false
            }
            Label {
                text: removePackageBox.checked
                      ? "会从配置移除，并调用 pnpm 卸载插件包及不再被使用的依赖。"
                      : "只从 dsh.profile.bundles 移除，插件包保留在 profile 中。"
                color: "#555555"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }
}
