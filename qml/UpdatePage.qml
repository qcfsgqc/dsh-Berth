import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 更新面板（需求 14）：左栏 dsh（System_Dsh + 版本仓库各版本），右栏插件（按 DSH_HOME/profile 分组）。
// 每项显示当前/最新版本、最近检查时间与状态；有更新时可单项升级；"一键升级"只处理有更新的项。
// 检查或升级进行中时禁用对应按钮与"一键升级"。数据来自 berth.updates（Update_Center）。
Item {
    id: page

    readonly property var updates: berth.updates
    readonly property bool busy: updates.checking || updates.upgrading
    property bool resultOk: true
    property string resultText: ""

    readonly property var dshItems: updates.items.filter(function(it) { return it.section === "dsh" })
    readonly property var pluginItems: updates.items.filter(function(it) { return it.section === "plugin" })

    function stateColor(state) {
        switch (state) {
        case "hasUpdate": return "#0067c0"
        case "upToDate":
        case "latestInstalled": return "#107c10"
        case "checking":
        case "upgrading": return "#0067c0"
        case "failed": return "#c42b1c"
        case "incomparable": return "#9d5d00"
        default: return "#666666"
        }
    }

    function doUpgrade(item) {
        const r = updates.upgrade(item.id)
        resultOk = r.ok === true
        resultText = r.ok ? ("正在升级 " + item.label + "…") : (r.error || "无法升级")
    }

    function doUpgradeAll() {
        const r = updates.upgradeAll()
        resultOk = r.ok === true
        resultText = r.ok ? ("正在依次升级 " + r.total + " 项…") : (r.error || "无法升级")
    }

    Connections {
        target: page.updates
        function onUpgradeFinished(itemId, label, ok, detail, newVersion) {
            page.resultOk = ok
            page.resultText = ok ? (label + " 已升级到 " + newVersion) : (label + " 升级失败：" + detail)
        }
        function onUpgradeAllFinished(summary) {
            page.resultOk = summary.failed === 0
            let text = "一键升级完成：成功 " + summary.succeeded + " 项，失败 " + summary.failed + " 项"
            if (summary.failed > 0 && summary.failedNames && summary.failedNames.length)
                text += "（" + summary.failedNames.join("、") + "）"
            page.resultText = text
        }
        function onCheckFinished(updates, failed) {
            page.resultOk = failed === 0
            page.resultText = "检查完成：" + updates + " 项可更新" + (failed > 0 ? ("，" + failed + " 项检查失败") : "")
        }
    }

    // 单个条目行；groupTitle 非空时在行上方显示分组标题（插件栏每组第一项）
    component UpdateRow: Item {
        id: row
        required property var modelData
        property string groupTitle: ""
        width: ListView.view ? ListView.view.width : implicitWidth
        height: card.y + card.height

        Label {
            id: groupLabel
            visible: row.groupTitle.length > 0
            text: row.groupTitle
            color: "#555555"
            font.pixelSize: 12
            topPadding: 4
            bottomPadding: 4
        }

        Rectangle {
        id: card
        y: groupLabel.visible ? groupLabel.height : 0
        width: parent.width
        height: rowLayout.implicitHeight + 16
        radius: 4
        color: "#ffffff"
        border.color: "#e5e5e5"

        RowLayout {
            id: rowLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    spacing: 8
                    Label {
                        text: row.modelData.label
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.maximumWidth: rowLayout.width - 140
                    }
                    Label {
                        text: row.modelData.stateText
                        color: page.stateColor(row.modelData.state)
                    }
                }
                Label {
                    text: "当前 " + (row.modelData.current || "—") + "　最新 " + (row.modelData.latest || "—")
                    color: "#333333"
                    font.pixelSize: 12
                }
                Label {
                    text: (row.modelData.lastChecked || "").length ? ("检查于 " + row.modelData.lastCheckedText) : "未检查"
                    color: "#666666"
                    font.pixelSize: 11
                }
                Label {
                    visible: (row.modelData.error || "").length > 0
                    text: "原因：" + row.modelData.error
                    color: "#c42b1c"
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
            Button {
                Layout.alignment: Qt.AlignTop
                visible: row.modelData.hasUpdate || row.modelData.state === "upgrading"
                text: row.modelData.state === "upgrading" ? "升级中…" : "升级"
                enabled: row.modelData.hasUpdate && !row.modelData.busy && !page.busy
                onClicked: page.doUpgrade(row.modelData)
            }
        }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Label {
            text: "更新"
            font.pixelSize: 20
            font.bold: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                text: page.updates.checking ? "检查中…" : "检查更新"
                enabled: !page.busy
                onClicked: page.updates.checkAll()
            }
            Button {
                text: page.updates.upgrading ? "升级中…" : ("一键升级" + (page.updates.updateCount > 0 ? "（" + page.updates.updateCount + "）" : ""))
                enabled: page.updates.canUpgradeAll
                onClicked: page.doUpgradeAll()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: page.updates.lastCheckText.length ? ("最近检查：" + page.updates.lastCheckText) : "尚未检查"
                color: "#666666"
                font.pixelSize: 12
            }
        }
        Label {
            visible: page.resultText.length > 0
            text: page.resultText
            color: page.resultOk ? "#107c10" : "#c42b1c"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // 左栏：dsh
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 6
                Label { text: "dsh"; font.bold: true; font.pixelSize: 14 }
                ListView {
                    id: dshList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: page.dshItems
                    ScrollBar.vertical: ScrollBar {}
                    delegate: UpdateRow {}
                }
            }

            // 右栏：插件（按 DSH_HOME/profile 分组）
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 6
                Label { text: "插件"; font.bold: true; font.pixelSize: 14 }
                Label {
                    visible: page.pluginItems.length === 0
                    text: "没有已安装的插件"
                    color: "#666666"
                }
                ListView {
                    id: pluginList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: page.pluginItems
                    ScrollBar.vertical: ScrollBar {}
                    delegate: UpdateRow {
                        required property int index
                        groupTitle: index === 0 || page.pluginItems[index - 1].group !== modelData.group
                                    ? (modelData.groupLabel || "") : ""
                    }
                }
            }
        }
    }
}
