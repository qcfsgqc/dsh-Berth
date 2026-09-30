import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

Item {
    id: pane

    // dsh 检查/更新结果反馈（三个 finished 信号共用）
    property bool dshResultOk: true
    property string dshResultText: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14
        width: Math.min(parent.width - 48, 640)

        Label {
            text: "设置"
            font.pixelSize: 20
            font.bold: true
        }
        Label {
            text: "控台负责拉起 dsh web，官方界面在内嵌窗口（WebView2）里打开。"
            color: "#555555"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 10
            Layout.fillWidth: true
            Label { text: "dsh 可执行文件" }
            TextField {
                Layout.fillWidth: true
                text: berth.settings.dshExecutable
                placeholderText: "dsh"
                onEditingFinished: berth.settings.dshExecutable = text
            }
            Label { text: "Node（可选）" }
            TextField {
                Layout.fillWidth: true
                text: berth.settings.nodeExecutable
                placeholderText: "留空则用 PATH。需要 ^22.19 或 >=24"
                onEditingFinished: berth.settings.nodeExecutable = text
            }
            Label { text: "数据目录" }
            Label {
                text: berth.settings.dataDir
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }
            Label { text: "DSH_HOME" }
            Label {
                text: berth.defaultHome
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }
        }

        Label {
            text: "已识别 profile"
            font.bold: true
        }
        Label {
            text: berth.knownProfiles.length ? berth.knownProfiles.join("、") : "未找到。检查 %USERPROFILE%\\.dsh\\profiles"
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
            text: "实例就绪后打开界面"
            checked: berth.settings.openUiOnStart
            onToggled: berth.settings.openUiOnStart = checked
        }
        Label {
            text: "dsh 安装与更新"
            font.bold: true
        }
        Label {
            text: "已安装版本：" + (berth.dshUpdate.installedVersion || "未知")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            text: "npm 最新版本：" + (berth.dshUpdate.latestVersion || "未知")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                text: "检查更新"
                onClicked: {
                    pane.dshResultText = ""
                    berth.dshUpdate.checkInstalled()
                    berth.dshUpdate.checkLatest()
                }
            }
            Button {
                text: "更新到最新"
                enabled: !berth.dshUpdate.busy
                onClicked: {
                    pane.dshResultText = ""
                    berth.dshUpdate.updateDsh()
                }
            }
            BusyIndicator {
                visible: berth.dshUpdate.busy
                running: berth.dshUpdate.busy
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            visible: pane.dshResultText.length > 0
            text: pane.dshResultText
            color: pane.dshResultOk ? "#107c10" : "#c42b1c"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.maximumHeight: 140
            elide: Text.ElideRight
        }
        Label {
            // npm 全局安装才能自动更新，其它安装方式不适用
            text: "更新通过 npm 全局安装执行；从源码或其它方式安装的 dsh 请手动升级。"
            color: "#666666"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Button {
            text: "保存设置"
            onClicked: berth.settings.save()
        }
        Item { Layout.fillHeight: true }
    }

    // 进入设置页时刷新一次本地与 npm 上的版本
    Component.onCompleted: {
        pane.dshResultText = ""
        berth.dshUpdate.checkInstalled()
        berth.dshUpdate.checkLatest()
    }

    Connections {
        target: berth.dshUpdate
        // 成功时上方的版本行已自动更新，结果 Label 只用来报错误，避免两条检查互相覆盖
        function onCheckInstalledFinished(ok, version, error) {
            if (ok) {
                pane.dshResultText = ""
                return
            }
            pane.dshResultOk = false
            pane.dshResultText = "本机版本检查失败：" + error
        }
        function onCheckLatestFinished(ok, version, error) {
            if (ok) {
                pane.dshResultText = ""
                return
            }
            pane.dshResultOk = false
            pane.dshResultText = "npm 版本检查失败：" + error
        }
        function onUpdateFinished(ok, message) {
            pane.dshResultOk = ok
            pane.dshResultText = ok ? message : ("更新失败：" + message)
        }
    }
}
