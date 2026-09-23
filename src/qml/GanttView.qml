// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: one recycled row carries the task label and interval, so
// timeline and grid identity both come from GanttTaskModel.taskId. The owner
// shares a SelectionModel with its DataTable and applies any grid sort to a
// source/proxy model before handing it to both views.
T.Control {
    id: root
    objectName: "ganttView"
    property Tk.GanttTaskModel taskModel: null
    property Tk.GanttCalendar calendar: taskModel ? taskModel.calendar : null
    property Tk.SelectionModel selectionModel: null
    // Provider contract: valueForTask(taskId) -> [0,1] or -1 for missing.
    property var allocationProvider: null
    property string visibleStartUtc: "0"
    property real pixelsPerWorkingMinute: Tk.Theme.size.row / 60
    property real labelWidth: Tk.Theme.size.labelWidth
    property string tooltip: ""
    readonly property real tickStepMinutes: {
        const minuteWidth = Math.max(0.0001, pixelsPerWorkingMinute)
        const wanted = Tk.Theme.size.fieldWidth * 1.5 / minuteWidth
        return 60 * Math.pow(2, Math.max(0, Math.ceil(Math.log(Math.max(1, wanted / 60)) / Math.LN2)))
    }
    readonly property int count: taskModel ? taskModel.count : 0
    readonly property bool previewActive: taskModel && taskModel.previewActive
    signal taskActivated(string taskId)
    signal previewRequested(string taskId, string startUtc)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight
    padding: 0
    clip: true
    activeFocusOnTab: enabled
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip.length ? tooltip : qsTr("Gantt schedule")
    Accessible.description: qsTr("Up and Down select tasks. Left and Right preview a one hour move. Enter commits; Escape cancels; Control Z requests undo.")
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: Tk.Theme.color.border
        border.width: Tk.Theme.size.border
    }

    function xFor(utc) {
        return calendar && utc ? calendar.workingMinutesBetween(visibleStartUtc, utc)
                                  * pixelsPerWorkingMinute : 0
    }
    function endFor(record) {
        return calendar && record && record.startUtc ? calendar.addWorkingMinutes(
            record.startUtc, record.durationMinutes) : ""
    }
    function allocationFor(taskId) {
        if (!allocationProvider || typeof allocationProvider.valueForTask !== "function") return -1
        const amount = Number(allocationProvider.valueForTask(taskId))
        return isFinite(amount) && amount >= 0 ? Math.min(1, amount) : -1
    }
    function selectId(taskId) {
        if (selectionModel) selectionModel.select(taskId)
        taskActivated(taskId)
    }
    function previewMove(taskId, startUtc) {
        if (!taskModel) return false
        const accepted = taskModel.previewMove(taskId, startUtc)
        if (accepted) previewRequested(taskId, startUtc)
        return accepted
    }
    function commitPreview() { return taskModel ? taskModel.commitPreview() : false }
    function cancelPreview() { if (taskModel) taskModel.cancelPreview() }
    function undo() { return taskModel ? taskModel.undo() : false }

    Keys.onPressed: function(event) {
        if (!root.taskModel || !root.enabled) return
        let row = root.selectionModel ? root.taskModel.indexOfId(root.selectionModel.currentId) : -1
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
            row = Math.max(0, Math.min(root.count - 1, row + (event.key === Qt.Key_Down ? 1 : -1)))
            if (row >= 0 && root.count) {
                root.selectId(root.taskModel.get(row).taskId)
                rows.positionViewAtIndex(row, ListView.Contain)
            }
            event.accepted = true
        } else if ((event.key === Qt.Key_Left || event.key === Qt.Key_Right) && row >= 0) {
            const record = root.taskModel.get(row)
            const offset = event.key === Qt.Key_Right ? 60 : -60
            root.previewMove(record.taskId, root.calendar.addWorkingMinutes(record.startUtc, offset))
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.commitPreview(); event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            root.cancelPreview(); event.accepted = true
        } else if (event.key === Qt.Key_Z && (event.modifiers & Qt.ControlModifier)) {
            root.undo(); event.accepted = true
        }
    }

    contentItem: Item {
        Rectangle {
            id: header
            objectName: "ganttHeader"
            width: parent.width
            height: Tk.Theme.size.timeRuler
            color: Tk.Theme.color.headerBg
            Tk.Label {
                x: Tk.Theme.space.sm
                width: root.labelWidth - Tk.Theme.space.sm * 2
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Task")
                color: Tk.Theme.color.headerText
            }
            Repeater {
                model: Math.min(100, Math.ceil((header.width - root.labelWidth)
                                               / (root.tickStepMinutes
                                                  * Math.max(0.0001, root.pixelsPerWorkingMinute))) + 1)
                delegate: Tk.Label {
                    required property int index
                    x: root.labelWidth + index * root.tickStepMinutes
                       * root.pixelsPerWorkingMinute
                    anchors.verticalCenter: header.verticalCenter
                    text: root.calendar ? root.calendar.formatLocal(root.calendar.addWorkingMinutes(
                          root.visibleStartUtc, index * root.tickStepMinutes)) : ""
                    color: Tk.Theme.color.headerText
                    font.pixelSize: Tk.Theme.font.caption
                }
            }
        }
        ListView {
            id: rows
            objectName: "ganttRows"
            x: 0
            y: header.height
            width: parent.width
            height: Math.max(0, parent.height - header.height)
            model: root.taskModel
            clip: true
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            delegate: Item {
                id: rowItem
                required property int index
                property var record: root.taskModel ? root.taskModel.displayGet(index) : ({})
                readonly property string taskId: String(record.taskId || "")
                readonly property string endUtc: root.endFor(record)
                readonly property real barX: root.xFor(record.startUtc)
                readonly property real barWidth: Math.max(Tk.Theme.size.border,
                    root.xFor(endUtc) - barX)
                objectName: "ganttRow"
                width: rows.width
                height: Tk.Theme.size.row
                Accessible.role: Accessible.ListItem
                Accessible.name: (record.label || taskId) + ", "
                    + (root.calendar && record.startUtc ? root.calendar.formatLocal(record.startUtc) : "")
                Accessible.description: qsTr("%1 percent complete; %2 dependencies")
                    .arg(Math.round(Number(record.progress || 0) * 100))
                    .arg(record.dependencies ? record.dependencies.length : 0)
                Accessible.selected: root.selectionModel && root.selectionModel.currentId === taskId
                Connections {
                    target: root.taskModel
                    function onPreviewChanged() { rowItem.record = root.taskModel.displayGet(rowItem.index) }
                }
                Rectangle {
                    anchors.fill: parent
                    color: rowItem.index % 2 ? Tk.Theme.color.panelAlt : Tk.Theme.color.panel
                    border.color: Tk.Theme.color.divider
                    border.width: Tk.Theme.size.border
                }
                Tk.Label {
                    objectName: "ganttTaskLabel"
                    x: Tk.Theme.space.sm
                    width: root.labelWidth - Tk.Theme.space.sm * 2
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.record.label || rowItem.taskId
                    color: root.selectionModel && root.selectionModel.currentId === rowItem.taskId
                           ? Tk.Theme.color.accentText : Tk.Theme.color.text
                }
                Item {
                    id: track
                    objectName: "ganttTrack"
                    x: root.labelWidth
                    width: Math.max(0, parent.width - x)
                    height: parent.height
                    clip: true
                    Rectangle {
                        objectName: "ganttBaseline"
                        visible: !!rowItem.record.baselineStartUtc
                        x: root.xFor(rowItem.record.baselineStartUtc)
                        y: parent.height - Tk.Theme.size.border * 3
                        width: !rowItem.record.baselineStartUtc ? 0 : Math.max(
                            Tk.Theme.size.border,
                            root.xFor(root.calendar ? root.calendar.addWorkingMinutes(
                                rowItem.record.baselineStartUtc,
                                rowItem.record.baselineDurationMinutes !== undefined
                                    ? rowItem.record.baselineDurationMinutes
                                    : rowItem.record.durationMinutes) : "") - x)
                        height: Tk.Theme.size.border * 2
                        color: Tk.Theme.color.textMuted
                    }
                    Rectangle {
                        objectName: "ganttAllocation"
                        visible: root.allocationFor(rowItem.taskId) >= 0
                        x: rowItem.barX
                        y: Tk.Theme.size.border
                        width: rowItem.barWidth * root.allocationFor(rowItem.taskId)
                        height: Tk.Theme.size.border * 2
                        color: Tk.Theme.color.warning
                    }
                    Rectangle {
                        id: bar
                        objectName: "ganttTaskBar"
                        x: rowItem.barX
                        y: Tk.Theme.space.xs
                        width: rowItem.barWidth
                        height: parent.height - Tk.Theme.space.xs * 2
                        radius: Tk.Theme.radius.sm
                        color: root.previewActive ? Tk.Theme.color.accentSubtle : Tk.Theme.color.accent
                        border.color: Tk.Theme.color.accent
                        border.width: Tk.Theme.size.border
                        Rectangle {
                            objectName: "ganttProgress"
                            width: parent.width * Math.max(0, Math.min(1, Number(rowItem.record.progress || 0)))
                            height: parent.height
                            radius: parent.radius
                            color: Tk.Theme.color.accent
                        }
                        TapHandler {
                            onTapped: {
                                root.selectId(rowItem.taskId)
                                root.forceActiveFocus()
                            }
                            onDoubleTapped: root.taskActivated(rowItem.taskId)
                        }
                        DragHandler {
                            id: mover
                            target: null
                            xAxis.enabled: true
                            yAxis.enabled: false
                            onTranslationChanged: {
                                if (!active || !root.calendar || !root.taskModel) return
                                const original = root.taskModel.get(rowItem.index)
                                const minutes = Math.round(translation.x / root.pixelsPerWorkingMinute)
                                root.previewMove(rowItem.taskId,
                                    root.calendar.addWorkingMinutes(original.startUtc, minutes))
                            }
                        }
                    }
                }
            }
        }
        Canvas {
            id: links
            objectName: "ganttDependencies"
            x: root.labelWidth
            y: header.height
            width: Math.max(0, parent.width - x)
            height: rows.height
            visible: root.count > 0
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                if (!root.taskModel) return
                ctx.strokeStyle = Tk.Theme.color.textMuted
                ctx.lineWidth = Tk.Theme.size.border
                const first = Math.max(0, Math.floor(rows.contentY / Tk.Theme.size.row))
                const last = Math.min(root.count, Math.ceil((rows.contentY + rows.height)
                                                         / Tk.Theme.size.row))
                for (let i = first; i < last; ++i) {
                    const task = root.taskModel.displayGet(i)
                    const deps = task.dependencies || []
                    const y2 = i * Tk.Theme.size.row - rows.contentY + Tk.Theme.size.row / 2
                    for (const id of deps) {
                        const p = root.taskModel.indexOfId(String(id))
                        if (p < 0) continue
                        const predecessor = root.taskModel.displayGet(p)
                        const x1 = root.xFor(root.endFor(predecessor))
                        const x2 = root.xFor(task.startUtc)
                        const y1 = p * Tk.Theme.size.row - rows.contentY + Tk.Theme.size.row / 2
                        const elbow = Math.max(x1, x2) + Tk.Theme.space.sm
                        ctx.beginPath()
                        ctx.moveTo(x1, y1)
                        ctx.lineTo(elbow, y1)
                        ctx.lineTo(elbow, y2)
                        ctx.lineTo(x2, y2)
                        ctx.stroke()
                    }
                }
            }
            Connections {
                target: root.taskModel
                function onPreviewChanged() { links.requestPaint() }
                function onRowsChanged() { links.requestPaint() }
            }
            Connections { target: rows; function onContentYChanged() { links.requestPaint() } }
            Connections { target: root; function onVisibleStartUtcChanged() { links.requestPaint() } }
        }
    }
}
