// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// Internal part of GanttView (D-303): one recycled task row — the label
// column, the bar (task, split pieces, summary bracket, milestone diamond or
// group span), progress and the bar text. Baseline lanes, slack, deadline
// and allocation are GanttRowLanes; stripes are the backdrop's; a row
// paints only its selection wash (and a group header its band, D-312).
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
    readonly property bool isGroup: kind === "group"
    readonly property bool collapsed: record.collapsed === true
    readonly property bool critical: record.critical === true
    readonly property bool inactive: record.inactive === true
    readonly property bool selected: view.selectionModel !== null && view.selectionModel.currentId === taskId
    readonly property bool hasBaseline: !!record.baselineStartUtc
    readonly property bool hasBaseline2: !!record.baseline2StartUtc
    // AGENT-GUARD: a QVariantList from the model is a sequence, not a JS
    // Array (Array.isArray is false); test its length.
    readonly property var segments: kind === "task" && record.segments && record.segments.length > 0
                                    ? record.segments : []
    readonly property string endUtc: { void view.calendarRevision; return view.endFor(record) }
    readonly property real barX: { void view.calendarRevision; return view.xFor(record.startUtc) }
    readonly property real barWidth: Math.max(Tk.Theme.size.border, view.xFor(endUtc) - barX)
    // Lanes (D-313): the bar sits high when baseline lanes share the row,
    // centred otherwise; the one-lane geometry is the D-303 one.
    readonly property int laneCount: (hasBaseline ? 1 : 0) + (hasBaseline2 ? 1 : 0)
    readonly property real barHeight: Math.round(height * (laneCount === 2 ? 0.34 : laneCount === 1 ? 0.42 : 0.5))
    readonly property real barY: laneCount === 2 ? Math.round(height * 0.1)
        : laneCount === 1 ? Math.round(height * 0.14) : Math.round((height - barHeight) / 2)
    readonly property real laneHeight: Math.max(Tk.Theme.size.border * 2, Math.round(height * (laneCount === 2 ? 0.14 : 0.16)))
    readonly property real diamond: Math.round(height * (laneCount > 0 ? 0.5 : 0.58))
    readonly property real barEnd: kind === "milestone" ? barX + diamond / 2 : barX + barWidth
    // Track x of the progress point: `progressUtc` when the host gives it,
    // else `progress` along the bar (along the pieces of a split bar).
    readonly property real progressX: {
        void view.calendarRevision
        const fraction = Math.max(0, Math.min(1, Number(record.progress || 0)))
        if (record.progressUtc) return view.xFor(record.progressUtc)
        if (segments.length === 0) return barX + barWidth * fraction
        let total = 0
        for (let i = 0; i < segments.length; ++i)
            total += Math.max(0, view.xFor(segments[i].finishUtc) - view.xFor(segments[i].startUtc))
        let left = total * fraction
        for (let j = 0; j < segments.length; ++j) {
            const from = view.xFor(segments[j].startUtc)
            const span = Math.max(0, view.xFor(segments[j].finishUtc) - from)
            if (left <= span) return from + left
            left -= span
        }
        return barX + barWidth
    }
    function laneY(lane) { return barY + barHeight + Tk.Theme.space.xs + lane * (laneHeight + Tk.Theme.space.xs) }

    objectName: "ganttRow"
    height: view.rowHeight
    Accessible.role: Accessible.ListItem
    Accessible.name: (record.label || taskId) + ", "
        + (isGroup ? String(record.summaryText || "")
                   : view.calendar && record.startUtc ? view.calendar.formatLocal(record.startUtc) : "")
    Accessible.description: [
        isGroup ? (collapsed ? qsTr("group, collapsed") : qsTr("group, expanded"))
            : kind === "summary" ? qsTr("summary") : kind === "milestone" ? qsTr("milestone") : "",
        critical ? qsTr("critical") : "", inactive ? qsTr("inactive") : "",
        segments.length > 1 ? qsTr("split into %1 parts").arg(segments.length) : "",
        isGroup ? "" : qsTr("%1 percent complete; %2 dependencies")
            .arg(Math.round(Number(record.progress || 0) * 100))
            .arg(record.dependencies ? record.dependencies.length : 0)
    ].filter(function(part) { return part.length > 0 }).join("; ")
    Accessible.selected: selected

    Connections {
        target: rowItem.view.taskModel
        function onPreviewChanged() { rowItem.previewRevision++ }
    }

    // A group header (D-312) is a band across the row, under the selection.
    Rectangle {
        objectName: "ganttGroupHeader"
        visible: rowItem.isGroup
        anchors.fill: parent
        color: Tk.Theme.color.headerBg
        Rectangle {
            y: parent.height - height
            width: parent.width
            height: Tk.Theme.size.border
            color: Tk.Theme.color.divider
        }
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
        // A group header toggles from its chevron or the keyboard only, so a
        // double-click on the chevron does not toggle it twice.
        onDoubleTapped: if (!rowItem.isGroup) rowItem.view.taskActivated(rowItem.taskId)
    }

    Tk.Label {
        objectName: "ganttTaskLabel"
        visible: rowItem.view.labelWidth > 0 && !rowItem.isGroup
        x: Tk.Theme.space.sm + Tk.Theme.space.lg * Math.max(0, Number(rowItem.record.outlineLevel || 0))
        width: Math.max(0, rowItem.view.labelWidth - x - Tk.Theme.space.sm)
        height: parent.height
        text: rowItem.record.label || rowItem.taskId
        font.bold: rowItem.kind === "summary"
        color: rowItem.selected ? Tk.Theme.color.accentText
             : rowItem.inactive ? Tk.Theme.color.textMuted : Tk.Theme.color.text
    }
    // Group header label column: chevron (toggles), name, summary.
    Item {
        id: groupTitle
        objectName: "ganttGroupTitle"
        visible: rowItem.view.labelWidth > 0 && rowItem.isGroup
        x: Tk.Theme.space.xs + Tk.Theme.space.lg * Math.max(0, Number(rowItem.record.outlineLevel || 0))
        width: Math.max(0, rowItem.view.labelWidth - x - Tk.Theme.space.sm)
        height: parent.height
        clip: true
        Tk.Icon {
            id: chevron
            objectName: "ganttGroupChevron"
            // AGENT-GUARD (D-339): a sheet may own the label column. The
            // same persistent chevron then lives in the chart, never under
            // the hidden title item, so chart-only groups remain collapsible.
            parent: rowItem.view.labelWidth > 0 ? groupTitle : track
            visible: rowItem.isGroup
            x: rowItem.view.labelWidth > 0 ? 0 : Tk.Theme.space.xs
            z: rowItem.view.labelWidth > 0 ? 0 : 2
            Accessible.role: Accessible.Button
            Accessible.name: rowItem.collapsed ? qsTr("Expand group") : qsTr("Collapse group")
            Accessible.onPressAction: rowItem.view.toggleGroup(rowItem.taskId)
            name: rowItem.collapsed ? "chevron-right" : "chevron-down"
            size: Tk.Theme.size.iconSm
            y: (parent.height - height) / 2
            color: Tk.Theme.color.textMuted
            Rectangle {
                objectName: "ganttGroupChevronBacking"
                anchors.fill: parent
                anchors.margins: -Tk.Theme.space.xs
                z: -1
                visible: rowItem.view.labelWidth <= 0
                color: Tk.Theme.color.headerBg
            }
            TapHandler {
                margin: Tk.Theme.space.xs
                onTapped: rowItem.view.toggleGroup(rowItem.taskId)
            }
        }
        Tk.Label {
            id: groupLabel
            objectName: "ganttGroupLabel"
            x: chevron.width + Tk.Theme.space.xs
            width: Math.min(implicitWidth, parent.width - x)
            height: parent.height
            text: rowItem.record.label || rowItem.taskId
            font.bold: true
            color: rowItem.selected ? Tk.Theme.color.accentText : Tk.Theme.color.text
        }
        Tk.Caption {
            objectName: "ganttGroupSummary"
            visible: text.length > 0
            x: groupLabel.x + groupLabel.width + Tk.Theme.space.sm
            width: Math.max(0, parent.width - x)
            height: parent.height
            text: String(rowItem.record.summaryText || "")
            elide: Text.ElideRight
        }
    }

    Item {
        id: track
        objectName: "ganttTrack"
        x: rowItem.view.labelWidth
        width: Math.max(0, parent.width - x)
        height: parent.height
        clip: true

        // Above the bar: a deadline marker can fall on it. The lanes take no input.
        Tk.GanttRowLanes {
            row: rowItem
            anchors.fill: parent
            z: 1
        }

        // Task bar: subtle fill, solid progress, accent (danger when critical).
        // A split bar (`segments`) keeps this item as the drag surface and
        // draws its pieces joined by a thin line instead of one fill.
        Rectangle {
            id: bar
            objectName: "ganttTaskBar"
            visible: rowItem.kind === "task"
            readonly property bool split: rowItem.segments.length > 0
            readonly property color tone: rowItem.inactive ? Tk.Theme.color.textMuted
                : rowItem.critical ? Tk.Theme.color.danger : Tk.Theme.color.accent
            readonly property color fill: rowItem.inactive ? "transparent"
                 : rowItem.critical ? Tk.Theme.color.dangerSubtle : Tk.Theme.color.accentSubtle
            x: rowItem.barX
            y: rowItem.barY
            width: rowItem.barWidth
            height: rowItem.barHeight
            radius: Tk.Theme.radius.sm
            color: split ? "transparent" : fill
            opacity: rowItem.view.previewActive ? Tk.Theme.opacity.ghost : Tk.Theme.opacity.full
            border.color: split ? "transparent" : bar.tone
            border.width: Tk.Theme.size.border
            Rectangle {
                objectName: "ganttProgress"
                visible: !rowItem.inactive && !bar.split
                width: Math.max(0, Math.min(parent.width, rowItem.progressX - rowItem.barX))
                height: parent.height
                radius: parent.radius
                color: bar.tone
            }
            Repeater {
                model: bar.split ? rowItem.segments.length : 0
                delegate: Item {
                    id: piece
                    required property int index
                    readonly property var span: rowItem.segments[index] || ({})
                    readonly property real from: rowItem.view.xFor(span.startUtc) - rowItem.barX
                    readonly property real to: rowItem.view.xFor(span.finishUtc) - rowItem.barX
                    readonly property real previousTo: index > 0
                        ? rowItem.view.xFor(rowItem.segments[index - 1].finishUtc) - rowItem.barX : from
                    Rectangle {
                        objectName: "ganttSplitGap"
                        visible: piece.index > 0
                        x: piece.previousTo
                        y: bar.height / 2 - height / 2
                        width: Math.max(0, piece.from - piece.previousTo)
                        height: Tk.Theme.size.border
                        color: bar.tone
                        opacity: Tk.Theme.opacity.muted
                    }
                    Rectangle {
                        objectName: "ganttBarSegment"
                        x: piece.from
                        width: Math.max(Tk.Theme.size.border, piece.to - piece.from)
                        height: bar.height
                        radius: Tk.Theme.radius.sm
                        color: bar.fill
                        border.color: bar.tone
                        border.width: Tk.Theme.size.border
                        Rectangle {
                            objectName: "ganttSegmentProgress"
                            visible: !rowItem.inactive
                            width: Math.max(0, Math.min(parent.width, rowItem.progressX - rowItem.barX - piece.from))
                            height: parent.height
                            radius: parent.radius
                            color: bar.tone
                        }
                    }
                }
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

        // Group span (D-312): a flat band from the group's start to finish.
        Rectangle {
            objectName: "ganttGroupBar"
            visible: rowItem.isGroup && rowItem.endUtc.length > 0 && rowItem.barWidth > Tk.Theme.size.border
            x: rowItem.barX
            y: Math.round((rowItem.height - height) / 2)
            width: rowItem.barWidth
            height: Math.max(Tk.Theme.size.border * 3, Math.round(rowItem.height * 0.24))
            radius: Tk.Theme.radius.xs
            color: Tk.Theme.color.borderStrong
            Rectangle {
                objectName: "ganttGroupProgress"
                width: parent.width * Math.max(0, Math.min(1, Number(rowItem.record.progress || 0)))
                height: parent.height
                radius: parent.radius
                color: Tk.Theme.color.textMuted
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

        // Bar text; a group row beside a table (no label column) shows its
        // name and summary here instead.
        Tk.Caption {
            objectName: "ganttBarText"
            visible: text.length > 0
            x: rowItem.isGroup && rowItem.view.labelWidth === 0
                ? Math.max(rowItem.barEnd + Tk.Theme.space.sm, chevron.width + Tk.Theme.space.sm * 2)
                : rowItem.barEnd + Tk.Theme.space.sm
            width: Math.max(0, track.width - x)
            height: rowItem.laneCount > 0 ? rowItem.barHeight + rowItem.barY * 2 : rowItem.height
            text: rowItem.isGroup
                  ? (rowItem.view.labelWidth > 0 ? ""
                     : [String(rowItem.record.label || ""), String(rowItem.record.summaryText || "")]
                         .filter(function(part) { return part.length > 0 }).join(" · "))
                  : String(rowItem.record.barText || "")
        }
    }
}
