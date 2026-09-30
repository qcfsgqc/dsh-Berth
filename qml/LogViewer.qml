import QtQuick
import QtQuick.Controls.Windows
import QtQuick.Layouts
import QtQuick.Dialogs

// 日志页：搜索高亮与循环跳转、仅显示匹配行、级别过滤、暂停滚动、复制/清空/导出、文件大小
// 后端为 berth.logs（LogService）：tailLoaded 替换、moreLoaded 前置、appended 追加、cleared 清空
Dialog {
    id: dlg

    property string instanceId: ""
    property string instanceName: ""
    // 当前已加载的原始日志文本（不受过滤影响）
    property string rawText: ""
    // 生效中的关键字（输入后 300ms 防抖）
    property string keyword: ""
    // 过滤后显示的纯文本，及其中的匹配 [{pos, len}]
    property string viewText: ""
    property var matches: []
    property int currentMatch: -1
    property string errorText: ""
    property string infoText: ""
    property bool infoOk: true

    readonly property var logs: berth.logs

    title: "日志" + (instanceName.length ? "  ·  " + instanceName : "")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: parent ? parent.width - 48 : 900
    height: parent ? parent.height - 48 : 640
    standardButtons: Dialog.Close

    function openFor(id, name) {
        instanceId = id
        instanceName = name || ""
        rawText = ""
        errorText = ""
        infoText = ""
        searchField.text = ""
        keyword = ""
        onlyMatchBox.checked = false
        levelBox.currentIndex = 0
        pauseBox.checked = false
        rebuild("replace")
        open()
        logs.loadTail(id)
    }

    onClosed: {
        logs.close()
        instanceId = ""
    }

    function escapeHtml(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    // 按过滤条件生成显示行；返回 [{text, level}]
    function visibleLines() {
        const src = rawText.split("\n")
        // 末尾换行产生的空行不显示
        if (src.length && src[src.length - 1] === "")
            src.pop()
        const filter = levelBox.currentIndex
        const only = onlyMatchBox.checked && keyword.length > 0
        const out = []
        for (let i = 0; i < src.length; ++i) {
            let line = src[i]
            if (line.endsWith("\r"))
                line = line.slice(0, -1)
            if ((filter !== 0 || only) && !logs.linePasses(line, filter, only, keyword))
                continue
            out.push({ text: line, level: logs.lineLevel(line) })
        }
        return out
    }

    // 生成富文本：行级别着色 + 匹配高亮（当前匹配用更亮的底色）
    // RichText 中 <br> 计 1 个字符，与纯文本的 "\n" 下标一致，故 matches 的 pos 可直接用于定位
    function buildHtml(lines) {
        const parts = []
        let offset = 0
        let mi = 0
        for (let i = 0; i < lines.length; ++i) {
            const line = lines[i].text
            const end = offset + line.length
            let html = ""
            let p = 0
            while (mi < matches.length && matches[mi].pos < end) {
                const m = matches[mi]
                const s = m.pos - offset
                html += escapeHtml(line.slice(p, s))
                const bg = mi === currentMatch ? "#9e6a03" : "#613214"
                html += "<span style=\"background-color:" + bg + ";color:#ffffff\">"
                      + escapeHtml(line.substr(s, m.len)) + "</span>"
                p = s + m.len
                ++mi
            }
            html += escapeHtml(line.slice(p))
            const lv = lines[i].level
            if (lv === 2)
                html = "<span style=\"color:#f48771\">" + html + "</span>"
            else if (lv === 1)
                html = "<span style=\"color:#cca700\">" + html + "</span>"
            parts.push(html)
            offset = end + 1
        }
        return "<div style=\"white-space:pre-wrap\">" + parts.join("<br>") + "</div>"
    }

    // mode：replace（尾部加载，滚到底）、prepend（加载更多，保持可见行）、
    //       append（新内容，未暂停时滚到底）、filter（过滤/搜索变化，保持位置）
    function rebuild(mode) {
        const oldH = logFlick.contentHeight
        const oldY = logFlick.contentY
        const lines = visibleLines()
        viewText = lines.map(function(l) { return l.text }).join("\n")
        const found = keyword.length ? logs.find(viewText, keyword) : []
        const searchChanged = mode === "filter" || mode === "replace"
        matches = found
        if (!found.length)
            currentMatch = -1
        else if (searchChanged || currentMatch < 0)
            currentMatch = 0
        else if (currentMatch >= found.length)
            currentMatch = found.length - 1
        logView.text = buildHtml(lines)
        Qt.callLater(function() {
            if (mode === "prepend") {
                logFlick.contentY = Math.max(0, oldY + (logFlick.contentHeight - oldH))
            } else if (mode === "replace" || (mode === "append" && !pauseBox.checked)) {
                scrollToBottom()
            } else if (mode === "append") {
                logFlick.contentY = oldY
            } else if (mode === "filter" && currentMatch >= 0) {
                scrollToMatch()
            }
        })
    }

    function scrollToBottom() {
        logFlick.contentY = Math.max(0, logFlick.contentHeight - logFlick.height)
    }

    function scrollToMatch() {
        if (currentMatch < 0 || currentMatch >= matches.length)
            return
        const r = logView.positionToRectangle(matches[currentMatch].pos)
        if (r.y < logFlick.contentY || r.y + r.height > logFlick.contentY + logFlick.height)
            logFlick.contentY = Math.max(0, Math.min(r.y - logFlick.height / 3,
                                                     logFlick.contentHeight - logFlick.height))
    }

    function jump(next) {
        currentMatch = next ? logs.nextIndex(currentMatch, matches.length)
                            : logs.prevIndex(currentMatch, matches.length)
        const oldY = logFlick.contentY
        logView.text = buildHtml(visibleLines())
        Qt.callLater(function() {
            logFlick.contentY = oldY
            scrollToMatch()
        })
    }

    function copyText(s) {
        clipHelper.text = s
        clipHelper.selectAll()
        clipHelper.copy()
        clipHelper.text = ""
    }

    function showInfo(text, ok) {
        infoText = text
        infoOk = ok
        infoTimer.restart()
    }

    Connections {
        target: dlg.logs
        enabled: dlg.visible
        function onTailLoaded(text) {
            dlg.errorText = ""
            dlg.rawText = text
            dlg.rebuild("replace")
        }
        function onMoreLoaded(text) {
            dlg.rawText = text + dlg.rawText
            dlg.rebuild("prepend")
        }
        function onAppended(text) {
            dlg.rawText = dlg.rawText + text
            dlg.rebuild("append")
        }
        function onCleared() {
            dlg.rawText = ""
            dlg.rebuild("replace")
            dlg.showInfo("已清空", true)
        }
        function onExportFinished(ok, message) {
            dlg.showInfo(ok ? "已导出到 " + message : message, ok)
        }
        function onErrorOccurred(operation, message) {
            dlg.errorText = operation + "失败：" + message
        }
    }

    Timer {
        id: debounce
        interval: 300
        onTriggered: {
            dlg.keyword = searchField.text
            dlg.rebuild("filter")
        }
    }

    Timer {
        id: infoTimer
        interval: 4000
        onTriggered: dlg.infoText = ""
    }

    // 复制用的隐藏编辑框（QML 无直接剪贴板 API）
    TextEdit {
        id: clipHelper
        visible: false
        textFormat: TextEdit.PlainText
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // 搜索与过滤
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: searchField
                Layout.preferredWidth: 260
                placeholderText: "搜索（不区分大小写）"
                maximumLength: 256
                selectByMouse: true
                onTextChanged: debounce.restart()
                Keys.onReturnPressed: if (dlg.matches.length) dlg.jump(true)
            }
            Label {
                text: dlg.keyword.length
                      ? (dlg.matches.length ? (dlg.currentMatch + 1) + "/" + dlg.matches.length : "0/0")
                      : ""
                Layout.minimumWidth: 48
            }
            Button { text: "上一个"; enabled: dlg.matches.length > 0; onClicked: dlg.jump(false) }
            Button { text: "下一个"; enabled: dlg.matches.length > 0; onClicked: dlg.jump(true) }
            CheckBox {
                id: onlyMatchBox
                text: "仅显示匹配行"
                onToggled: dlg.rebuild("filter")
            }
            Label { text: "级别" }
            ComboBox {
                id: levelBox
                model: ["全部", "错误", "警告"]
                onActivated: dlg.rebuild("filter")
            }
            Item { Layout.fillWidth: true }
            CheckBox {
                id: pauseBox
                text: "暂停滚动"
                onToggled: if (!checked) dlg.scrollToBottom()
            }
        }

        // 操作
        RowLayout {
            Layout.fillWidth: true
            Button {
                text: "复制所选"
                enabled: logView.selectedText.length > 0
                onClicked: dlg.copyText(logView.selectedText.replace(/[\u2028\u2029]/g, "\n"))
            }
            Button {
                text: "复制全部"
                enabled: dlg.viewText.length > 0
                onClicked: {
                    dlg.copyText(dlg.viewText)
                    dlg.showInfo("已复制", true)
                }
            }
            Button {
                text: "清空"
                enabled: dlg.instanceId.length > 0
                onClicked: {
                    if (dlg.logs.clearNeedsConfirm())
                        clearConfirm.open()
                    else
                        dlg.logs.clear()
                }
            }
            Button {
                text: dlg.logs.exporting ? "导出中…" : "导出"
                enabled: dlg.instanceId.length > 0 && !dlg.logs.exporting
                onClicked: exportDialog.open()
            }
            Button {
                text: "外部打开"
                enabled: dlg.instanceId.length > 0
                onClicked: berth.openLog(dlg.instanceId)
            }
            Item { Layout.fillWidth: true }
            Label {
                text: dlg.infoText
                visible: dlg.infoText.length > 0
                color: dlg.infoOk ? "#2e7d32" : "#c62828"
                elide: Text.ElideMiddle
                Layout.maximumWidth: 360
            }
            Label { text: "文件大小：" + dlg.logs.sizeText }
        }

        Label {
            Layout.fillWidth: true
            visible: dlg.errorText.length > 0
            text: dlg.errorText
            color: "#c62828"
            wrapMode: Text.Wrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1e1e1e"
            radius: 4

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                Button {
                    Layout.alignment: Qt.AlignHCenter
                    visible: dlg.logs.canLoadMore
                    enabled: !dlg.logs.loading
                    text: dlg.logs.loading ? "加载中…" : "加载更多"
                    onClicked: dlg.logs.loadMore()
                }

                Flickable {
                    id: logFlick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: logView.contentHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    TextEdit {
                        id: logView
                        width: logFlick.width - 12
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        textFormat: TextEdit.RichText
                        color: "#d4d4d4"
                        selectionColor: "#264f78"
                        font.family: "Consolas"
                        font.pixelSize: 12
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: dlg.viewText.length === 0 && !dlg.logs.loading
                text: !dlg.logs.fileExists ? "日志文件不存在"
                      : dlg.rawText.length === 0 ? "暂无日志" : "没有符合条件的行"
                color: "#808080"
            }
        }
    }

    // 泊位不处于 Active_State 时清空会截断日志文件，先确认
    Dialog {
        id: clearConfirm
        title: "清空日志"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, parent ? parent.width - 48 : 420)
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: "泊位未在运行，清空会把日志文件截断为 0 字节，且无法恢复。确定清空？"
        }
        onAccepted: dlg.logs.clear()
    }

    FileDialog {
        id: exportDialog
        title: "导出日志"
        fileMode: FileDialog.SaveFile
        nameFilters: ["日志文件 (*.log)", "文本文件 (*.txt)", "所有文件 (*)"]
        defaultSuffix: "log"
        onAccepted: dlg.logs.exportTo(selectedFile.toString())
    }
}
