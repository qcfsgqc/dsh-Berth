import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 新建 / 重命名 / 复制 profile
// mode："create"（新建）、"rename"（重命名）、"copy"（复制）
Dialog {
    id: dlg

    property string mode: "create"
    // rename / copy 模式下要操作的 profile 名；操作所在的 DSH_HOME
    property string profileName: ""
    property string home: ""

    // 返回错误文案；空串表示通过
    function validateProfileName(text) {
        const t = text.trim()
        if (t.length === 0)
            return "请输入 profile 名称"
        if (t.indexOf("/") >= 0 || t.indexOf("\\") >= 0)
            return "名称不能包含 / 或 \\"
        if (t === "." || t === "..")
            return "名称不能使用相对目录引用"
        if (t.toLowerCase() === "node_modules")
            return "不能使用保留名 node_modules"
        if (t.toLowerCase() === "desktop")
            return "不能使用保留名 desktop"
        return ""
    }

    title: mode === "rename" ? "重命名 profile"
         : mode === "copy" ? "复制 profile"
         : "新建 profile"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(460, parent ? parent.width - 48 : 460)
    standardButtons: Dialog.NoButton

    // 实时校验结果（非空即错误文案）
    property string nameError: validateProfileName(nameField.text)

    onOpened: {
        nameField.text = mode === "rename" ? profileName
                       : mode === "copy" ? profileName + " - 副本"
                       : ""
        tplBox.currentIndex = 0
        if (nameField.text.length === 0)
            Qt.callLater(nameField.forceActiveFocus)
    }

    footer: DialogButtonBox {
        Button {
            text: dlg.mode === "rename" ? "重命名"
                : dlg.mode === "copy" ? "复制"
                : "新建"
            flat: true
            highlighted: true
            enabled: dlg.nameError.length === 0
            onClicked: dlg.accept()
        }
        Button {
            text: "取消"
            flat: true
            onClicked: dlg.reject()
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        Label {
            visible: dlg.mode === "rename"
            text: "将把 profile「" + dlg.profileName + "」改为："
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            visible: dlg.mode === "copy"
            text: "将复制 profile「" + dlg.profileName + "」为："
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "profile 名称"
        }

        Label {
            id: errorLabel
            text: dlg.nameError
            visible: text.length > 0
            color: "#c42b1c"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // 仅新建模式：选择初始化模板
        ColumnLayout {
            visible: dlg.mode === "create"
            spacing: 4
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                Label { text: "模板" }
                ComboBox {
                    id: tplBox
                    Layout.fillWidth: true
                    editable: false
                    model: ["web", "acp", "headless", "sdk", "sdk-minimal"]
                }
            }
            Label {
                text: "将以所选模板初始化 dsh.profile.bundles（package.json、cordis.patch.yml、pnpm-workspace.yaml）"
                color: "#555555"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        Label {
            visible: dlg.mode === "rename"
            text: "重命名目录并同步更新引用它的泊位"
            color: "#555555"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            visible: dlg.mode === "copy"
            text: "只拷贝 package.json、pnpm-lock.yaml、cordis.patch.yml、pnpm-workspace.yaml；依赖通过 pnpm install 重建"
            color: "#555555"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    onAccepted: {
        const name = nameField.text.trim()
        if (validateProfileName(name) !== "")
            return
        if (dlg.mode === "rename") {
            berth.renameProfile(dlg.home, dlg.profileName, name)
        } else if (dlg.mode === "copy") {
            berth.copyProfile(dlg.home, dlg.profileName, name)
        } else {
            berth.createProfile(name, tplBox.currentText)
        }
    }
}
