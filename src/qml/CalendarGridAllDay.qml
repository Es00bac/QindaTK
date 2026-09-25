// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "CalendarGridLayout.js" as CalendarLayout

// The all-day lane above the time grid of CalendarGridView: all-day rows
// and timed rows of 24 hours or more as bars across their visible days,
// first-fit into lanes. Beyond `maxLanes` the last lane becomes a
// "+k more" row (moreRequested). Internal part of CalendarGridView.
Item {
    id: lane

    required property var view // CalendarGridTime
    required property Tk.CalendarGridView grid
    readonly property string areaName: "allday"
    property int maxLanes: 3
    readonly property var layout: view.lane
    readonly property bool overflow: layout.laneCount > maxLanes
    readonly property int shownLanes: overflow ? maxLanes - 1 : layout.laneCount
    readonly property real barHeight: Tk.Theme.size.controlSm
    readonly property real gap: Tk.Theme.space.xs
    readonly property real columnWidth: view.columnWidth
    readonly property var hiddenCounts: {
        const counts = view.days.map(function() { return 0 })
        if (!overflow) return counts
        layout.bars.forEach(function(bar) {
            if (bar.lane < lane.shownLanes) return
            for (let i = bar.col; i < bar.col + bar.span; ++i) counts[i] += 1
        })
        return counts
    }
    readonly property var ghost: {
        const d = grid.dragState
        if (!d || d.area !== "allday") return null
        const first = view.days.indexOf(CalendarLayout.isoFromDayNumber(Math.floor(d.start / 1440)))
        const last = view.days.indexOf(CalendarLayout.isoFromDayNumber(Math.floor((d.end - 1) / 1440)))
        const col = first >= 0 ? first : 0, end = last >= 0 ? last : view.days.length - 1
        return { col: col, span: Math.max(1, end - col + 1), pending: d.pending,
                 title: d.entry ? String(d.entry.row.title || "") : qsTr("New all-day event"),
                 tone: d.entry ? grid.colorFor(d.entry.row) : Tk.Theme.color.accent }
    }

    objectName: "calendarGridAllDay"
    implicitHeight: Math.max(1, shownLanes + (overflow ? 1 : 0)) * (barHeight + gap) + gap
    Accessible.role: Accessible.Row
    Accessible.name: qsTr("All-day events")

    function slotFrom(item, x, y) {
        const p = item.mapToItem(lane, x, y)
        return { dayIndex: Math.max(0, Math.min(view.days.length - 1, Math.floor((p.x - view.gutterWidth) / Math.max(1, columnWidth)))), minute: 0 }
    }
    function dragTo(item, x, y) { if (item) grid.updateDrag(slotFrom(item, x, y)) }
    function laneY(index) { return gap + index * (barHeight + gap) }

    Tk.Caption {
        x: Tk.Theme.space.xs
        width: lane.view.gutterWidth - Tk.Theme.space.sm
        y: lane.laneY(0) + (lane.barHeight - implicitHeight) / 2
        horizontalAlignment: Text.AlignRight
        text: qsTr("all-day")
    }
    Repeater {
        model: lane.view.days
        delegate: Item {
            required property string modelData
            required property int index
            x: lane.view.gutterWidth + index * lane.columnWidth
            width: lane.columnWidth
            height: lane.height
            Rectangle {
                anchors.fill: parent
                visible: parent.modelData === lane.grid.todayDate
                color: Tk.Theme.alpha(Tk.Theme.color.accent, Tk.Theme.opacity.wash)
            }
            Rectangle { width: Tk.Theme.size.border; height: parent.height; color: Tk.Theme.color.divider }
        }
    }
    MouseArea {
        id: creator
        x: lane.view.gutterWidth
        width: Math.max(0, lane.width - x)
        height: lane.height
        preventStealing: true
        property point pressAt
        property bool started: false
        onPressed: function(mouse) {
            lane.grid.forceActiveFocus(Qt.MouseFocusReason)
            pressAt = Qt.point(mouse.x, mouse.y)
            started = false
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            if (!started) {
                if (Math.abs(mouse.x - pressAt.x) < lane.grid.dragDistance) return
                started = lane.grid.beginDrag("create", "allday", null, lane.slotFrom(creator, pressAt.x, pressAt.y))
                if (!started) return
            }
            lane.dragTo(creator, mouse.x, mouse.y)
        }
        onReleased: { if (started) lane.grid.endDrag(); started = false }
        onCanceled: { if (started) lane.grid.cancelDrag(); started = false }
        onDoubleClicked: function(mouse) { lane.grid.createDay(lane.slotFrom(creator, mouse.x, mouse.y).dayIndex) }
    }
    Repeater {
        model: lane.layout.bars.filter(function(bar) { return bar.lane < lane.shownLanes })
        delegate: Tk.CalendarGridEvent {
            required property var modelData
            grid: lane.grid
            piece: modelData
            area: lane
            variant: "bar"
            x: lane.view.gutterWidth + modelData.col * lane.columnWidth + Tk.Theme.size.border + Tk.Theme.space.xs
            y: lane.laneY(modelData.lane)
            width: Math.max(0, modelData.span * lane.columnWidth - Tk.Theme.size.border - Tk.Theme.space.xs * 2)
            height: lane.barHeight
        }
    }
    Repeater {
        model: lane.overflow ? lane.view.days : []
        delegate: Tk.CalendarGridMore {
            required property string modelData
            required property int index
            objectName: "calendarGridMore_" + modelData
            visible: lane.hiddenCounts[index] > 0
            count: lane.hiddenCounts[index]
            x: lane.view.gutterWidth + index * lane.columnWidth + Tk.Theme.space.xs
            y: lane.laneY(lane.shownLanes)
            width: Math.max(0, lane.columnWidth - Tk.Theme.space.sm)
            height: lane.barHeight
            onClicked: lane.grid.moreRequested(modelData)
        }
    }
    Loader {
        active: !!lane.ghost
        z: 2
        sourceComponent: Rectangle {
            id: ghostBar
            objectName: "calendarGridGhost"
            readonly property var g: lane.ghost
            x: lane.view.gutterWidth + g.col * lane.columnWidth + Tk.Theme.size.border + Tk.Theme.space.xs
            y: lane.laneY(0)
            width: Math.max(0, g.span * lane.columnWidth - Tk.Theme.size.border - Tk.Theme.space.xs * 2)
            height: lane.barHeight
            radius: Tk.Theme.radius.sm
            opacity: g.pending ? Tk.Theme.opacity.muted : Tk.Theme.opacity.full
            color: Tk.Theme.mix(Tk.Theme.color.panel, g.tone, Tk.Theme.opacity.tintStrong)
            border.width: Tk.Theme.size.focusRing
            border.color: g.tone
            Accessible.role: Accessible.Indicator
            Accessible.name: g.title
            Tk.Label {
                anchors.fill: parent
                anchors.leftMargin: Tk.Theme.space.sm
                text: ghostBar.g.title
                font.weight: Font.DemiBold
            }
        }
    }
}
