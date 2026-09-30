import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 多版本 dsh 管理：安装指定版本、列出已安装版本（SemVer 降序）与引用计数、删除未被引用的版本。
// 安装中的版本单独列出并显示"安装中"，同一版本号的再次安装与删除被禁用。
Item {
    id: page

    readonly property var store: berth.versions
    // 最近一次操作的结果提示
    property bool resultOk: true
    property string resultText: ""
    // 待确认删除的版本号
    property string pendingRemove: ""

    // 列表行：安装中（尚未完成）的版本在前，其后为已安装版本（后端已按 SemVer 降序）
    readonly property var rows: {
        const installed = store.versions
        const busy = store.installing
        const out = []
        for (let i = 0; i < busy.length; ++i) {
            let done = false
            for (let j = 0; j < installed.length; ++j)
                if (installed[j].version === busy[i]) { done = true; break }
            if (!done)
                out.push({ version: busy[i], path: "", refCount: 0, installing: true })
        }
        for (let k = 0; k < installed.length; ++k) {
            const e = installed[k]
            out.push({ version: e.version, path: e.path, refCount: e.refCount,
                       installing: busy.indexOf(e.version) >= 0 })
        }
        return out
    }

    function startInstall() {
        const ver = versionField.text.trim()
        if (!ver.length)
            return
        const r = store.install(ver)
        resultOk = r.ok === true
        resultText = r.ok ? ("正在安装 dsh " + r.version + "…") : r.error
        if (r.ok)
            versionField.text = ""
    }

    function doRemove(ver) {
        const r = store.remove(ver)
        resultOk = r.ok === true
        if (r.ok)
            resultText = "已删除 dsh " + ver
        else
            resultText = r.error
    }

    Connections {
        target: page.store
        function onInstallFinished(version, ok, message) {
            page.resultOk = ok
            page.resultText = message
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Label {
            text: "dsh 版本"
            font.pixelSize: 20
            font.bold: true
        }
        Label {
            text: "每个版本安装在独立目录，泊位可在编辑界面绑定其中一个；系统 dsh 不受影响。"
            color: "#555555"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            text: "安装目录：" + (page.store.rootDir.length ? page.store.rootDir : "（未知）")
            color: "#666666"
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: versionField
                Layout.preferredWidth: 220
                placeholderText: "版本号，如 1.2.3"
                onAccepted: page.startInstall()
            }
            Button {
                text: "安装"
                // 同一版本号正在安装或已安装时禁用（后端也会拒绝）
                enabled: {
                    const ver = versionField.text.trim()
                    page.store.installing
                    page.store.versions
                    return ver.length > 0 && !page.store.isInstalling(ver) && !page.store.isInstalled(ver)
                }
                onClicked: page.startInstall()
            }
            Button {
                text: "刷新"
                onClicked: page.store.refresh()
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            visible: page.resultText.length > 0
            text: page.resultText
            color: page.resultOk ? "#107c10" : "#c42b1c"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            visible: page.rows.length === 0
            text: "还没有安装任何版本。"
            color: "#666666"
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: page.rows
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                width: list.width
                height: rowLayout.implicitHeight + 16
                radius: 4
                color: "#ffffff"
                border.color: "#e5e5e5"

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: 8
                            Label {
                                text: row.modelData.version
                                font.bold: true
                                font.pixelSize: 14
                            }
                            Label {
                                visible: row.modelData.installing
                                text: "安装中…"
                                color: "#0067c0"
                            }
                        }
                        Label {
                            visible: row.modelData.path.length > 0
                            text: row.modelData.path
                            color: "#555555"
                            font.pixelSize: 12
                            wrapMode: Text.WrapAnywhere
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: !row.modelData.installing || row.modelData.path.length > 0
                            text: row.modelData.refCount > 0
                                  ? ("被 " + row.modelData.refCount + " 个泊位使用")
                                  : "未被泊位使用"
                            color: "#666666"
                            font.pixelSize: 12
                        }
                    }
                    Button {
                        text: "删除"
                        // 安装中禁用；被引用时仍可点击，由后端拒绝并列出泊位名
                        enabled: !row.modelData.installing && row.modelData.path.length > 0
                        onClicked: {
                            // 被引用的版本直接交给后端拒绝（提示中列出泊位名），不弹确认
                            if (row.modelData.refCount > 0) {
                                page.doRemove(row.modelData.version)
                                return
                            }
                            page.pendingRemove = row.modelData.version
                            confirmDialog.open()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: confirmDialog
        title: "删除 dsh 版本"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            text: "确定删除 dsh " + page.pendingRemove + " 吗？该版本目录将被删除。"
            wrapMode: Text.Wrap
        }
        onAccepted: {
            page.doRemove(page.pendingRemove)
            page.pendingRemove = ""
        }
        onRejected: page.pendingRemove = ""
    }
}
