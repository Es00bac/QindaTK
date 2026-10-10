// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// Internal part of GanttView (D-303, D-313): what a task row draws around
// its bar — the baseline lane and the second baseline lane under it, the
// slack bars after it, the deadline marker and the allocation overlay. It
// fills GanttRow's track and reads every geometry from the row, so lane
// placement has one source (GanttRow.laneY).
Item {
    id: lanes
    // AGENT-NOTE: `var`, not the owner's type: a part typed with the control
    // that instantiates it is a cyclic type, which qmllint reports as
    // "cannot assign X to X" (the GridCellDelegate / GridFillHandle precedent).
    required property var row
    readonly property var view: row.view
    readonly property var record: row.record
    readonly property real laneHeight: row.laneHeight

    // Baseline lanes: a muted bar per lane (a hollow diamond for a milestone
    // on the first lane, a tick on the second).
    Rectangle {
        objectName: "ganttBaseline"
        visible: lanes.row.hasBaseline && lanes.row.kind !== "milestone"
        readonly property real startX: { void lanes.view.calendarRevision; return lanes.view.xFor(lanes.record.baselineStartUtc) }
        x: startX
        y: lanes.row.laneY(0)
        width: !lanes.row.hasBaseline ? 0 : Math.max(Tk.Theme.size.border,
            lanes.view.xFor(lanes.view.baselineEndFor(lanes.record)) - startX)
        height: lanes.laneHeight
        radius: Tk.Theme.radius.sm / 2
        color: Tk.Theme.color.textMuted
        opacity: Tk.Theme.opacity.muted
    }
    Rectangle {
        objectName: "ganttBaseline"
        visible: lanes.row.hasBaseline && lanes.row.kind === "milestone"
        readonly property real side: Math.round(lanes.row.diamond * 0.6)
        x: lanes.view.xFor(lanes.record.baselineStartUtc) - side / 2
        y: lanes.row.height - side - Tk.Theme.space.xs
        width: side
        height: side
        rotation: 45
        color: "transparent"
        border.color: Tk.Theme.color.textMuted
        border.width: Tk.Theme.size.border
    }
    Rectangle {
        objectName: "ganttBaseline2"
        visible: lanes.row.hasBaseline2 && lanes.row.kind !== "group"
        readonly property bool point: lanes.row.kind === "milestone"
        readonly property real startX: { void lanes.view.calendarRevision; return lanes.view.xFor(lanes.record.baseline2StartUtc) }
        x: point ? startX - width / 2 : startX
        y: lanes.row.laneY(lanes.row.hasBaseline ? 1 : 0)
        width: point || !lanes.row.hasBaseline2 ? Tk.Theme.size.border * 2 : Math.max(Tk.Theme.size.border,
            lanes.view.xFor(lanes.view.baseline2EndFor(lanes.record)) - startX)
        height: lanes.laneHeight
        radius: Tk.Theme.radius.sm / 2
        color: Tk.Theme.color.info
        opacity: Tk.Theme.opacity.muted
    }

    // Slack after the bar: total slack (muted) with free slack over its start.
    Repeater {
        model: [{ key: "slackFinishUtc", name: "ganttSlack", role: "textMuted" },
                { key: "freeSlackFinishUtc", name: "ganttFreeSlack", role: "success" }]
        delegate: Rectangle {
            id: slack
            required property var modelData
            readonly property string until: String(lanes.record[modelData.key] || "")
            readonly property real endX: { void lanes.view.calendarRevision; return until.length ? lanes.view.xFor(until) : 0 }
            objectName: modelData.name
            visible: until.length > 0 && endX > lanes.row.barEnd
                     && (lanes.row.kind === "task" || lanes.row.kind === "milestone")
            x: lanes.row.barEnd
            width: Math.max(0, endX - lanes.row.barEnd)
            height: Math.max(Tk.Theme.size.border * 2, Math.round(lanes.row.barHeight / 4))
            y: lanes.row.barY + lanes.row.barHeight / 2 - height / 2
            color: Tk.Theme.color[modelData.role]
        }
    }

    // Deadline: a down arrow at the instant, danger once the bar ends after it.
    Tk.Icon {
        objectName: "ganttDeadline"
        readonly property string at: String(lanes.record.deadlineUtc || "")
        readonly property bool missed: at.length > 0 && lanes.row.endUtc.length > 0
                                       && Number(lanes.row.endUtc) > Number(at)
        readonly property real atX: { void lanes.view.calendarRevision; return at.length ? lanes.view.xFor(at) : 0 }
        visible: at.length > 0 && lanes.row.kind !== "group"
        name: "arrow-down"
        size: Tk.Theme.size.iconSm
        x: atX - width / 2
        y: Math.max(0, lanes.row.barY + lanes.row.barHeight / 2 - height / 2)
        color: missed ? Tk.Theme.color.danger : Tk.Theme.color.success
        Accessible.role: Accessible.Indicator
        Accessible.name: missed ? qsTr("Deadline missed") : qsTr("Deadline")
    }

    Rectangle {
        objectName: "ganttAllocation"
        visible: lanes.view.allocationFor(lanes.row.taskId) >= 0 && lanes.row.kind === "task"
        x: lanes.row.barX
        y: Tk.Theme.size.border
        width: lanes.row.barWidth * lanes.view.allocationFor(lanes.row.taskId)
        height: Tk.Theme.size.border * 2
        color: Tk.Theme.color.warning
    }
}
