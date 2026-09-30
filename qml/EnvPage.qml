import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts
import QtQuick.Dialogs

// 环境检测：Node / npm / pnpm / Git / dsh / WebView2 的状态、版本、路径；
// 缺失或版本过低时给出下载链接与安装指引；winget 可用时"缺失"项可一键安装（先确认完整命令）；
// 导出诊断报告（后端脱敏）。检测在后台进行，检测期间界面可操作。
Item {
    id: page

    readonly property var env: berth.env
    // 页面级结果提示（安装启动失败、导出结果等）
    property bool resultOk: true
    property string resultText: ""
    // 待确认一键安装的项
    property string pendingInstallId: ""
    property string pendingInstallName: ""
    property string pendingInstallCommand: ""

    // 打开环境页时若从未检测过（且没在检测），在后台检测一次
    onVisibleChanged: {
        if (visible && !env.checkedOnce && !env.checking)
            env.checkAll()
    }

    function statusColor(status) {
        switch (status) {
        case "ok": return "#107c10"
        case "checking": return "#0067c0"
        case "missing": return "#c42b1c"
        case "outdated": return "#9d5d00"
        case "failed": return "#c42b1c"
        default: return "#666666"
        }
    }

    function askInstall(item) {
        const cmd = env.installCommand(item.id)
        if (!cmd.length) {
            resultOk = false
            resultText = item.name + " 不支持一键安装"
            return
        }
        pendingInstallId = item.id
        pendingInstallName = item.name
        pendingInstallCommand = cmd
        installConfirmDialog.open()
    }

    function doInstall() {
        const id = pendingInstallId
        const name = pendingInstallName
        pendingInstallId = ""
        if (!id.length)
            return
        const r = env.install(id)
        resultOk = r.ok === true
        resultText = r.ok ? ("正在安装 " + name + "…") : r.error
    }

    function doExport(path) {
        const r = env.exportReport(path)
        if (!r.ok) {
            resultOk = false
            resultText = r.error
        } else if (r.pending) {
            resultOk = true
            resultText = "正在检测环境，完成后写出诊断报告…"
        }
    }

    Connections {
        target: page.env
        function onInstallFinished(id, ok, exitCode, message) {
            page.resultOk = ok
            page.resultText = ok ? "安装完成，已重新检测该项。" : ("安装失败（退出码 " + exitCode + "），详见对应项。")
        }
        function onReportExported(ok, path, message) {
            page.resultOk = ok
            page.resultText = ok ? ("诊断报告已导出：" + path) : ("导出失败：" + message)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Label {
            text: "环境"
            font.pixelSize: 20
            font.bold: true
        }
        Label {
            text: "检测运行 dsh 所需的工具。检测在后台进行，每项最多 10 秒。"
            color: "#555555"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                text: page.env.checking ? "检测中…" : "重新检测"
                enabled: !page.env.checking
                onClicked: page.env.checkAll()
            }
            Button {
                text: page.env.exporting ? "正在导出…" : "导出诊断报告"
                enabled: !page.env.exporting
                onClicked: exportDialog.open()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: page.env.wingetChecking ? "winget：检测中…"
                      : page.env.wingetAvailable ? ("winget：" + page.env.wingetVersion)
                      : "winget：不可用（无法一键安装）"
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

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: page.env.items
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property string status: modelData.status
                readonly property bool problem: status === "missing" || status === "outdated"
                width: list.width
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
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        RowLayout {
                            spacing: 8
                            Label {
                                text: row.modelData.name
                                font.bold: true
                                font.pixelSize: 14
                            }
                            Label {
                                text: row.modelData.statusText
                                color: page.statusColor(row.status)
                            }
                            Label {
                                visible: (row.modelData.version || "").length > 0
                                text: row.modelData.version
                                color: "#333333"
                            }
                        }
                        Label {
                            visible: (row.modelData.path || "").length > 0
                            text: row.modelData.path
                            color: "#555555"
                            font.pixelSize: 12
                            wrapMode: Text.WrapAnywhere
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: row.status === "outdated"
                            text: "当前 " + (row.modelData.version || "未知") + "，最低要求 " + (row.modelData.minVersion || "")
                            color: "#9d5d00"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: row.status === "failed" && (row.modelData.reason || "").length > 0
                            text: "原因：" + row.modelData.reason
                            color: "#c42b1c"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: row.problem && (row.modelData.guide || "").length > 0
                            text: row.modelData.guide
                            color: "#555555"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: row.problem && (row.modelData.url || "").length > 0
                            text: "<a href=\"" + row.modelData.url + "\">" + row.modelData.url + "</a>"
                            textFormat: Text.RichText
                            font.pixelSize: 12
                            wrapMode: Text.WrapAnywhere
                            Layout.fillWidth: true
                            onLinkActivated: function(link) { Qt.openUrlExternally(link) }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                        }
                        Label {
                            visible: row.modelData.installing
                            text: "正在安装…"
                            color: "#0067c0"
                            font.pixelSize: 12
                        }
                        Label {
                            visible: row.modelData.hasInstallResult && !row.modelData.installing
                            text: row.modelData.installOk
                                  ? "上次一键安装成功"
                                  : ("上次一键安装失败，退出码 " + row.modelData.installCode
                                     + ((row.modelData.installMessage || "").length ? "\n" + row.modelData.installMessage : ""))
                            color: row.modelData.installOk ? "#107c10" : "#c42b1c"
                            font.pixelSize: 12
                            font.family: row.modelData.installOk ? "" : "Consolas"
                            wrapMode: Text.WrapAnywhere
                            Layout.fillWidth: true
                        }
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        spacing: 4
                        Button {
                            visible: row.status === "missing" && page.env.wingetAvailable
                            text: "一键安装"
                            enabled: row.modelData.canInstall && !row.modelData.installing
                            Layout.fillWidth: true
                            onClicked: page.askInstall(row.modelData)
                        }
                        Button {
                            text: "重测"
                            enabled: row.status !== "checking" && !row.modelData.installing
                            Layout.fillWidth: true
                            onClicked: page.env.checkItem(row.modelData.id)
                        }
                    }
                }
            }
        }
    }

    // 一键安装确认：列出完整命令，确认后才执行，取消不执行
    Dialog {
        id: installConfirmDialog
        title: "一键安装 " + page.pendingInstallName
        modal: true
        anchors.centerIn: parent
        width: Math.min(page.width - 80, 560)
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            width: parent.width
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "将执行以下命令："
            }
            TextArea {
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                font.family: "Consolas"
                text: page.pendingInstallCommand
            }
        }
        onAccepted: page.doInstall()
        onRejected: page.pendingInstallId = ""
    }

    FileDialog {
        id: exportDialog
        title: "导出诊断报告"
        fileMode: FileDialog.SaveFile
        nameFilters: ["文本文件 (*.txt)", "所有文件 (*)"]
        defaultSuffix: "txt"
        onAccepted: page.doExport(selectedFile.toString())
    }
}
