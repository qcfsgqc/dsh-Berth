import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 扩展页：按 (DSH_HOME, profile) 列出 Skills 与 MCP_Server（后端已按名称升序），提供启用开关。
// 只读条目显示原因；任一 cordis.patch.yml 无法解析时显示文件路径与原因，MCP 开关全部禁用。
// 开关操作后一律以后端重新读取的结果为准，失败时开关自然退回。
Item {
    id: page

    readonly property var ext: berth.extensions

    // 筛选器：[{home, profile, label, key}]
    property var targets: []
    property int targetIndex: -1
    property string extHome: ""
    property string profileName: ""
    // 工作区选项：[{label, path}]，第一项为"不含工作区"
    property var workspaces: []
    property int workspaceIndex: 0
    readonly property string workspacePath: workspaceIndex >= 0 && workspaceIndex < workspaces.length
                                            ? workspaces[workspaceIndex].path : ""

    property var mcp: ({ ok: true, writable: false, error: "", files: [], rows: [] })
    property var skills: ({ ok: true, error: "", roots: [], rows: [] })
    property string resultText: ""
    property bool resultOk: true

    readonly property bool mcpWritable: mcp.ok === true && mcp.writable === true

    function sourceText(s) {
        if (s === "profile") return "profile"
        if (s === "home") return "DSH_HOME"
        if (s === "workspace-dsh") return ".dsh/skills"
        if (s === "workspace-agents") return ".agents/skills"
        return s || ""
    }

    // 进入页面时刷新筛选器，尽量保留当前选择
    function refreshTargets() {
        const oldKey = targetIndex >= 0 && targetIndex < targets.length ? targets[targetIndex].key : ""
        targets = berth.plugins.targets()
        let index = targets.length > 0 ? 0 : -1
        for (let i = 0; i < targets.length; ++i) {
            if (targets[i].key === oldKey) { index = i; break }
        }
        selectTarget(index)
    }

    // 该 (home, profile) 下泊位用到的工作区（去重）
    function buildWorkspaces() {
        const out = [{ label: "不含工作区（仅 DSH_HOME）", path: "" }]
        if (!profileName.length)
            return out
        const key = berth.plugins.keyOf(extHome, profileName)
        const seen = {}
        const model = berth.instances
        for (let r = 0; r < model.rowCount(); ++r) {
            const id = model.data(model.index(r, 0), 257)
            const item = berth.instance(id)
            const ws = item.workspace || ""
            if (!ws.length || berth.plugins.keyOf(item.dshHome || "", item.profile || "") !== key)
                continue
            const k = ws.toLowerCase()
            if (seen[k])
                continue
            seen[k] = true
            out.push({ label: ws + "（" + (item.name || "") + "）", path: ws })
        }
        return out
    }

    function selectTarget(index) {
        targetIndex = index
        const t = index >= 0 && index < targets.length ? targets[index] : null
        extHome = t ? t.home : ""
        profileName = t ? t.profile : ""
        workspaces = buildWorkspaces()
        workspaceIndex = workspaces.length > 1 ? 1 : 0
        resultText = ""
        reload()
    }

    function reload() {
        if (!profileName.length) {
            mcp = { ok: true, writable: false, error: "", files: [], rows: [] }
            skills = { ok: true, error: "", roots: [], rows: [] }
            return
        }
        const r = ext.load(extHome, profileName, workspacePath)
        mcp = r.mcp || { ok: true, writable: false, error: "", files: [], rows: [] }
        skills = r.skills || { ok: true, error: "", roots: [], rows: [] }
    }

    function showResult(r) {
        resultOk = !!r.ok
        if (r.ok)
            resultText = ""
        else if (r.externalChange)
            resultText = "文件已被外部修改，未写入，已重新读取。" + (r.error ? "\n" + r.error : "")
        else
            resultText = r.error || "操作失败"
    }

    onVisibleChanged: if (visible) refreshTargets()
    Component.onCompleted: if (visible) refreshTargets()

    Connections {
        target: page.ext
        function onMcpChanged(home, profile) {
            if (page.profileName.length && berth.plugins.keyOf(home, profile) === berth.plugins.keyOf(page.extHome, page.profileName))
                page.mcp = page.ext.mcpState()
        }
        function onSkillsChanged(home, profile) {
            if (page.profileName.length && berth.plugins.keyOf(home, profile) === berth.plugins.keyOf(page.extHome, page.profileName))
                page.skills = page.ext.skillsState()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 10

        Label {
            text: "扩展"
            font.pixelSize: 20
            font.bold: true
        }
        Label {
            text: "启用或禁用 Skills 与 MCP 服务器；变更后需重启相关泊位才能生效。"
            color: "#555555"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            rowSpacing: 6
            Label { text: "Profile" }
            ComboBox {
                id: targetBox
                Layout.fillWidth: true
                model: page.targets
                textRole: "label"
                currentIndex: page.targetIndex
                enabled: page.targets.length > 0
                Accessible.name: "选择 DSH_HOME 与 profile"
                onActivated: function(index) { page.selectTarget(index) }
            }
            Button {
                text: "刷新"
                onClicked: { page.resultText = ""; page.reload() }
            }
            Label { text: "工作区" }
            ComboBox {
                Layout.fillWidth: true
                Layout.columnSpan: 2
                model: page.workspaces
                textRole: "label"
                currentIndex: page.workspaceIndex
                enabled: page.profileName.length > 0
                Accessible.name: "选择扫描 Skills 的工作区"
                onActivated: function(index) {
                    page.workspaceIndex = index
                    page.resultText = ""
                    page.reload()
                }
            }
        }

        Label {
            visible: page.resultText.length > 0
            text: page.resultText
            color: page.resultOk ? "#107c10" : "#c42b1c"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            visible: !page.profileName.length
            text: "没有可选的 profile"
            color: "#666666"
        }

        ScrollView {
            id: scroll
            visible: page.profileName.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: scroll.availableWidth
                spacing: 8

                // —— Skills ——
                Label {
                    text: "Skills（" + (page.skills.rows ? page.skills.rows.length : 0) + "）"
                    font.pixelSize: 15
                    font.bold: true
                }
                Label {
                    visible: page.skills.ok === false
                    text: page.skills.error || ""
                    color: "#c42b1c"
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
                Label {
                    visible: page.skills.ok !== false && (!page.skills.rows || page.skills.rows.length === 0)
                    text: "没有找到 Skill"
                    color: "#666666"
                }
                Repeater {
                    model: page.skills.rows || []
                    delegate: Rectangle {
                        id: skillRow
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: skillLayout.implicitHeight + 12
                        radius: 4
                        color: "#ffffff"
                        border.color: "#e5e5e5"
                        RowLayout {
                            id: skillLayout
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                RowLayout {
                                    spacing: 8
                                    Label { text: skillRow.modelData.name; font.bold: true }
                                    Label {
                                        text: page.sourceText(skillRow.modelData.source)
                                        color: "#666666"
                                        font.pixelSize: 12
                                    }
                                }
                                Label {
                                    text: skillRow.modelData.path || ""
                                    color: "#555555"
                                    font.pixelSize: 12
                                    wrapMode: Text.WrapAnywhere
                                    Layout.fillWidth: true
                                }
                            }
                            Switch {
                                checked: skillRow.modelData.enabled === true
                                enabled: page.skills.ok !== false
                                text: checked ? "启用" : "禁用"
                                Accessible.name: "启用 Skill " + skillRow.modelData.name
                                onToggled: {
                                    const r = page.ext.setSkillEnabled(skillRow.modelData.root,
                                                                       skillRow.modelData.entry, checked)
                                    page.showResult(r)
                                    // 以磁盘为准重读：失败时开关退回
                                    page.skills = page.ext.skillsState()
                                }
                            }
                        }
                    }
                }

                Item { implicitHeight: 8 }

                // —— MCP ——
                Label {
                    text: "MCP 服务器（" + (page.mcp.rows ? page.mcp.rows.length : 0) + "）"
                    font.pixelSize: 15
                    font.bold: true
                }
                // 解析错误：显示文件路径与原因，所有开关禁用
                Rectangle {
                    visible: page.mcp.ok === false
                    Layout.fillWidth: true
                    implicitHeight: errLayout.implicitHeight + 16
                    radius: 4
                    color: "#fde7e9"
                    border.color: "#f1bbbc"
                    ColumnLayout {
                        id: errLayout
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4
                        Label {
                            text: "cordis.patch.yml 无法解析，MCP 开关已全部禁用："
                            font.bold: true
                            color: "#c42b1c"
                        }
                        Label {
                            text: page.mcp.error || ""
                            color: "#c42b1c"
                            wrapMode: Text.WrapAnywhere
                            Layout.fillWidth: true
                        }
                    }
                }
                Label {
                    visible: page.mcp.ok !== false && (!page.mcp.rows || page.mcp.rows.length === 0)
                    text: "没有配置 MCP 服务器"
                    color: "#666666"
                }
                Repeater {
                    model: page.mcp.rows || []
                    delegate: Rectangle {
                        id: mcpRow
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: mcpLayout.implicitHeight + 12
                        radius: 4
                        color: "#ffffff"
                        border.color: "#e5e5e5"
                        RowLayout {
                            id: mcpLayout
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                RowLayout {
                                    spacing: 8
                                    Label { text: mcpRow.modelData.name; font.bold: true }
                                    Label {
                                        text: (mcpRow.modelData.transport || "") + " · "
                                              + page.sourceText(mcpRow.modelData.source)
                                        color: "#666666"
                                        font.pixelSize: 12
                                    }
                                }
                                Label {
                                    visible: (mcpRow.modelData.target || "").length > 0
                                    text: mcpRow.modelData.target || ""
                                    color: "#555555"
                                    font.pixelSize: 12
                                    wrapMode: Text.WrapAnywhere
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: (mcpRow.modelData.path || "") + "：第 " + mcpRow.modelData.line + " 行"
                                    color: "#888888"
                                    font.pixelSize: 11
                                    wrapMode: Text.WrapAnywhere
                                    Layout.fillWidth: true
                                }
                                Label {
                                    visible: mcpRow.modelData.readOnly === true
                                    text: "只读：" + (mcpRow.modelData.readOnlyReason || "")
                                    color: "#9d5d00"
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                            Switch {
                                checked: mcpRow.modelData.enabled === true
                                enabled: page.mcpWritable && mcpRow.modelData.readOnly !== true
                                text: checked ? "启用" : "禁用"
                                Accessible.name: "启用 MCP " + mcpRow.modelData.name
                                onToggled: {
                                    const r = page.ext.setMcpEnabled(mcpRow.modelData.path, mcpRow.modelData.id, checked)
                                    page.showResult(r)
                                    // 以文件为准重读：失败时开关退回
                                    page.mcp = page.ext.mcpState()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
