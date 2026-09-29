import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14
        width: Math.min(parent.width - 48, 640)

        Label {
            text: "设置"
            color: "#000000"
            font.pixelSize: 20
            font.bold: true
        }
        Label {
            text: "控台只负责拉起 dsh web。官方界面仍在浏览器里打开。"
            color: "#000000"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 10
            Layout.fillWidth: true
            Label { text: "dsh 可执行文件"; color: "#000000" }
            TextField {
                Layout.fillWidth: true
                text: berth.settings.dshExecutable
                placeholderText: "dsh"
                color: "#000000"
                onEditingFinished: berth.settings.dshExecutable = text
            }
            Label { text: "Node（可选）"; color: "#000000" }
            TextField {
                Layout.fillWidth: true
                text: berth.settings.nodeExecutable
                placeholderText: "留空则用 PATH。需要 ^22.19 或 >=24"
                color: "#000000"
                onEditingFinished: berth.settings.nodeExecutable = text
            }
            Label { text: "数据目录"; color: "#000000" }
            Label {
                text: berth.settings.dataDir
                color: "#000000"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }
            Label { text: "DSH_HOME"; color: "#000000" }
            Label {
                text: berth.defaultHome
                color: "#000000"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }
        }

        Label {
            text: "已识别 profile"
            color: "#000000"
            font.bold: true
        }
        Label {
            text: berth.knownProfiles.length ? berth.knownProfiles.join("、") : "未找到。检查 %USERPROFILE%\\.dsh\\profiles"
            color: "#000000"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Button {
            text: "重新识别 profile"
            onClicked: berth.importDetectedProfiles()
        }

        CheckBox {
            text: "启动时最小化到托盘"
            checked: berth.settings.startMinimized
            onToggled: berth.settings.startMinimized = checked
        }
        CheckBox {
            text: "实例就绪后打开浏览器"
            checked: berth.settings.openUiOnStart
            onToggled: berth.settings.openUiOnStart = checked
        }
        Button {
            text: "保存设置"
            onClicked: berth.settings.save()
        }
        Item { Layout.fillHeight: true }
    }
}
