import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 主窗口顶部汇总面板（需求 24）：泊位总数、运行中、失败、内存总和、CPU 总和。
// 数据来自 berth.summary()；泊位状态/增删或 Process_Metrics 新采样时刷新（Qt.callLater 合并）。
// 点击"运行中"/"失败"数字切换列表筛选；可用更新数量在 32.2 接入前隐藏。
Rectangle {
    id: panel

    // 当前筛选：""（无）、"running"、"failed"；由 Main 持有，点击时发 filterToggled
    property string statusFilter: ""
    signal filterToggled(string kind)

    // 可用更新数量（32.2 接入 Update_Center 后赋值并开启）
    property bool updateCountEnabled: false
    property int updateCount: 0
    signal updateCountClicked()

    property var stats: ({ total: 0, running: 0, failed: 0, memText: "0 MB", cpuText: "0.0%" })

    function refresh() { stats = berth.summary() }
    function scheduleRefresh() { Qt.callLater(panel.refresh) }

    implicitHeight: 48
    color: "#fafafa"
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#e5e5e5" }

    component Metric: Rectangle {
        id: metric
        property string title: ""
        property string value: ""
        property color valueColor: "#1a1a1a"
        property bool clickable: false
        property bool selected: false
        signal clicked()

        Layout.preferredHeight: 36
        implicitWidth: col.implicitWidth + 20
        radius: 4
        color: selected ? "#cce4f7" : (clickable && area.containsMouse ? "#eef4fc" : "transparent")
        border.width: selected ? 1 : 0
        border.color: "#0078d4"

        Accessible.role: clickable ? Accessible.Button : Accessible.StaticText
        Accessible.name: title + " " + value
        Accessible.onPressAction: if (clickable) metric.clicked()

        Column {
            id: col
            anchors.centerIn: parent
            spacing: 0
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: metric.title
                color: "#666666"
                font.pixelSize: 11
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: metric.value
                color: metric.valueColor
                font.pixelSize: 15
                font.bold: true
            }
        }
        MouseArea {
            id: area
            anchors.fill: parent
            enabled: metric.clickable
            hoverEnabled: true
            cursorShape: metric.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: metric.clicked()
        }
        ToolTip.visible: clickable && area.containsMouse
        ToolTip.text: selected ? "再次点击取消筛选" : "点击筛选泊位列表"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        Metric {
            title: "泊位"
            value: String(panel.stats.total)
        }
        Metric {
            title: "运行中"
            value: String(panel.stats.running)
            valueColor: "#107c10"
            clickable: true
            selected: panel.statusFilter === "running"
            onClicked: panel.filterToggled("running")
        }
        Metric {
            title: "失败"
            value: String(panel.stats.failed)
            valueColor: panel.stats.failed > 0 ? "#c42b1c" : "#1a1a1a"
            clickable: true
            selected: panel.statusFilter === "failed"
            onClicked: panel.filterToggled("failed")
        }
        Metric {
            title: "内存"
            value: panel.stats.memText
        }
        Metric {
            title: "CPU"
            value: panel.stats.cpuText
        }
        Item { Layout.fillWidth: true }
        Metric {
            visible: panel.updateCountEnabled && panel.updateCount >= 1
            title: "可用更新"
            value: String(panel.updateCount)
            valueColor: "#0078d4"
            clickable: true
            onClicked: panel.updateCountClicked()
        }
    }

    Connections {
        target: berth.instances
        function onDataChanged() { panel.scheduleRefresh() }
        function onRowsInserted() { panel.scheduleRefresh() }
        function onRowsRemoved() { panel.scheduleRefresh() }
        function onModelReset() { panel.scheduleRefresh() }
    }
    Connections {
        target: berth
        function onInstanceStatusChanged(id, status) { panel.scheduleRefresh() }
    }
    Connections {
        target: berth.metrics
        function onSamplesUpdated() { panel.scheduleRefresh() }
    }
    Component.onCompleted: refresh()
}
