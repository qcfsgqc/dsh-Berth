import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 泊位环境变量表（键、值、敏感）与额外启动参数列表（可排序）。
// 校验由 berth.checkEnvTable 实时给出：非法/重复行标红，DSH_HOME 行提示，冲突参数提示。
// 保存时由 berth.updateInstance 再校验一次；被拒绝时这里的内容保持不动。
ColumnLayout {
    id: editor
    spacing: 6

    readonly property int maxRows: 100
    readonly property int maxArgs: 50
    // 有未保存的编辑；为真时状态刷新（dataChanged）不覆盖表格
    property bool dirty: false
    property var check: ({ invalidRows: [], duplicateRows: [], dshHomeRows: [], conflicts: [],
                           tooManyRows: false, tooManyArgs: false, ok: true })

    ListModel { id: envModel }
    ListModel { id: argModel }

    function load(env, args) {
        envModel.clear()
        argModel.clear()
        const rows = env || []
        for (let i = 0; i < rows.length; ++i)
            envModel.append({ key: rows[i].key || "", value: rows[i].value || "", secret: rows[i].secret === true })
        const list = args || []
        for (let j = 0; j < list.length; ++j)
            argModel.append({ arg: String(list[j]) })
        dirty = false
        revalidate()
    }
    function envRows() {
        const out = []
        for (let i = 0; i < envModel.count; ++i) {
            const r = envModel.get(i)
            out.push({ key: r.key, value: r.value, secret: r.secret })
        }
        return out
    }
    function argList() {
        const out = []
        for (let i = 0; i < argModel.count; ++i)
            out.push(argModel.get(i).arg)
        return out
    }
    function revalidate() { check = berth.checkEnvTable(envRows(), argList()) }
    function touched() {
        dirty = true
        revalidate()
    }
    function rowIn(list, row) { return (list || []).indexOf(row) >= 0 }

    // —— 环境变量表 ——
    RowLayout {
        Layout.fillWidth: true
        Label { text: "环境变量"; font.bold: true }
        Label { text: envModel.count + " / " + editor.maxRows; color: "#888888"; font.pixelSize: 11 }
        Item { Layout.fillWidth: true }
        Button {
            text: "添加变量"
            enabled: envModel.count < editor.maxRows
            onClicked: {
                envModel.append({ key: "", value: "", secret: false })
                editor.touched()
            }
        }
    }
    Label {
        visible: envModel.count === 0
        text: "未配置；启动时沿用 Berth 的系统环境"
        color: "#999999"
        font.pixelSize: 12
    }
    Repeater {
        model: envModel
        delegate: ColumnLayout {
            id: envRow
            required property int index
            required property string key
            required property string value
            required property bool secret
            readonly property bool invalid: editor.rowIn(editor.check.invalidRows, index)
            readonly property bool duplicate: editor.rowIn(editor.check.duplicateRows, index)
            readonly property bool dshHome: editor.rowIn(editor.check.dshHomeRows, index)
            Layout.fillWidth: true
            spacing: 2

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: envLine.implicitHeight + 4
                color: envRow.invalid || envRow.duplicate ? "#fde7e9" : "transparent"
                border.color: envRow.invalid || envRow.duplicate ? "#c42b1c" : "transparent"
                radius: 3
                RowLayout {
                    id: envLine
                    anchors.fill: parent
                    anchors.margins: 2
                    TextField {
                        Layout.preferredWidth: 180
                        placeholderText: "键"
                        text: envRow.key
                        onTextEdited: {
                            envModel.setProperty(envRow.index, "key", text)
                            editor.touched()
                        }
                    }
                    Label { text: "=" }
                    TextField {
                        Layout.fillWidth: true
                        placeholderText: "值"
                        text: envRow.value
                        // 敏感值只显示掩码
                        echoMode: envRow.secret ? TextInput.Password : TextInput.Normal
                        onTextEdited: {
                            envModel.setProperty(envRow.index, "value", text)
                            editor.touched()
                        }
                    }
                    CheckBox {
                        text: "敏感"
                        checked: envRow.secret
                        onToggled: {
                            envModel.setProperty(envRow.index, "secret", checked)
                            editor.touched()
                        }
                    }
                    Button {
                        text: "删除"
                        onClicked: {
                            envModel.remove(envRow.index)
                            editor.touched()
                        }
                    }
                }
            }
            Label {
                visible: envRow.invalid || envRow.duplicate
                text: envRow.invalid ? "键名非法：不能为空、不能含 = 或空白，且不超过 256 个字符"
                                     : "键名重复（不区分大小写）"
                color: "#c42b1c"
                font.pixelSize: 11
            }
            Label {
                visible: envRow.dshHome
                text: "DSH_HOME 由 Berth 注入，此处的值不会生效"
                color: "#b45309"
                font.pixelSize: 11
            }
        }
    }

    // —— 额外启动参数 ——
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 6
        Label { text: "额外启动参数"; font.bold: true }
        Label { text: argModel.count + " / " + editor.maxArgs; color: "#888888"; font.pixelSize: 11 }
        Item { Layout.fillWidth: true }
        Button {
            text: "添加参数"
            enabled: argModel.count < editor.maxArgs
            onClicked: {
                argModel.append({ arg: "" })
                editor.touched()
            }
        }
    }
    Label {
        visible: argModel.count === 0
        text: "每项作为一个完整参数追加到 Berth 生成的参数之后，不按空格拆分"
        color: "#999999"
        font.pixelSize: 12
    }
    Repeater {
        model: argModel
        delegate: RowLayout {
            id: argRow
            required property int index
            required property string arg
            Layout.fillWidth: true
            TextField {
                Layout.fillWidth: true
                placeholderText: "参数"
                text: argRow.arg
                onTextEdited: {
                    argModel.setProperty(argRow.index, "arg", text)
                    editor.touched()
                }
            }
            Button {
                text: "上移"
                enabled: argRow.index > 0
                onClicked: {
                    argModel.move(argRow.index, argRow.index - 1, 1)
                    editor.touched()
                }
            }
            Button {
                text: "下移"
                enabled: argRow.index < argModel.count - 1
                onClicked: {
                    argModel.move(argRow.index, argRow.index + 1, 1)
                    editor.touched()
                }
            }
            Button {
                text: "删除"
                onClicked: {
                    argModel.remove(argRow.index)
                    editor.touched()
                }
            }
        }
    }
    Label {
        visible: (editor.check.conflicts || []).length > 0
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: "与 --port/--profile 冲突，启动时不传递（允许保存）：" + (editor.check.conflicts || []).join(" ")
        color: "#b45309"
        font.pixelSize: 11
    }
    Label {
        visible: editor.check.tooManyRows || editor.check.tooManyArgs
        text: "超过上限：环境变量最多 " + editor.maxRows + " 行，额外参数最多 " + editor.maxArgs + " 项"
        color: "#c42b1c"
        font.pixelSize: 11
    }
}
