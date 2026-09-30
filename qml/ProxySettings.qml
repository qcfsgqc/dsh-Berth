import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 代理与 npm 镜像设置（需求 16）。
// 界面编辑的是本地草稿；只有点"保存代理设置"才经 berth.proxy.save(draft) 写入，
// 非法字段时后端拒绝保存且已保存设置不变。自动探测与测试连接都不改已保存设置。
ColumnLayout {
    id: root
    spacing: 10

    // —— 草稿 ——
    property string dMode: "none"
    property string dScheme: "http"
    property string dHost: ""
    property string dPort: ""
    property string dUser: ""
    property string dPassword: ""
    property bool passwordEdited: false   // 输入过新密码
    property bool clearPassword: false    // 勾选清除已存密码
    property string dRegistryKind: "official"
    property int dPresetIndex: 0
    property string dCustomUrl: ""
    property bool dMirrorFallback: false
    property bool dInheritProxy: false

    // —— 结果反馈 ——
    property var fieldErrors: []          // host | port | registryUrl
    property string saveMessage: ""
    property bool saveOk: true
    property string detectMessage: ""
    property bool detectFound: true

    readonly property var modeValues: ["none", "system", "manual"]
    readonly property var schemeValues: ["http", "https", "socks5"]
    readonly property var kindValues: ["official", "preset", "custom"]
    readonly property var presetList: berth.proxy.presets

    function hasError(field) { return fieldErrors.indexOf(field) >= 0 }

    function registryUrlForDraft() {
        if (dRegistryKind === "preset")
            return presetList.length > dPresetIndex ? presetList[dPresetIndex].url : ""
        if (dRegistryKind === "custom")
            return dCustomUrl.trim()
        return ""
    }

    function buildDraft() {
        var d = {
            mode: dMode,
            scheme: dScheme,
            host: dHost.trim(),
            port: dPort.trim(),
            user: dUser,
            registryKind: dRegistryKind,
            registryUrl: registryUrlForDraft(),
            mirrorFallback: dMirrorFallback,
            inheritProxy: dInheritProxy
        }
        // 不传 password = 沿用已存密码；空串 = 清除
        if (clearPassword)
            d.password = ""
        else if (passwordEdited)
            d.password = dPassword
        return d
    }

    // 从已保存设置载入草稿
    function load() {
        var p = berth.proxy
        dMode = p.mode || "none"
        dScheme = p.scheme || "http"
        dHost = p.host
        dPort = p.port > 0 ? String(p.port) : ""
        dUser = p.user
        dPassword = ""
        passwordEdited = false
        clearPassword = false
        dRegistryKind = p.registryKind || "official"
        dPresetIndex = 0
        dCustomUrl = ""
        if (dRegistryKind === "preset") {
            var idx = -1
            for (var i = 0; i < presetList.length; ++i)
                if (presetList[i].url === p.registryUrl) idx = i
            if (idx >= 0) dPresetIndex = idx
            else { dRegistryKind = "custom"; dCustomUrl = p.registryUrl }
        } else if (dRegistryKind === "custom") {
            dCustomUrl = p.registryUrl
        }
        dMirrorFallback = p.mirrorFallback
        dInheritProxy = p.inheritProxy
        fieldErrors = []
    }

    function fieldLabel(f) {
        if (f === "host") return "主机"
        if (f === "port") return "端口"
        if (f === "registryUrl") return "镜像地址"
        return f
    }

    function doSave() {
        var r = berth.proxy.save(buildDraft())
        saveOk = r.ok
        fieldErrors = r.errors || []
        if (r.ok) {
            load()
            saveMessage = "代理设置已保存，对之后的新请求与新子进程生效。"
        } else {
            var names = fieldErrors.map(fieldLabel).join("、")
            saveMessage = "未保存" + (names ? "，非法字段：" + names : "")
                    + (r.error ? "（" + r.error + "）" : "")
        }
    }

    Component.onCompleted: load()

    Connections {
        target: berth.proxy
        function onDetectFinished(found, message) {
            root.detectFound = found
            root.detectMessage = message || (found ? "" : "未发现可用代理")
        }
    }

    Label {
        text: "代理与 npm 镜像"
        font.bold: true
    }
    Label {
        visible: berth.proxy.credentialNotice.length > 0
        text: berth.proxy.credentialNotice
        color: "#c42b1c"
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 8
        Layout.fillWidth: true

        Label { text: "代理模式" }
        ComboBox {
            Layout.fillWidth: true
            model: ["不使用代理", "系统代理", "手动"]
            currentIndex: Math.max(0, root.modeValues.indexOf(root.dMode))
            onActivated: index => root.dMode = root.modeValues[index]
        }

        Label { text: "协议"; visible: root.dMode === "manual" }
        ComboBox {
            visible: root.dMode === "manual"
            Layout.fillWidth: true
            model: ["HTTP", "HTTPS", "SOCKS5"]
            currentIndex: Math.max(0, root.schemeValues.indexOf(root.dScheme))
            onActivated: index => root.dScheme = root.schemeValues[index]
        }

        Label {
            text: "主机"
            visible: root.dMode === "manual"
            color: root.hasError("host") ? "#c42b1c" : palette.windowText
        }
        TextField {
            visible: root.dMode === "manual"
            Layout.fillWidth: true
            text: root.dHost
            placeholderText: "127.0.0.1"
            onTextEdited: root.dHost = text
        }

        Label {
            text: "端口"
            visible: root.dMode === "manual"
            color: root.hasError("port") ? "#c42b1c" : palette.windowText
        }
        TextField {
            visible: root.dMode === "manual"
            Layout.fillWidth: true
            text: root.dPort
            placeholderText: "1–65535"
            inputMethodHints: Qt.ImhDigitsOnly
            onTextEdited: root.dPort = text
        }

        Label { text: "用户名（可选）"; visible: root.dMode === "manual" }
        TextField {
            visible: root.dMode === "manual"
            Layout.fillWidth: true
            text: root.dUser
            onTextEdited: root.dUser = text
        }

        Label { text: "密码（可选）"; visible: root.dMode === "manual" }
        RowLayout {
            visible: root.dMode === "manual"
            Layout.fillWidth: true
            spacing: 8
            TextField {
                Layout.fillWidth: true
                echoMode: TextInput.Password
                enabled: !root.clearPassword
                text: root.dPassword
                // 已存密码只显示固定长度掩码，不回显明文
                placeholderText: berth.proxy.hasPassword ? "●●●●●●●●（已保存，留空沿用）" : ""
                onTextEdited: {
                    root.dPassword = text
                    root.passwordEdited = true
                }
            }
            CheckBox {
                visible: berth.proxy.hasPassword
                text: "清除已存密码"
                checked: root.clearPassword
                onToggled: root.clearPassword = checked
            }
        }
    }

    // —— 自动探测 ——
    RowLayout {
        spacing: 8
        Button {
            text: berth.proxy.detecting ? "探测中…" : "自动探测"
            enabled: !berth.proxy.detecting
            onClicked: {
                root.detectMessage = ""
                berth.proxy.detect()
            }
        }
        BusyIndicator {
            visible: berth.proxy.detecting
            running: berth.proxy.detecting
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
        }
        Label {
            visible: root.detectMessage.length > 0
            text: root.detectMessage
            color: root.detectFound ? "#555555" : "#c42b1c"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
    Repeater {
        model: berth.proxy.candidates
        delegate: RowLayout {
            required property var modelData
            spacing: 8
            Label {
                text: modelData.scheme + "://" + modelData.host + ":" + modelData.port
                font.family: "Consolas"
            }
            Label {
                text: "来源：" + modelData.sourceText
                color: "#666666"
            }
            Button {
                // 只填入草稿，点"保存代理设置"后才生效
                text: "使用"
                onClicked: {
                    root.dMode = "manual"
                    root.dScheme = modelData.scheme
                    root.dHost = modelData.host
                    root.dPort = String(modelData.port)
                    root.saveMessage = "已填入候选，保存后生效。"
                    root.saveOk = true
                }
            }
        }
    }

    // —— npm 镜像 ——
    GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 8
        Layout.fillWidth: true

        Label { text: "npm 源" }
        ComboBox {
            Layout.fillWidth: true
            model: ["官方源", "预置镜像", "自定义"]
            currentIndex: Math.max(0, root.kindValues.indexOf(root.dRegistryKind))
            onActivated: index => root.dRegistryKind = root.kindValues[index]
        }

        Label { text: "预置镜像"; visible: root.dRegistryKind === "preset" }
        ComboBox {
            visible: root.dRegistryKind === "preset"
            Layout.fillWidth: true
            model: root.presetList.map(p => p.name + "  " + p.url)
            currentIndex: root.dPresetIndex
            onActivated: index => root.dPresetIndex = index
        }

        Label {
            text: "镜像地址"
            visible: root.dRegistryKind === "custom"
            color: root.hasError("registryUrl") ? "#c42b1c" : palette.windowText
        }
        TextField {
            visible: root.dRegistryKind === "custom"
            Layout.fillWidth: true
            text: root.dCustomUrl
            placeholderText: "https://…"
            onTextEdited: root.dCustomUrl = text
        }

        Label { text: "当前生效" }
        Label {
            text: berth.proxy.effectiveRegistry
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: "#555555"
        }
    }
    CheckBox {
        text: "镜像兜底：官方源请求失败时改用镜像重试一次"
        checked: root.dMirrorFallback
        onToggled: root.dMirrorFallback = checked
    }
    CheckBox {
        text: "泊位继承代理：启动泊位时注入代理环境变量（泊位自身同名变量优先）"
        checked: root.dInheritProxy
        onToggled: root.dInheritProxy = checked
    }

    // —— 保存 / 测试 ——
    RowLayout {
        spacing: 8
        Button {
            text: "保存代理设置"
            enabled: !berth.settings.readOnly
            onClicked: root.doSave()
        }
        Button {
            text: berth.proxy.testing ? "测试中…" : "测试连接"
            enabled: !berth.proxy.testing
            // 用当前草稿（含未保存修改）测试
            onClicked: berth.proxy.test(root.buildDraft())
        }
        Button {
            text: "还原"
            onClicked: {
                root.load()
                root.saveMessage = ""
            }
        }
        BusyIndicator {
            visible: berth.proxy.testing
            running: berth.proxy.testing
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
        }
    }
    Label {
        visible: root.saveMessage.length > 0
        text: root.saveMessage
        color: root.saveOk ? "#107c10" : "#c42b1c"
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Repeater {
        model: berth.proxy.testResults
        delegate: Label {
            required property var modelData
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: modelData.ok ? "#107c10" : "#c42b1c"
            text: (modelData.name || modelData.target) + "："
                  + (modelData.ok
                     ? "成功，" + modelData.ms + " ms" + (modelData.status ? "（HTTP " + modelData.status + "）" : "")
                     : "失败，" + modelData.categoryText
                       + (modelData.ms ? "，" + modelData.ms + " ms" : "")
                       + (modelData.error ? "（" + modelData.error + "）" : ""))
        }
    }
}
