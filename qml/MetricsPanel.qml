import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts

// 泊位运行指标（需求 23.4 / 23.6 / 23.8）：当前 CPU/内存、5 分钟趋势图、累计数据与\"重置统计\"
ColumnLayout {
    id: root

    property string instanceId: ""
    property string status: "stopped"

    readonly property bool activeState: status === "starting" || status === "running" || status === "stopping"
    readonly property bool live: status === "running" || status === "external"

    property int rev: 0
    property int totalsRev: 0
    readonly property var cur: (rev, instanceId.length ? berth.metrics.current(instanceId) : ({ valid: false }))
    readonly property var points: (rev, instanceId.length && live ? berth.metrics.trend(instanceId) : [])
    readonly property var tot: (totalsRev, rev, instanceId.length ? berth.metrics.totals(instanceId) : ({}))
    property string resetError: ""

    Layout.fillWidth: true
    spacing: 4

    onInstanceIdChanged: { resetError = ""; rev++; totalsRev++ }
    onStatusChanged: { rev++; totalsRev++ }

    function fmtDuration(sec) {
        sec = Math.max(0, Math.floor(sec || 0))
        const h = Math.floor(sec / 3600)
        const m = Math.floor((sec % 3600) / 60)
        const s = sec % 60
        if (h > 0) return h + " 小时 " + m + " 分 " + s + " 秒"
        if (m > 0) return m + " 分 " + s + " 秒"
        return s + " 秒"
    }

    Connections {
        target: berth.metrics
        function onSampled(id) {
            if (id === root.instanceId) {
                root.rev++
                chart.requestPaint()
            }
        }
        function onTotalsChanged(id) {
            if (id === root.instanceId)
                root.totalsRev++
        }
    }

    RowLayout {
        spacing: 16
        Label { text: "运行指标"; font.bold: true }
        Label {
            text: "CPU：" + (root.live && root.cur.valid ? Number(root.cur.cpu).toFixed(1) + "%" : "—")
        }
        Label {
            text: "内存：" + (root.live && root.cur.valid ? Number(root.cur.memMb).toFixed(1) + " MB" : "—")
        }
        Label {
            visible: root.status === "external"
            text: "仅主进程"
            color: "#6b6b6b"
        }
    }

    // 5 分钟趋势图：CPU（蓝，左轴 0~max）与内存（绿，右轴 0~max），各自按窗口内最大值归一化
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 90
        visible: root.live
        color: "#fafafa"
        border.color: "#e5e5e5"
        border.width: 1
        radius: 2

        Canvas {
            id: chart
            anchors.fill: parent
            anchors.margins: 6
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections {
                target: root
                function onPointsChanged() { chart.requestPaint() }
            }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const pts = root.points
                if (!pts || pts.length < 2)
                    return
                const maxN = 150
                const w = width, h = height
                const stepX = w / (maxN - 1)
                const x0 = w - (pts.length - 1) * stepX
                let maxCpu = 1, maxMem = 1
                for (let i = 0; i < pts.length; ++i) {
                    maxCpu = Math.max(maxCpu, pts[i].cpu)
                    maxMem = Math.max(maxMem, pts[i].memMb)
                }
                function line(key, maxV, color) {
                    ctx.beginPath()
                    ctx.strokeStyle = color
                    ctx.lineWidth = 1.5
                    for (let i = 0; i < pts.length; ++i) {
                        const x = x0 + i * stepX
                        const y = h - (pts[i][key] / maxV) * (h - 2) - 1
                        if (i === 0) ctx.moveTo(x, y)
                        else ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                }
                line("memMb", maxMem, "#107c10")
                line("cpu", maxCpu, "#0067c0")
            }
        }
        Label {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 4
            font.pixelSize: 10
            color: "#6b6b6b"
            text: {
                const pts = root.points
                if (!pts || pts.length < 2)
                    return "趋势（最近 5 分钟）：采样中…"
                let maxCpu = 0, maxMem = 0
                for (let i = 0; i < pts.length; ++i) {
                    maxCpu = Math.max(maxCpu, pts[i].cpu)
                    maxMem = Math.max(maxMem, pts[i].memMb)
                }
                return "最近 5 分钟  ·  蓝 CPU 峰值 " + maxCpu.toFixed(1) + "%  ·  绿 内存峰值 " + maxMem.toFixed(1) + " MB"
            }
        }
    }

    RowLayout {
        spacing: 16
        Label { text: "累计运行：" + root.fmtDuration(root.tot.runtimeSec) }
        Label { text: "启动：" + (root.tot.starts || 0) + " 次" }
        Label { text: "崩溃：" + (root.tot.crashes || 0) + " 次" }
        Label {
            text: "最近退出：" + (root.tot.hasLastExit ? root.tot.lastExitCode + "（" + root.tot.lastExitAt + "）" : "—")
        }
        Item { Layout.fillWidth: true }
        // 禁用的按钮不接收悬停，外包一层 Item 承载 HoverHandler 与提示
        Item {
            implicitWidth: resetBtn.implicitWidth
            implicitHeight: resetBtn.implicitHeight
            HoverHandler { id: resetHover }
            ToolTip.visible: resetHover.hovered && root.activeState
            ToolTip.text: "泊位运行中，需先停止泊位才能重置统计"
            Button {
                id: resetBtn
                anchors.fill: parent
                text: "重置统计"
                enabled: !root.activeState && root.instanceId.length > 0
                onClicked: confirmReset.open()
            }
        }
    }
    Label {
        Layout.fillWidth: true
        visible: berth.metrics.loadNotice.length > 0
        wrapMode: Text.Wrap
        color: "#b45309"
        font.pixelSize: 11
        text: "统计已重置：" + berth.metrics.loadNotice
    }
    Label {
        Layout.fillWidth: true
        visible: root.resetError.length > 0
        wrapMode: Text.Wrap
        color: "#c42b1c"
        font.pixelSize: 11
        text: root.resetError
    }

    Dialog {
        id: confirmReset
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, parent ? parent.width - 48 : 420)
        standardButtons: Dialog.Ok | Dialog.Cancel
        title: "重置统计"

        onAccepted: {
            if (berth.metrics.reset(root.instanceId)) {
                root.resetError = ""
                root.totalsRev++
            } else {
                root.resetError = root.activeState
                        ? "泊位运行中，需先停止泊位才能重置统计"
                        : "重置失败：统计文件为只读"
            }
        }

        contentItem: Label {
            text: "将清零该泊位的累计运行时长、启动次数与崩溃次数，并清除最近退出记录。"
            wrapMode: Text.Wrap
        }
    }
}
