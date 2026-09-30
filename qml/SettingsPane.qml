import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

Item {
    id: pane

    // dsh 检查/更新结果反馈（三个 finished 信号共用）
    property bool dshResultOk: true
    property string dshResultText: ""
    // 开机自启开关写注册表失败时的原因
    property string autostartError: ""
    // 插件目录地址校验失败的原因
    property string catalogUrlError: ""

    // 内容较多，整页可滚动
    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth

    ColumnLayout {
        x: 24
        spacing: 14
        width: Math.min(scroll.availableWidth - 48, 640)

        Item { implicitHeight: 10 }

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
            Label { text: "插件目录地址" }
            TextField {
                id: catalogUrlField
                Layout.fillWidth: true
                text: berth.settings.catalogUrl
                placeholderText: "https://dsh-plug.in/api/plugins.json"
                onEditingFinished: {
                    pane.catalogUrlError = berth.settings.setCatalogUrl(text)
                    // 不合法时保留原值
                    if (pane.catalogUrlError.length > 0)
                        text = berth.settings.catalogUrl
                }
            }
            Item { visible: pane.catalogUrlError.length > 0 }
            Label {
                visible: pane.catalogUrlError.length > 0
                text: "地址未保存：" + pane.catalogUrlError
                color: "#c42b1c"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
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
            id: autostartBox
            text: "开机自启 Berth"
            // 按注册表实际状态显示；写入失败时 registeredChanged 把开关拉回原状态
            checked: berth.autostart.registered
            onToggled: {
                pane.autostartError = berth.autostart.apply(checked)
                checked = Qt.binding(function() { return berth.autostart.registered })
            }
        }
        Label {
            visible: pane.autostartError.length > 0
            text: pane.autostartError
            color: "#c42b1c"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        CheckBox {
            text: "开机自启时最小化到托盘"
            checked: berth.settings.startMinimized
            onToggled: berth.settings.startMinimized = checked
        }
        CheckBox {
            text: "实例就绪后打开界面"
            checked: berth.settings.openUiOnStart
            onToggled: berth.settings.openUiOnStart = checked
        }
        CheckBox {
            text: "为运行中泊位显示独立托盘图标"
            checked: berth.settings.trayInstanceIcons
            onToggled: berth.settings.trayInstanceIcons = checked
        }
        CheckBox {
            text: "关闭窗口时最小化到托盘"
            checked: berth.settings.closeToTray
            onToggled: berth.settings.closeToTray = checked
        }
        RowLayout {
            spacing: 12
            Label { text: "系统通知" }
            CheckBox {
                text: "崩溃"
                checked: berth.settings.notifyCrash
                onToggled: berth.settings.notifyCrash = checked
            }
            CheckBox {
                text: "就绪"
                checked: berth.settings.notifyReady
                onToggled: berth.settings.notifyReady = checked
            }
            CheckBox {
                text: "更新"
                checked: berth.settings.notifyUpdate
                onToggled: berth.settings.notifyUpdate = checked
            }
        }
        // 更新检查间隔（需求 14.2）：1–168 小时，取消勾选即关闭（updateCheckHours = 0）
        RowLayout {
            spacing: 12
            CheckBox {
                id: updateCheckBox
                text: "定期检查更新，间隔（小时）"
                checked: berth.settings.updateCheckHours > 0
                onToggled: berth.settings.updateCheckHours = checked ? Math.max(1, updateHoursSpin.value) : 0
            }
            SpinBox {
                id: updateHoursSpin
                from: 1
                to: 168
                editable: true
                enabled: updateCheckBox.checked
                value: berth.settings.updateCheckHours > 0 ? berth.settings.updateCheckHours : 24
                onValueModified: berth.settings.updateCheckHours = value
            }
        }
        RowLayout {
            spacing: 12
            Label { text: "批量启动错峰间隔（秒）" }
            SpinBox {
                from: 0
                to: 60
                editable: true
                value: berth.settings.staggerSec
                onValueModified: berth.settings.staggerSec = value
            }
        }
        Label {
            text: "批量启动与开机自启的泊位按此间隔依次发起，0 表示不间隔。"
            color: "#666666"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            text: "崩溃自动重启"
            font.bold: true
        }
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 8
            Label { text: "基础间隔（秒）" }
            SpinBox {
                from: 1
                to: 60
                editable: true
                value: berth.settings.autoRestartBaseSec
                onValueModified: berth.settings.autoRestartBaseSec = value
            }
            Label { text: "最大间隔（秒）" }
            SpinBox {
                // 不小于基础间隔，不超过 600 秒
                from: berth.settings.autoRestartBaseSec
                to: 600
                editable: true
                value: berth.settings.autoRestartMaxSec
                onValueModified: berth.settings.autoRestartMaxSec = value
            }
            Label { text: "连续重启上限（次）" }
            SpinBox {
                from: 1
                to: 20
                editable: true
                value: berth.settings.autoRestartMaxAttempts
                onValueModified: berth.settings.autoRestartMaxAttempts = value
            }
        }
        Label {
            text: "第 n 次重启前等待 min(2^(n-1) × 基础间隔, 最大间隔)；连续运行满 120 秒后计数清零。开关在各泊位的编辑页里。"
            color: "#666666"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
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
        ProxySettings { Layout.fillWidth: true }
        ModelPriceEditor { Layout.fillWidth: true }
        Button {
            text: "保存设置"
            // settings.json 只读模式（schema 过高或损坏）时禁止保存
            enabled: !berth.settings.readOnly
            onClicked: berth.settings.save()
        }
        Item { implicitHeight: 24 }
    }
    }

    // 进入设置页时刷新一次本地与 npm 上的版本
    Component.onCompleted: {
        pane.dshResultText = ""
        pane.autostartError = ""
        pane.catalogUrlError = ""
        berth.autostart.refresh()
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
