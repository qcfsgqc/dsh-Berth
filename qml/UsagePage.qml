import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// Token 用量与估算费用（需求 22，待定 TODO #2 方案 A）。
// 数据来自 berth.usage（结构见 StatsService::UsageResult::toVariantMap），经 berth.refreshUsage(range) 刷新；
// 读取期间 berth.usageLoading 为 true，并保留上一次结果。单价在"设置"页配置。
Item {
    id: page

    // 页面可见时为 true（由 Main.qml 传入）；变为可见时刷新一次
    property bool active: false

    readonly property var ranges: [
        { key: "today", label: "今日" },
        { key: "7d", label: "近 7 天" },
        { key: "30d", label: "近 30 天" },
        { key: "all", label: "全部" }
    ]
    property string range: "today"

    readonly property var usage: berth.usage
    readonly property bool loading: berth.usageLoading
    readonly property bool hasResult: usage && usage.requestId !== undefined
    // 需求 22.5 / 22.8：没有任何 usage 记录或全部泊位失败时隐藏用量区域
    readonly property bool showData: hasResult && usage.anyUsage === true && usage.allFailed !== true

    function refresh() {
        berth.refreshUsage(range)
    }

    function fmtInt(n) {
        return Number(n || 0).toLocaleString(Qt.locale(), "f", 0)
    }
    function fmtCost(v) {
        return Number(v || 0).toFixed(4)
    }

    onActiveChanged: if (active) refresh()
    onRangeChanged: refresh()

    // 表格列宽
    readonly property int colName: 180
    readonly property int colNum: 110
    readonly property int colCost: 110

    component HeaderCell: Label {
        font.bold: true
        color: "#444444"
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: page.colNum
    }
    component NumCell: Label {
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: page.colNum
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 12

            Item { implicitHeight: 4 }

            // —— 工具栏：时间范围 + 刷新 ——
            RowLayout {
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: "Token 用量"
                    font.pixelSize: 16
                    font.bold: true
                }
                Item { Layout.preferredWidth: 12 }
                Repeater {
                    model: page.ranges
                    Button {
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        checked: page.range === modelData.key
                        onClicked: page.range = modelData.key
                    }
                }
                Item { Layout.fillWidth: true }
                BusyIndicator {
                    running: page.loading
                    visible: page.loading
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                }
                Label {
                    visible: page.loading
                    text: "读取中…"
                    color: "#666666"
                }
                Button {
                    text: "刷新"
                    onClicked: page.refresh()
                }
            }

            Label {
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#666666"
                font.pixelSize: 12
                text: "按泊位的 DSH_HOME 及其 workspace 对应的项目目录归属；同一 DSH_HOME 下不同 profile 的用量无法区分。"
                      + (page.hasResult && page.usage.updatedAt
                         ? "  更新于 " + page.usage.updatedAt.replace("T", " ") : "")
            }

            // —— 无数据 / 全部失败 ——
            Label {
                visible: page.hasResult && !page.showData
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: page.usage && page.usage.allFailed ? "#c42b1c" : "#666666"
                text: {
                    if (!page.hasResult)
                        return ""
                    if (page.usage.allFailed)
                        return "读取失败：" + (page.usage.error || "未知原因")
                    if (page.usage.error)
                        return page.usage.error
                    return "当前 dsh 版本未提供用量数据"
                }
            }
            Label {
                visible: !page.hasResult
                Layout.leftMargin: 20
                color: "#666666"
                text: page.loading ? "正在读取用量…" : "尚未读取"
            }

            // —— 按泊位 ——
            ColumnLayout {
                visible: page.showData
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.fillWidth: true
                spacing: 6

                Label { text: "按泊位"; font.bold: true }
                RowLayout {
                    spacing: 8
                    Label { text: "泊位"; font.bold: true; color: "#444444"; Layout.preferredWidth: page.colName }
                    HeaderCell { text: "输入" }
                    HeaderCell { text: "输出" }
                    HeaderCell { text: "缓存" }
                }
                Repeater {
                    model: page.showData ? page.usage.byBerth : []
                    RowLayout {
                        required property var modelData
                        spacing: 8
                        Label {
                            text: modelData.name || modelData.id
                            elide: Text.ElideRight
                            Layout.preferredWidth: page.colName
                        }
                        NumCell { visible: modelData.ok; text: page.fmtInt(modelData.input) }
                        NumCell { visible: modelData.ok; text: page.fmtInt(modelData.output) }
                        NumCell { visible: modelData.ok; text: page.fmtInt(modelData.cache) }
                        Label {
                            visible: !modelData.ok
                            color: "#c42b1c"
                            text: "读取失败：" + (modelData.error || "未知原因")
                            wrapMode: Text.Wrap
                            Layout.preferredWidth: page.colNum * 3 + 16
                        }
                    }
                }
                Rectangle { Layout.preferredWidth: page.colName + page.colNum * 3 + 24; height: 1; color: "#e5e5e5" }
                RowLayout {
                    spacing: 8
                    Label { text: "合计"; font.bold: true; Layout.preferredWidth: page.colName }
                    NumCell { font.bold: true; text: page.showData ? page.fmtInt(page.usage.total.input) : "" }
                    NumCell { font.bold: true; text: page.showData ? page.fmtInt(page.usage.total.output) : "" }
                    NumCell { font.bold: true; text: page.showData ? page.fmtInt(page.usage.total.cache) : "" }
                }
                Label {
                    visible: page.showData && page.usage.failedCount > 0
                    color: "#9d5d00"
                    font.pixelSize: 12
                    text: page.showData ? ("已从合计中排除 " + page.usage.failedCount + " 个读取失败的泊位") : ""
                }
            }

            // —— 按模型（含估算费用）——
            ColumnLayout {
                visible: page.showData
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.fillWidth: true
                spacing: 6

                Item { implicitHeight: 6 }
                Label { text: "按模型"; font.bold: true }
                RowLayout {
                    spacing: 8
                    Label { text: "模型"; font.bold: true; color: "#444444"; Layout.preferredWidth: page.colName }
                    HeaderCell { text: "输入" }
                    HeaderCell { text: "输出" }
                    HeaderCell { text: "缓存" }
                    HeaderCell { text: "费用（估算）"; Layout.preferredWidth: page.colCost }
                }
                Repeater {
                    model: page.showData ? page.usage.byModel : []
                    RowLayout {
                        required property var modelData
                        spacing: 8
                        Label {
                            text: modelData.model || "未知模型"
                            elide: Text.ElideRight
                            Layout.preferredWidth: page.colName
                        }
                        NumCell { text: page.fmtInt(modelData.input) }
                        NumCell { text: page.fmtInt(modelData.output) }
                        NumCell { text: page.fmtInt(modelData.cache) }
                        NumCell {
                            Layout.preferredWidth: page.colCost
                            text: modelData.priced ? page.fmtCost(modelData.cost) : "—"
                            color: modelData.priced ? palette.text : "#888888"
                        }
                    }
                }
                Rectangle { Layout.preferredWidth: page.colName + page.colNum * 3 + page.colCost + 32; height: 1; color: "#e5e5e5" }
                RowLayout {
                    spacing: 8
                    Label { text: "合计"; font.bold: true; Layout.preferredWidth: page.colName }
                    NumCell { font.bold: true; text: page.showData ? page.fmtInt(page.usage.total.input) : "" }
                    NumCell { font.bold: true; text: page.showData ? page.fmtInt(page.usage.total.output) : "" }
                    NumCell { font.bold: true; text: page.showData ? page.fmtInt(page.usage.total.cache) : "" }
                    NumCell {
                        font.bold: true
                        Layout.preferredWidth: page.colCost
                        text: page.showData ? page.fmtCost(page.usage.totalCost) : ""
                    }
                    Label {
                        text: "估算"
                        color: "#666666"
                        font.pixelSize: 12
                    }
                }
                Label {
                    visible: page.showData && page.usage.hasUnpriced === true
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#9d5d00"
                    font.pixelSize: 12
                    text: page.showData && page.usage.hasUnpriced
                          ? "存在未计价模型（" + page.usage.unpriced.join("、") + "），未计入合计费用；可在\"设置 → 模型单价\"中配置"
                          : ""
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#666666"
                    font.pixelSize: 12
                    text: "费用 = token 数 ÷ 1,000,000 × 单价（每百万 token），保留 4 位小数，仅供参考。"
                }
            }

            Item { implicitHeight: 12 }
        }
    }
}
