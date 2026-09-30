import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 模型单价编辑（需求 22.3、22.9）：每百万 token 的输入 / 输出 / 缓存单价。
// 校验在 Settings::setModelPrice 中完成（0–10000，最多 4 位小数）；不合法时拒绝保存、提示有效范围，
// 输入框恢复为此前已保存的单价。
ColumnLayout {
    id: editor
    spacing: 6

    readonly property var prices: berth.settings.modelPrices
    property string error: ""
    property string errorModel: ""

    readonly property int colModel: 180
    readonly property int colPrice: 90

    function fmt(v) {
        // 最多 4 位小数，去掉多余的 0
        return String(Number(Number(v).toFixed(4)))
    }

    Label {
        text: "模型单价（每百万 token）"
        font.bold: true
    }
    Label {
        text: "用于\"用量\"页的估算费用。单价范围 0–10000，最多 4 位小数；未配置单价的模型费用显示\"—\"且不计入合计。"
        color: "#666666"
        font.pixelSize: 12
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    RowLayout {
        spacing: 8
        Label { text: "模型"; color: "#444444"; Layout.preferredWidth: editor.colModel }
        Label { text: "输入"; color: "#444444"; Layout.preferredWidth: editor.colPrice }
        Label { text: "输出"; color: "#444444"; Layout.preferredWidth: editor.colPrice }
        Label { text: "缓存"; color: "#444444"; Layout.preferredWidth: editor.colPrice }
    }

    // 已保存的单价
    Repeater {
        model: editor.prices
        RowLayout {
            id: row
            required property var modelData
            spacing: 8

            function reset() {
                inField.text = editor.fmt(modelData.input)
                outField.text = editor.fmt(modelData.output)
                cacheField.text = editor.fmt(modelData.cache)
            }
            function commit() {
                const err = berth.settings.setModelPrice(modelData.model, inField.text, outField.text,
                                                         cacheField.text)
                editor.errorModel = modelData.model
                editor.error = err
                if (err.length > 0)
                    reset()
            }

            Label {
                text: row.modelData.model
                elide: Text.ElideRight
                Layout.preferredWidth: editor.colModel
            }
            TextField {
                id: inField
                Layout.preferredWidth: editor.colPrice
                text: editor.fmt(row.modelData.input)
                onEditingFinished: row.commit()
            }
            TextField {
                id: outField
                Layout.preferredWidth: editor.colPrice
                text: editor.fmt(row.modelData.output)
                onEditingFinished: row.commit()
            }
            TextField {
                id: cacheField
                Layout.preferredWidth: editor.colPrice
                text: editor.fmt(row.modelData.cache)
                onEditingFinished: row.commit()
            }
            Button {
                text: "删除"
                onClicked: {
                    editor.error = ""
                    berth.settings.removeModelPrice(row.modelData.model)
                }
            }
        }
    }

    // 新增一个模型的单价
    RowLayout {
        spacing: 8
        TextField {
            id: newModel
            Layout.preferredWidth: editor.colModel
            placeholderText: "模型名（与用量页一致）"
        }
        TextField { id: newIn; Layout.preferredWidth: editor.colPrice; placeholderText: "0" }
        TextField { id: newOut; Layout.preferredWidth: editor.colPrice; placeholderText: "0" }
        TextField { id: newCache; Layout.preferredWidth: editor.colPrice; placeholderText: "0" }
        Button {
            text: "添加"
            enabled: newModel.text.trim().length > 0
            onClicked: {
                const v = function(f) { return f.text.trim().length ? f.text : "0" }
                const err = berth.settings.setModelPrice(newModel.text, v(newIn), v(newOut), v(newCache))
                editor.errorModel = newModel.text.trim()
                editor.error = err
                if (err.length === 0) {
                    newModel.text = ""
                    newIn.text = ""
                    newOut.text = ""
                    newCache.text = ""
                }
            }
        }
    }

    Label {
        visible: editor.error.length > 0
        text: "单价未保存（" + editor.errorModel + "）：" + editor.error + "。有效范围 0–10000，最多 4 位小数。"
        color: "#c42b1c"
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
