import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 插件市场：搜索（300ms 防抖）、渠道选择、目标 (home, profile) 选择、手动输入包名或 git URL、安装进度
Item {
    id: page

    // 目标选项：[{home, profile, label, key}]
    property var targets: []
    property int targetIndex: -1
    property string channel: "stable"
    property string keyword: ""
    property var entries: []
    // 手动输入的格式错误
    property string inputError: ""
    // 最近一次安装结果
    property string resultText: ""
    property bool resultOk: true
    property string resultTail: ""

    readonly property var target: targetIndex >= 0 && targetIndex < targets.length ? targets[targetIndex] : null
    readonly property string targetKey: target ? target.key : ""
    readonly property var prog: targetKey.length ? berth.plugins.progress[targetKey] : undefined
    // 目标 profile 上有任何操作进行中（安装 / 批量 / 单个卸载）时禁用安装
    readonly property bool targetBusy: target !== null
                                       && (prog !== undefined || berth.pluginBusy
                                           || (berth.plugins.progress, berth.plugins.isBusy(target.home, target.profile)))
    readonly property bool canInstall: target !== null && !targetBusy
    readonly property bool installing: prog !== undefined && prog.op === "install"

    // 由插件页调用：同步目标列表与预选项
    function init(list, index) {
        targets = list
        targetIndex = index
    }

    // 打开市场或点刷新时拉取目录
    function refresh() {
        berth.plugins.fetchCatalog()
        applyFilter()
    }

    function applyFilter() {
        entries = berth.plugins.filterCatalog(keyword)
    }

    function versionOf(entry) {
        return entry[channel] || ""
    }

    function startInstall(spec) {
        resultText = ""
        resultTail = ""
        const r = berth.plugins.install(target.home, target.profile, spec, channel)
        if (!r.ok) {
            resultOk = false
            resultText = r.error
        }
    }

    function installManual() {
        const text = manualField.text.trim()
        const err = berth.plugins.validateInstallInput(text)
        if (err.length) {
            // 拒绝安装，保留输入框内容
            inputError = err
            return
        }
        inputError = ""
        startInstall(text)
    }

    Timer {
        id: debounce
        interval: 300
        repeat: false
        onTriggered: page.applyFilter()
    }

    Connections {
        target: berth.plugins
        function onCatalogChanged() {
            page.applyFilter()
        }
        function onInstallFinished(home, profile, spec, ok, name, message, tail) {
            page.resultOk = ok
            page.resultText = (ok ? "" : spec + "：") + message
            page.resultTail = ok ? "" : (tail || "")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: "安装到" }
            ComboBox {
                Layout.fillWidth: true
                model: page.targets
                textRole: "label"
                currentIndex: page.targetIndex
                displayText: page.target ? page.target.label : "请选择泊位或 profile"
                enabled: page.targets.length > 0
                onActivated: index => page.targetIndex = index
                Accessible.name: "安装目标"
            }
            Label { text: "渠道" }
            ComboBox {
                id: channelBox
                model: ["stable", "beta", "alpha"]
                currentIndex: Math.max(0, model.indexOf(page.channel))
                onActivated: index => page.channel = model[index]
                Accessible.name: "发布渠道"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "搜索包名或描述"
                onTextChanged: {
                    page.keyword = text
                    debounce.restart()
                }
                Accessible.name: "搜索插件"
            }
            BusyIndicator {
                visible: berth.plugins.catalogLoading
                running: visible
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }
            Button {
                text: "刷新"
                enabled: !berth.plugins.catalogLoading
                onClicked: page.refresh()
            }
        }

        Label {
            visible: berth.plugins.catalogLoading
            text: "正在加载插件目录…"
            color: "#555555"
        }

        RowLayout {
            Layout.fillWidth: true
            visible: berth.plugins.catalogError.length > 0 && !berth.plugins.catalogLoading
            spacing: 8
            Label {
                text: "目录加载失败：" + berth.plugins.catalogError
                      + (berth.plugins.catalogFromCache ? "（显示缓存）" : "")
                color: "#c42b1c"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Button {
                text: "重试"
                onClicked: page.refresh()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#e5e5e5"
            border.width: 1
            radius: 4

            ListView {
                id: marketList
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: page.entries
                spacing: 2
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property string version: page.versionOf(modelData)
                    width: marketList.width
                    height: 52
                    radius: 4
                    color: rowHover.hovered ? "#eef4fc" : "transparent"
                    HoverHandler { id: rowHover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                text: row.modelData.name
                                font.pixelSize: 14
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: row.modelData.description || ""
                                color: "#555555"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        Label {
                            text: row.version.length ? row.version : page.channel + " 渠道无可用版本"
                            color: row.version.length ? "#555555" : "#ca5010"
                            font.pixelSize: 12
                        }
                        Button {
                            text: "安装"
                            enabled: page.canInstall && row.version.length > 0
                            onClicked: page.startInstall(row.modelData.name)
                            Accessible.name: "安装 " + row.modelData.name
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: page.entries.length === 0 && !berth.plugins.catalogLoading
                text: page.keyword.length ? "没有匹配的插件" : "目录为空"
                color: "#666666"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: manualField
                Layout.fillWidth: true
                placeholderText: "包名（可带 @版本 或 @dist-tag）或 git URL"
                onTextChanged: page.inputError = ""
                onAccepted: if (page.canInstall) page.installManual()
                Accessible.name: "手动输入包名或 git URL"
            }
            Button {
                text: "安装"
                enabled: page.canInstall && manualField.text.trim().length > 0
                onClicked: page.installManual()
            }
        }

        Label {
            visible: page.inputError.length > 0
            text: "格式错误：" + page.inputError
            color: "#c42b1c"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            visible: page.target === null
            text: "请先选择安装目标"
            color: "#555555"
        }

        Label {
            visible: page.installing
            text: "正在安装到「" + (page.target ? page.target.label : "") + "」，该 profile 的其他操作暂不可用…"
            color: "#ca5010"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            visible: page.resultText.length > 0 && !page.installing
            text: (page.resultOk ? "成功：" : "失败：") + page.resultText
            color: page.resultOk ? "#107c10" : "#c42b1c"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        ScrollView {
            visible: page.resultTail.length > 0 && !page.installing
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            TextArea {
                text: page.resultTail
                readOnly: true
                wrapMode: TextEdit.NoWrap
                font.family: "Consolas"
                font.pixelSize: 12
            }
        }
    }
}
