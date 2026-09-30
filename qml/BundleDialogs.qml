import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts
import QtQuick.Dialogs

// 泊位 / profile 整包导出与导入（berth.bundles）的全部对话框：
// - 导出：泊位含敏感 env 时先问\"包含敏感值\"（默认不勾选），再选保存位置
// - 导入：选文件 → 选目标 DSH_HOME → inspect 检测冲突（不写数据）→ 重命名 profile / 自动分配端口 / 取消
// - 结果：列出需要补填的敏感键；依赖重建失败时显示错误摘要并可重试
Item {
    id: root

    readonly property var io: berth.bundles

    // 导出上下文
    property string exportKind: ""      // "instance" | "profile"
    property string exportId: ""
    property string exportHome: ""
    property string exportProfile: ""
    property bool exportSecrets: false

    // 导入上下文
    property string importPath: ""
    property var info: null             // inspected 结果；null 表示检查中
    property bool importing: false

    // —— 对外入口 ——
    function exportInstance(id) {
        exportKind = "instance"
        exportId = id
        exportSecrets = false
        if (io.hasSecrets(id)) {
            secretsBox.checked = false
            exportOptionsDialog.open()
        } else {
            saveDialog.open()
        }
    }

    function exportProfile(home, profile) {
        exportKind = "profile"
        exportHome = home
        exportProfile = profile
        exportSecrets = false
        saveDialog.open()
    }

    // home：目标 DSH_HOME 的初始值（空为默认）
    function openImport(home) {
        importHomeField.text = home || ""
        openDialog.open()
    }

    // 依赖未就绪条目：按泊位 id 或 (home, profile) 查找；没有返回 null
    function pendingFor(instanceId, home, profile) {
        const map = io.depsPending
        for (const k in map) {
            const e = map[k]
            if (instanceId && e.instanceId === instanceId)
                return e
            if (!instanceId && profile && e.profile === profile && (!home || e.home === home))
                return e
        }
        return null
    }

    function retry(key) {
        const r = io.retryDeps(key)
        if (!r.ok)
            showMessage("重试失败", r.error)
    }

    function showMessage(title, text) {
        messageDialog.title = title
        messageDialog.text = text
        messageDialog.open()
    }

    function runInspect() {
        info = null
        const r = io.inspect(importPath, importHomeField.text)
        if (!r.ok)
            info = { ok: false, error: r.error }
    }

    // 当前冲突是否都已解决
    readonly property bool needRename: !!info && info.ok && (info.profileConflict || info.profileNameError.length > 0)
    readonly property bool needPort: !!info && info.ok && info.portConflict
    readonly property string renameError: needRename ? io.checkProfileName(importHomeField.text, renameField.text) : ""
    readonly property bool canImport: !!info && info.ok && !importing && !io.busy
                                      && (!needRename || renameError.length === 0)
                                      && (!needPort || autoPortBox.checked)

    // —— 导出 ——
    Dialog {
        id: exportOptionsDialog
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 48 : 440)
        title: "导出泊位"
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "该泊位含有标记为敏感的环境变量。不勾选时只导出键名与敏感标记。"
            }
            CheckBox { id: secretsBox; text: "包含敏感值"; checked: false }
            Label {
                Layout.fillWidth: true
                visible: secretsBox.checked
                wrapMode: Text.Wrap
                color: "#b45309"
                text: "敏感值将以明文写入导出文件，请妥善保管。"
            }
        }
        onAccepted: {
            root.exportSecrets = secretsBox.checked
            saveDialog.open()
        }
    }

    FileDialog {
        id: saveDialog
        title: root.exportKind === "instance" ? "导出泊位" : "导出 profile"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Berth Bundle (*.berthbundle)", "所有文件 (*)"]
        defaultSuffix: "berthbundle"
        onAccepted: {
            const path = selectedFile.toString()
            const r = root.exportKind === "instance"
                    ? root.io.exportInstance(root.exportId, path, root.exportSecrets)
                    : root.io.exportProfile(root.exportHome, root.exportProfile, path)
            if (!r.ok)
                root.showMessage("导出", "导出失败：" + r.error)
        }
    }

    // —— 导入 ——
    FileDialog {
        id: openDialog
        title: "选择要导入的 Bundle"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Berth Bundle (*.berthbundle)", "所有文件 (*)"]
        onAccepted: {
            root.importPath = selectedFile.toString()
            root.importing = false
            importDialog.open()
            root.runInspect()
        }
    }

    Dialog {
        id: importDialog
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, parent ? parent.width - 48 : 520)
        title: "导入 Bundle"
        closePolicy: root.importing ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

        contentItem: ColumnLayout {
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: root.importPath.replace(/^file:\/\/\//, "")
                color: "#555555"
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "目标 DSH_HOME" }
                TextField {
                    id: importHomeField
                    Layout.fillWidth: true
                    enabled: !root.importing
                    placeholderText: "留空则用默认 ~/.dsh"
                    onEditingFinished: if (importDialog.visible) root.runInspect()
                }
            }

            Label {
                visible: !root.info
                text: "正在检查…"
                color: "#666666"
            }
            Label {
                Layout.fillWidth: true
                visible: !!root.info && !root.info.ok
                wrapMode: Text.Wrap
                color: "#c42b1c"
                text: root.info && !root.info.ok ? "无法导入：" + root.info.error : ""
            }

            // 校验通过：摘要
            Label {
                Layout.fillWidth: true
                visible: !!root.info && root.info.ok
                wrapMode: Text.Wrap
                text: !root.info || !root.info.ok ? ""
                      : "类型：" + (root.info.type === "instance" ? "泊位" : "profile")
                        + "　profile：" + root.info.profileName
                        + (root.info.type === "instance" ? "　端口：" + root.info.port : "")
                        + "\n插件 " + root.info.pluginCount + " 个，配置文件 " + root.info.fileCount + " 个"
            }

            // 冲突：profile 重名或名称不合法 → 重命名
            Label {
                Layout.fillWidth: true
                visible: root.needRename
                wrapMode: Text.Wrap
                color: "#b45309"
                text: !root.needRename ? ""
                      : root.info.profileConflict ? "目标 DSH_HOME 下已存在同名 profile「" + root.info.profileName + "」，请重命名："
                                                  : "Bundle 中的 profile 名不可用（" + root.info.profileNameError + "），请重命名："
            }
            TextField {
                id: renameField
                Layout.fillWidth: true
                visible: root.needRename
                enabled: !root.importing
                placeholderText: "新的 profile 名"
            }
            Label {
                Layout.fillWidth: true
                visible: root.needRename && root.renameError.length > 0
                wrapMode: Text.Wrap
                color: "#c42b1c"
                font.pixelSize: 12
                text: root.renameError
            }

            // 冲突：端口已被现有泊位使用 → 自动分配
            Label {
                Layout.fillWidth: true
                visible: root.needPort
                wrapMode: Text.Wrap
                color: "#b45309"
                text: root.needPort ? "端口 " + root.info.port + " 已被泊位使用：" + root.info.portUsers.join("、") : ""
            }
            CheckBox {
                id: autoPortBox
                visible: root.needPort
                enabled: !root.importing
                text: root.needPort && root.info.suggestedPort > 0
                      ? "自动分配端口（建议 " + root.info.suggestedPort + "）" : "自动分配端口"
            }

            Label {
                Layout.fillWidth: true
                visible: !!root.info && root.info.ok && root.info.missingSecrets.length > 0
                wrapMode: Text.Wrap
                color: "#555555"
                font.pixelSize: 12
                text: root.info && root.info.ok
                      ? "以下敏感变量未包含值，导入后需补填：" + root.info.missingSecrets.join("、") : ""
            }

            Label {
                visible: root.importing
                text: "正在导入并重建依赖…"
                color: "#666666"
            }
        }

        footer: DialogButtonBox {
            Button {
                text: "导入"
                enabled: root.canImport
                onClicked: {
                    const res = {}
                    if (root.needRename)
                        res.profileName = renameField.text
                    if (root.needPort)
                        res.autoPort = true
                    const r = root.io.importBundle(root.importPath, importHomeField.text, res)
                    if (!r.ok) {
                        root.showMessage("导入", "导入失败：" + r.error)
                        return
                    }
                    root.importing = true
                }
            }
            Button {
                text: "取消"
                enabled: !root.importing
                onClicked: importDialog.close()
            }
        }
    }

    // inspect 结果到达：预填重命名与端口选项
    onInfoChanged: {
        if (info && info.ok) {
            renameField.text = info.suggestedName || ""
            autoPortBox.checked = !!info.portConflict
        }
    }

    // —— 结果 ——
    Dialog {
        id: resultDialog
        property var ioResult: ({})
        property bool success: false
        property string message: ""
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, parent ? parent.width - 48 : 520)
        title: success ? "导入完成" : "导入失败"

        readonly property var pending: resultDialog.ioResult && resultDialog.ioResult.depsKey
                                       ? root.io.depsPending[resultDialog.ioResult.depsKey] : undefined

        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: resultDialog.success
                      ? "已导入 profile「" + (resultDialog.ioResult.profile || "") + "」"
                        + (resultDialog.ioResult.type === "instance" ? "，并新增泊位" : "")
                      : resultDialog.message
                color: resultDialog.success ? palette.text : "#c42b1c"
            }
            Label {
                Layout.fillWidth: true
                visible: resultDialog.success && !!resultDialog.ioResult.missingSecrets
                         && resultDialog.ioResult.missingSecrets.length > 0
                wrapMode: Text.Wrap
                color: "#b45309"
                text: resultDialog.ioResult.missingSecrets
                      ? "需要补填的敏感变量：" + resultDialog.ioResult.missingSecrets.join("、") : ""
            }
            // 依赖未就绪：错误摘要 + 重试（重试成功后条目从 depsPending 移除）
            Label {
                Layout.fillWidth: true
                visible: resultDialog.success && resultDialog.ioResult.depsReady === false
                wrapMode: Text.Wrap
                color: resultDialog.pending ? "#c42b1c" : "#107c10"
                text: resultDialog.pending
                      ? "依赖未就绪：" + (resultDialog.pending.error || "")
                      : "依赖已就绪"
            }
            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                visible: !!resultDialog.pending && (resultDialog.pending.tail || "").length > 0
                readOnly: true
                wrapMode: TextEdit.WrapAnywhere
                font.family: "Consolas"
                font.pixelSize: 12
                text: resultDialog.pending ? (resultDialog.pending.tail || "") : ""
            }
        }

        footer: DialogButtonBox {
            Button {
                text: root.io.busy ? "重试中…" : "重试"
                visible: !!resultDialog.pending
                enabled: !root.io.busy
                onClicked: root.retry(resultDialog.ioResult.depsKey)
            }
            Button {
                text: "关闭"
                onClicked: resultDialog.close()
            }
        }
    }

    Dialog {
        id: messageDialog
        property string text: ""
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 48 : 440)
        standardButtons: Dialog.Ok
        contentItem: Label {
            wrapMode: Text.Wrap
            text: messageDialog.text
        }
    }

    Connections {
        target: root.io
        function onExportFinished(ok, message) {
            root.showMessage("导出", ok ? "已导出到：" + message : message)
        }
        function onInspected(result) {
            if (importDialog.visible && !root.importing)
                root.info = result
        }
        function onImportFinished(ok, message, result) {
            if (!root.importing)
                return
            root.importing = false
            importDialog.close()
            resultDialog.success = ok
            resultDialog.message = message
            resultDialog.ioResult = result || {}
            resultDialog.open()
        }
        function onDepsRetryFinished(key, ok, message) {
            // 结果对话框打开时由其自身的 pending 绑定反映结果
            if (!resultDialog.visible)
                root.showMessage("依赖重建", ok ? "依赖已就绪" : message)
        }
    }
}
