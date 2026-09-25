// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: one recycled row carries the task label and interval, so
// timeline and grid identity both come from GanttTaskModel.taskId. The owner
// shares a SelectionModel with its DataTable and applies any grid sort to a
// source/proxy model before handing it to both views.
//
// AGENT-NOTE (D-302): parts live in GanttBackdrop (stripes, non-working
// days, markers), GanttRuler (header), GanttRow (one task) and GanttLinks
// (dependency canvas); all of them map instants through this root's xFor()
// so the "working" and "calendar" time scales stay one code path.
T.Control {
    id: root
    objectName: "ganttView"
    property Tk.GanttTaskModel taskModel: null
    property Tk.GanttCalendar calendar: taskModel ? taskModel.calendar : null
    property Tk.SelectionModel selectionModel: null
    // Provider contract: valueForTask(taskId) -> [0,1] or -1 for missing.
    property var allocationProvider: null
    property string visibleStartUtc: "0"
    // "working": x counts working minutes (non-working time has no width).
    // "calendar": x is elapsed time at pixelsPerDay; non-working days shaded.
    property string timeScale: "working"
    property real pixelsPerWorkingMinute: Tk.Theme.size.row / 60
    property real pixelsPerDay: Tk.Theme.size.control
    property real labelWidth: Tk.Theme.size.labelWidth
    property real rowHeight: Tk.Theme.size.row
    property real headerHeight: Tk.Theme.size.timeRuler
    // Vertical lines such as today or a status date: [{utc, label, role}],
    // role = a Tk.Theme.color role name (default "accent").
    property var markers: []
    property string tooltip: ""
    property alias contentY: rows.contentY
    readonly property real contentHeight: rows.contentHeight
    readonly property bool calendarScale: timeScale === "calendar"
    readonly property real trackWidth: Math.max(0, width - labelWidth)
    readonly property string visibleEndUtc: calendarScale
        ? String(Math.round(Number(visibleStartUtc) + trackWidth / Math.max(0.0001, pixelsPerDay) * 86400000))
        : (calendar ? calendar.addWorkingMinutes(visibleStartUtc,
              Math.round(trackWidth / Math.max(0.0001, pixelsPerWorkingMinute))) : visibleStartUtc)
    readonly property real tickStepMinutes: {
        const minuteWidth = Math.max(0.0001, pixelsPerWorkingMinute)
        const wanted = Tk.Theme.size.fieldWidth * 1.5 / minuteWidth
        return 60 * Math.pow(2, Math.max(0, Math.ceil(Math.log(Math.max(1, wanted / 60)) / Math.LN2)))
    }
    readonly property int count: taskModel ? taskModel.count : 0
    readonly property bool previewActive: taskModel && taskModel.previewActive
    // Bumped when the calendar's rules change so mapped geometry re-reads it.
    property int calendarRevision: 0
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

    Connections {
        target: root.calendar
        function onChanged() { root.calendarRevision++ }
    }

    // Track x of an instant, 0 at visibleStartUtc.
    function xFor(utc) {
        if (!utc) return 0
        if (calendarScale)
            return (Number(utc) - Number(visibleStartUtc)) / 86400000 * pixelsPerDay
        return calendar ? calendar.workingMinutesBetween(visibleStartUtc, utc)
                          * pixelsPerWorkingMinute : 0
    }
    function endFor(record) {
        if (record && record.finishUtc) return record.finishUtc
        return calendar && record && record.startUtc ? calendar.addWorkingMinutes(
            record.startUtc, record.durationMinutes) : ""
    }
    function baselineEndFor(record) {
        if (!record || !record.baselineStartUtc) return ""
        if (record.baselineFinishUtc) return record.baselineFinishUtc
        return calendar ? calendar.addWorkingMinutes(record.baselineStartUtc,
            record.baselineDurationMinutes !== undefined ? record.baselineDurationMinutes
                                                         : record.durationMinutes) : ""
    }
    function kindOf(record) {
        if (!record) return "task"
        if (record.kind === "summary" || record.kind === "milestone") return record.kind
        const zero = Number(record.durationMinutes || 0) === 0
            && (!record.finishUtc || record.finishUtc === record.startUtc)
        return zero ? "milestone" : "task"
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
    // Shifts the visible window by `minutes` (elapsed on the calendar scale,
    // working minutes on the working scale).
    function scrollBy(minutes) {
        const step = Math.round(Number(minutes))
        if (!isFinite(step) || step === 0) return
        if (calendarScale) {
            visibleStartUtc = String(Math.round(Number(visibleStartUtc) + step * 60000))
        } else if (calendar) {
            const next = calendar.addWorkingMinutes(visibleStartUtc, step)
            if (next.length) visibleStartUtc = next
        }
    }
    function wheelStep() { return calendarScale ? 1440 : tickStepMinutes }

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
        Tk.GanttBackdrop {
            id: backdrop
            view: root
            y: root.headerHeight
            width: parent.width
            height: Math.max(0, parent.height - root.headerHeight)
            contentY: rows.contentY
        }
        ListView {
            id: rows
            objectName: "ganttRows"
            x: 0
            y: root.headerHeight
            width: parent.width
            height: Math.max(0, parent.height - root.headerHeight)
            model: root.taskModel
            clip: true
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            delegate: Tk.GanttRow { view: root; width: rows.width }
            WheelHandler {
                orientation: Qt.Horizontal
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                target: null
                onWheel: function(event) {
                    root.scrollBy(-event.angleDelta.x / 120 * root.wheelStep())
                }
            }
            WheelHandler {
                orientation: Qt.Vertical
                acceptedModifiers: Qt.ShiftModifier
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                target: null
                onWheel: function(event) {
                    root.scrollBy(-event.angleDelta.y / 120 * root.wheelStep())
                }
            }
        }
        Tk.GanttLinks {
            view: root
            x: root.labelWidth
            y: root.headerHeight
            width: root.trackWidth
            height: rows.height
            contentY: rows.contentY
        }
        Tk.GanttMarkers {
            view: root
            x: root.labelWidth
            y: root.headerHeight
            width: root.trackWidth
            height: rows.height
        }
        Tk.GanttRuler {
            view: root
            width: parent.width
            height: root.headerHeight
        }
    }
}
