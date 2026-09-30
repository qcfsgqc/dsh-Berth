import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Dialogs
import QtQuick.Layouts

// 泊位编辑界面的图标、分组、标签、备注（需求 9.1–9.3、9.6）。
// load(item) 载入已保存值；values() 取当前编辑值交给 berth.setInstanceMeta。
GridLayout {
    id: root

    columns: 2
    columnSpacing: 12
    rowSpacing: 8

    property string iconKind: "none"
    // file：已保存的相对路径，或新选图片的 file:/// URL（保存时才复制）
    property string iconValue: ""
    property var tags: []
    // 用户改过任一字段后为 true；load() 清零。状态刷新时据此不覆盖未保存的编辑
    property bool dirty: false
    // 最近一次被拒绝的输入原因（添加标签、选图片、保存）
    property string error: ""

    // 分组名/标签/备注实时校验
    readonly property var liveCheck: berth.checkInstanceMeta(groupField.text, root.tags, notesArea.text)
    readonly property string shownError: error.length ? error : (liveCheck.ok ? "" : liveCheck.error)
    readonly property bool valid: liveCheck.ok

    function load(item) {
        iconKind = item.iconKind || "none"
        iconValue = item.iconValue || ""
        groupField.text = item.group || ""
        tags = item.tags ? item.tags.slice() : []
        notesArea.text = item.notes || ""
        tagInput.text = ""
        error = ""
        dirty = false
    }

    function values() {
        return { iconKind: iconKind, iconValue: iconValue, group: groupField.text,
                 tags: tags.slice(), notes: notesArea.text }
    }

    function addTag() {
        const r = berth.checkTag(tags, tagInput.text)
        if (!r.ok) {
            error = r.error
            return
        }
        tags = tags.concat([tagInput.text.trim()])
        tagInput.text = ""
        error = ""
        dirty = true
    }

    function removeTag(i) {
        const t = tags.slice()
        t.splice(i, 1)
        tags = t
        error = ""
        dirty = true
    }

    // —— 图标 ——
    Label { text: "图标" }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        BerthIcon {
            kind: root.iconKind
            value: root.iconValue
            size: 24
            showDefaultForNone: true
        }
        ComboBox {
            id: kindBox
            model: ["无图标", "内置图标", "本地图片"]
            readonly property var kinds: ["none", "builtin", "file"]
            // 用户选择会打断普通绑定，故用 Binding
            Binding on currentIndex { value: Math.max(0, kindBox.kinds.indexOf(root.iconKind)) }
            onActivated: index => {
                const k = kinds[index]
                if (k === "file") {
                    // 通过校验后才切换，失败时保留原图标设置（需求 9.3）
                    imageDialog.open()
                    currentIndex = Math.max(0, kinds.indexOf(root.iconKind))
                    return
                }
                if (k === root.iconKind)
                    return
                root.iconKind = k
                root.iconValue = k === "builtin" ? builtinProbe.builtinNames[0] : ""
                root.error = ""
                root.dirty = true
            }
        }
        // 内置图标集
        Repeater {
            model: root.iconKind === "builtin" ? builtinProbe.builtinNames : []
            delegate: ToolButton {
                required property string modelData
                checkable: true
                checked: root.iconValue === modelData
                implicitWidth: 30
                implicitHeight: 30
                contentItem: BerthIcon { kind: "builtin"; value: modelData; size: 18 }
                onClicked: {
                    root.iconValue = modelData
                    root.dirty = true
                }
            }
        }
        Button {
            visible: root.iconKind === "file"
            text: "更换图片…"
            onClicked: imageDialog.open()
        }
        Item { Layout.fillWidth: true }
    }

    // —— 分组 ——
    Label { text: "分组" }
    TextField {
        id: groupField
        Layout.fillWidth: true
        placeholderText: "留空则归入「未分组」"
        onTextEdited: { root.error = ""; root.dirty = true }
    }

    // —— 标签 ——
    Label { text: "标签"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 4 }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4
        Flow {
            Layout.fillWidth: true
            spacing: 4
            visible: root.tags.length > 0
            Repeater {
                model: root.tags
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    radius: 10
                    color: "#eef4fc"
                    border.color: "#cce4f7"
                    height: 22
                    width: chipRow.implicitWidth + 12
                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 4
                        Label { text: modelData; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                        Label {
                            text: "×"
                            color: "#666666"
                            font.pixelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -3
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.removeTag(index)
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: tagInput
                Layout.fillWidth: true
                placeholderText: "输入标签后回车或点「添加」"
                onAccepted: root.addTag()
                onTextEdited: root.error = ""
            }
            Button { text: "添加"; onClicked: root.addTag() }
        }
    }

    // —— 备注 ——
    Label { text: "备注"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 4 }
    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: 72
        TextArea {
            id: notesArea
            wrapMode: TextArea.Wrap
            placeholderText: "多行备注（最长 2000 字符）"
            onTextChanged: if (activeFocus) { root.error = ""; root.dirty = true }
        }
    }

    Item { visible: root.shownError.length > 0 }
    Label {
        Layout.fillWidth: true
        visible: root.shownError.length > 0
        text: root.shownError
        color: "#c42b1c"
        wrapMode: Text.Wrap
    }

    // 只用于取内置图标名列表
    BerthIcon { id: builtinProbe; visible: false; Layout.preferredWidth: 0; Layout.preferredHeight: 0 }

    FileDialog {
        id: imageDialog
        title: "选择图标图片"
        fileMode: FileDialog.OpenFile
        nameFilters: ["图片 (*.png *.jpg *.jpeg *.ico *.svg)", "所有文件 (*)"]
        onAccepted: {
            const url = selectedFile.toString()
            const r = berth.checkImage(url)
            if (!r.ok) {
                root.error = r.error
                return
            }
            root.iconKind = "file"
            root.iconValue = url
            root.error = ""
            root.dirty = true
        }
    }
}
