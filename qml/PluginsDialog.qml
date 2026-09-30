import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 查看并卸载某个泊位 profile 里用户安装的插件
Dialog {
    id: dlg

    property string instanceId: ""
    property string profileName: ""
    // profile 模式使用：直接按 DSH_HOME + profile 名取插件，不经泊位
    property string pluginProfileName: ""
    property string pluginHome: ""
    property bool loadOk: true
    property string loadError: ""
    property var plugins: []
    property string resultText: ""
    property bool resultOk: true

    title: "插件" + (profileName.length ? "  ·  " + profileName : "")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 48 : 560)
    height: Math.min(460, parent ? parent.height - 48 : 460)
    standardButtons: Dialog.Close

    // 泊位模式打开（沿用原入口）
    function openForInstance(id) {
        instanceId = id
        pluginHome = ""
        pluginProfileName = ""
        open()
    }

    // profile 模式打开：不经泊位，直接按 home + profile 名管理插件
    function openForProfile(home, name) {
        instanceId = ""
        pluginHome = home
        pluginProfileName = name
        open()
    }

    function reload() {
        if (instanceId) {
            const item = berth.instance(instanceId)
            profileName = item.profile || ""
            const r = berth.listPlugins(instanceId)
            loadOk = !!r.ok
            loadError = r.error || ""
            plugins = r.plugins || []
            return
        }
        // profile 模式
        if (!pluginProfileName)
            return
        profileName = pluginProfileName
        const rp = berth.listPluginsForProfile(pluginHome, pluginProfileName)
        loadOk = !!rp.ok
        loadError = rp.error || ""
        plugins = rp.plugins || []
    }

    onOpened: {
        resultText = ""
        reload()
    }

    Connections {
        target: berth
        function onPluginUninstallFinished(id, ok, message) {
            // 泊位模式：原语义，id 必须等于当前 instanceId
            if (dlg.instanceId.length > 0 && id !== dlg.instanceId)
                return
            // profile 模式（instanceId 为空）：契约保证信号带空 id，直接处理
            dlg.resultOk = ok
            dlg.resultText = message
            dlg.reload()
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "已安装的插件（不含 dsh 自带 bundle）"
                color: "#555555"
            }
            Item { Layout.fillWidth: true }
            BusyIndicator {
                visible: berth.pluginBusy
                running: berth.pluginBusy
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }
            Button {
                text: "刷新"
                onClicked: dlg.reload()
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
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 10
                        Label {
                            text: pluginRow.modelData.name
                            font.pixelSize: 14
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: pluginRow.modelData.version || "—"
                            color: "#555555"
                            font.pixelSize: 12
                        }
                        Label {
                            text: pluginRow.modelData.enabled ? "已加载" : "未加载"
                            color: pluginRow.modelData.enabled ? "#107c10" : "#666666"
                            font.pixelSize: 12
                        }
                        Button {
                            text: "卸载"
                            enabled: !berth.pluginBusy
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
                text: "没有已安装的插件"
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
            visible: berth.pluginBusy
            text: "正在卸载，请稍候…"
            color: "#ca5010"
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

    // 卸载二级确认
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
            if (dlg.instanceId) {
                berth.uninstallPlugin(dlg.instanceId, pluginName, removePackageBox.checked)
            } else {
                berth.uninstallPluginForProfile(dlg.pluginHome, dlg.pluginProfileName,
                                                pluginName, removePackageBox.checked)
            }
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
