// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// Internal part of GanttView (D-303): one recycled task row — the label
// column, the baseline, the bar (task, summary bracket or milestone
// diamond), progress, allocation overlay and the bar text. Stripes are the
// backdrop's; a row paints only its selection wash.
Item {
    id: rowItem
    required property Tk.GanttView view
    required property int index
    // AGENT-GUARD: a binding, never assigned — recycled delegates change
    // `index`, and displayGet() is not notifiable, so previews bump a counter.
    property int previewRevision: 0
    readonly property var record: {
        void previewRevision
        return view.taskModel ? view.taskModel.displayGet(index) : ({})
    }
    readonly property string taskId: String(record.taskId || "")
    readonly property string kind: view.kindOf(record)
    readonly property bool critical: record.critical === true
    readonly property bool inactive: record.inactive === true
    readonly property bool selected: view.selectionModel !== null && view.selectionModel.currentId === taskId
    readonly property bool hasBaseline: !!record.baselineStartUtc
    readonly property string endUtc: { void view.calendarRevision; return view.endFor(record) }
    readonly property real barX: { void view.calendarRevision; return view.xFor(record.startUtc) }
    readonly property real barWidth: Math.max(Tk.Theme.size.border, view.xFor(endUtc) - barX)
    // Lanes: the bar sits high when a baseline shares the row, centred otherwise.
    readonly property real barHeight: Math.round(height * (hasBaseline ? 0.42 : 0.5))
    readonly property real barY: hasBaseline ? Math.round(height * 0.14) : Math.round((height - barHeight) / 2)
    readonly property real diamond: Math.round(height * (hasBaseline ? 0.5 : 0.58))
    readonly property real barEnd: kind === "milestone" ? barX + diamond / 2 : barX + barWidth

    objectName: "ganttRow"
    height: view.rowHeight
    Accessible.role: Accessible.ListItem
    Accessible.name: (record.label || taskId) + ", "
        + (view.calendar && record.startUtc ? view.calendar.formatLocal(record.startUtc) : "")
    Accessible.description: [
        kind === "summary" ? qsTr("summary") : kind === "milestone" ? qsTr("milestone") : "",
        critical ? qsTr("critical") : "", inactive ? qsTr("inactive") : "",
        qsTr("%1 percent complete; %2 dependencies")
            .arg(Math.round(Number(record.progress || 0) * 100))
            .arg(record.dependencies ? record.dependencies.length : 0)
    ].filter(function(part) { return part.length > 0 }).join("; ")
    Accessible.selected: selected

    Connections {
        target: rowItem.view.taskModel
        function onPreviewChanged() { rowItem.previewRevision++ }
    }

    Rectangle {
        objectName: "ganttRowSelection"
        anchors.fill: parent
        visible: rowItem.selected
        color: Tk.Theme.color.selection
    }
    TapHandler {
        onTapped: {
            rowItem.view.selectId(rowItem.taskId)
            rowItem.view.forceActiveFocus()
        }
        onDoubleTapped: rowItem.view.taskActivated(rowItem.taskId)
    }

    Tk.Label {
        objectName: "ganttTaskLabel"
        visible: rowItem.view.labelWidth > 0
        x: Tk.Theme.space.sm + Tk.Theme.space.lg * Math.max(0, Number(rowItem.record.outlineLevel || 0))
        width: Math.max(0, rowItem.view.labelWidth - x - Tk.Theme.space.sm)
        height: parent.height
        text: rowItem.record.label || rowItem.taskId
        font.bold: rowItem.kind === "summary"
        color: rowItem.selected ? Tk.Theme.color.accentText
             : rowItem.inactive ? Tk.Theme.color.textMuted : Tk.Theme.color.text
    }

    Item {
        id: track
        objectName: "ganttTrack"
        x: rowItem.view.labelWidth
        width: Math.max(0, parent.width - x)
        height: parent.height
        clip: true

        Rectangle {
            objectName: "ganttBaseline"
            visible: rowItem.hasBaseline && rowItem.kind !== "milestone"
            readonly property real startX: { void rowItem.view.calendarRevision; return rowItem.view.xFor(rowItem.record.baselineStartUtc) }
            x: startX
            y: rowItem.barY + rowItem.barHeight + Tk.Theme.space.xs
            width: !rowItem.hasBaseline ? 0 : Math.max(Tk.Theme.size.border,
                rowItem.view.xFor(rowItem.view.baselineEndFor(rowItem.record)) - startX)
            height: Math.max(Tk.Theme.size.border * 2, Math.round(rowItem.height * 0.16))
            radius: Tk.Theme.radius.sm / 2
            color: Tk.Theme.color.textMuted
            opacity: Tk.Theme.opacity.muted
        }
        Rectangle {
            objectName: "ganttBaseline"
            visible: rowItem.hasBaseline && rowItem.kind === "milestone"
            readonly property real side: Math.round(rowItem.diamond * 0.6)
            x: rowItem.view.xFor(rowItem.record.baselineStartUtc) - side / 2
            y: rowItem.height - side - Tk.Theme.space.xs
            width: side
            height: side
            rotation: 45
            color: "transparent"
            border.color: Tk.Theme.color.textMuted
            border.width: Tk.Theme.size.border
        }
        Rectangle {
            objectName: "ganttAllocation"
            visible: rowItem.view.allocationFor(rowItem.taskId) >= 0 && rowItem.kind === "task"
            x: rowItem.barX
            y: Tk.Theme.size.border
            width: rowItem.barWidth * rowItem.view.allocationFor(rowItem.taskId)
            height: Tk.Theme.size.border * 2
            color: Tk.Theme.color.warning
        }

        // Task bar: subtle fill, solid progress, accent (danger when critical).
        Rectangle {
            id: bar
            objectName: "ganttTaskBar"
            visible: rowItem.kind === "task"
            readonly property color tone: rowItem.inactive ? Tk.Theme.color.textMuted
                : rowItem.critical ? Tk.Theme.color.danger : Tk.Theme.color.accent
            x: rowItem.barX
            y: rowItem.barY
            width: rowItem.barWidth
            height: rowItem.barHeight
            radius: Tk.Theme.radius.sm
            color: rowItem.inactive ? "transparent"
                 : rowItem.critical ? Tk.Theme.color.dangerSubtle : Tk.Theme.color.accentSubtle
            opacity: rowItem.view.previewActive ? Tk.Theme.opacity.ghost : Tk.Theme.opacity.full
            border.color: bar.tone
            border.width: Tk.Theme.size.border
            Rectangle {
                objectName: "ganttProgress"
                visible: !rowItem.inactive
                width: parent.width * Math.max(0, Math.min(1, Number(rowItem.record.progress || 0)))
                height: parent.height
                radius: parent.radius
                color: bar.tone
            }
            DragHandler {
                id: mover
                target: null
                xAxis.enabled: true
                yAxis.enabled: false
                onTranslationChanged: {
                    const v = rowItem.view
                    if (!active || !v.calendar || !v.taskModel) return
                    const original = v.taskModel.get(rowItem.index)
                    if (v.calendarScale) {
                        const minutes = Math.round(translation.x / Math.max(0.0001, v.pixelsPerDay) * 1440)
                        v.previewMove(rowItem.taskId, v.calendar.snapForward(
                            String(Math.round(Number(original.startUtc) + minutes * 60000))))
                    } else {
                        const minutes = Math.round(translation.x / v.pixelsPerWorkingMinute)
                        v.previewMove(rowItem.taskId, v.calendar.addWorkingMinutes(original.startUtc, minutes))
                    }
                }
            }
        }

        // Summary: a slim bracket in the text role with pointed end caps.
        Item {
            id: summary
            objectName: "ganttSummaryBar"
            visible: rowItem.kind === "summary"
            readonly property real thickness: Math.max(Tk.Theme.size.border * 3, Math.round(rowItem.height * 0.2))
            readonly property color tone: rowItem.inactive ? Tk.Theme.color.textMuted : Tk.Theme.color.text
            x: rowItem.barX
            y: rowItem.barY
            width: rowItem.barWidth
            height: rowItem.barHeight
            Rectangle {
                width: parent.width
                height: summary.thickness
                color: summary.tone
            }
            Rectangle {
                objectName: "ganttSummaryProgress"
                width: parent.width * Math.max(0, Math.min(1, Number(rowItem.record.progress || 0)))
                height: Math.max(Tk.Theme.size.border, Math.round(summary.thickness / 3))
                y: summary.thickness - height
                color: Tk.Theme.color.accent
            }
            // End caps: a diamond centred on each end of the bar's TOP edge,
            // clipped there, so each reads as a downward point below the bar.
            Repeater {
                model: 2
                delegate: Item {
                    id: cap
                    required property int index
                    // Half the diamond's diagonal: the point hangs this far below the top.
                    readonly property real reach: summary.thickness + Math.max(Tk.Theme.space.xs * 2,
                                                                                Math.round(rowItem.barHeight * 0.3))
                    x: (index === 0 ? 0 : summary.width) - reach
                    width: reach * 2
                    height: reach
                    clip: true
                    Rectangle {
                        x: cap.reach - width / 2
                        y: -height / 2
                        width: cap.reach * Math.SQRT2
                        height: width
                        rotation: 45
                        color: summary.tone
                    }
                }
            }
        }

        // Milestone: a diamond centred on its instant.
        Rectangle {
            objectName: "ganttMilestone"
            visible: rowItem.kind === "milestone"
            x: rowItem.barX - rowItem.diamond / 2
            y: rowItem.barY + rowItem.barHeight / 2 - rowItem.diamond / 2
            width: rowItem.diamond
            height: rowItem.diamond
            rotation: 45
            scale: Math.SQRT1_2
            color: rowItem.inactive ? "transparent"
                 : rowItem.critical ? Tk.Theme.color.danger : Tk.Theme.color.text
            border.color: rowItem.inactive ? Tk.Theme.color.textMuted : "transparent"
            border.width: Tk.Theme.size.border
        }

        Tk.Caption {
            objectName: "ganttBarText"
            visible: text.length > 0
            x: rowItem.barEnd + Tk.Theme.space.sm
            width: Math.max(0, track.width - x)
            height: rowItem.hasBaseline ? rowItem.barHeight + rowItem.barY * 2 : rowItem.height
            text: String(rowItem.record.barText || "")
        }
    }
}
