import QtQuick
import QtQuick.Controls.Windows

// 泊位图标：kind 为 none | builtin | file。
// file 时 value 是 icons/ 下的相对路径，或编辑中新选的 file:/// URL；
// 文件不存在或无法解析时显示默认图标（不改已保存配置，需求 9.7）
Item {
    id: root

    property string kind: "none"
    property string value: ""
    property int size: 20
    // 为 true 时 kind 为 none 也显示默认图标（编辑界面预览用）
    property bool showDefaultForNone: false

    // 内置图标集：名称 → 字形与颜色
    readonly property var builtins: ({
        "star": { glyph: "★", color: "#e3a008" },
        "bolt": { glyph: "⚡", color: "#ca5010" },
        "cloud": { glyph: "☁", color: "#0078d4" },
        "heart": { glyph: "♥", color: "#c42b1c" },
        "gear": { glyph: "⚙", color: "#555555" },
        "flag": { glyph: "⚑", color: "#107c10" },
        "diamond": { glyph: "◆", color: "#8764b8" },
        "dot": { glyph: "●", color: "#038387" }
    })
    readonly property var builtinNames: ["star", "bolt", "cloud", "heart", "gear", "flag", "diamond", "dot"]

    readonly property string fileUrl: {
        if (kind !== "file" || value.length === 0)
            return ""
        return value.startsWith("file:") ? value : berth.iconFileUrl(value)
    }
    readonly property bool imageOk: kind === "file" && fileUrl.length > 0 && img.status !== Image.Error
    readonly property var builtinEntry: kind === "builtin" ? builtins[value] : undefined

    width: size
    height: size
    implicitWidth: size
    implicitHeight: size
    visible: kind !== "none" || showDefaultForNone

    Image {
        id: img
        anchors.fill: parent
        visible: root.imageOk && status === Image.Ready
        source: root.kind === "file" ? root.fileUrl : ""
        sourceSize.width: root.size * 2
        sourceSize.height: root.size * 2
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: false
    }

    Label {
        anchors.centerIn: parent
        visible: !!root.builtinEntry
        text: root.builtinEntry ? root.builtinEntry.glyph : ""
        color: root.builtinEntry ? root.builtinEntry.color : "#666666"
        font.pixelSize: Math.round(root.size * 0.85)
    }

    // 默认图标：无图标预览、未知内置名、图片缺失或解析失败
    Rectangle {
        anchors.fill: parent
        visible: !img.visible && !root.builtinEntry
                 && !(root.imageOk && img.status === Image.Loading)
        radius: 4
        color: "#e5e5e5"
        Label {
            anchors.centerIn: parent
            text: "⚓"
            color: "#666666"
            font.pixelSize: Math.round(root.size * 0.7)
        }
    }
}
