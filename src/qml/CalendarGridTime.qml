// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "CalendarGridLayout.js" as CalendarLayout

// Day / work week / week body of CalendarGridView: day headers, the all-day
// lane and the scrolling 0–24 h time grid. Internal part; the public API is
// CalendarGridView. Columns are positioned by arithmetic (not a Flex) so the
// header, the lane and the grid share exactly one column width.
Item {
    id: view

    required property Tk.CalendarGridView grid
    readonly property string areaName: "timed"
    readonly property var days: grid.days
    readonly property int dayCount: days.length
    readonly property real hourHeight: grid.hourHeight
    readonly property real canvasHeight: hourHeight * 24
    readonly property real gutterWidth: Math.ceil(gutterMetrics.advanceWidth) + Tk.Theme.space.md * 2
    readonly property real columnWidth: dayCount > 0 ? Math.max(0, (width - gutterWidth) / dayCount) : 0
    readonly property real minBlockHeight: Tk.Theme.font.caption + Tk.Theme.space.sm * 2
    readonly property int minVisualMinutes: Math.ceil(minBlockHeight / Math.max(1, hourHeight) * 60)
    readonly property var pieces: CalendarLayout.timedLayout(grid.entries, days, minVisualMinutes)
    readonly property var lane: CalendarLayout.allDayLayout(grid.entries, days)
    readonly property int todayIndex: days.indexOf(grid.todayDate)
    // Blocks leave a strip on the right of each day for creating by pointer.
    readonly property real blockGap: columnWidth > Tk.Theme.size.fieldWidth ? Tk.Theme.space.md : Tk.Theme.space.xs
    readonly property var ghost: {
        const d = grid.dragState
        if (!d || d.area !== "timed") return null
        const day = Math.floor(d.start / 1440)
        const index = days.indexOf(CalendarLayout.isoFromDayNumber(day))
        if (index < 0) return null
        return { dayIndex: index, start: d.start - day * 1440, end: Math.min(d.end - day * 1440, 1440),
                 label: grid.rangeText(d.start, d.end),
                 title: d.entry ? String(d.entry.row.title || "") : qsTr("New event"),
                 tone: d.entry ? grid.colorFor(d.entry.row) : Tk.Theme.color.accent, pending: d.pending }
    }
    property point _pointer: Qt.point(0, 0)
    property int _autoScroll: 0
    property bool _initialScrollDone: false

    function yFor(minute) { return minute / 60 * hourHeight }
    function slotFromView(px, py) {
        const p = canvasDays.mapFromItem(view, px, py)
        return { dayIndex: Math.max(0, Math.min(dayCount - 1, Math.floor(p.x / Math.max(1, columnWidth)))),
                 minute: Math.max(0, Math.min(1440, p.y / Math.max(1, hourHeight) * 60)) }
    }
    function slotFrom(item, x, y) { const p = item.mapToItem(view, x, y); return slotFromView(p.x, p.y) }
    // Pointer tracking in this item's coordinates, so auto-scroll can keep
    // re-resolving the slot while the canvas moves under a still pointer.
    function dragTo(item, x, y) {
        if (!item) { _autoScroll = 0; return }
        _pointer = item.mapToItem(view, x, y)
        const top = scroll.mapToItem(view, 0, 0).y, edge = Tk.Theme.size.row
        _autoScroll = _pointer.y < top + edge ? -1 : _pointer.y > top + scroll.height - edge ? 1 : 0
        grid.updateDrag(slotFromView(_pointer.x, _pointer.y))
    }
    function scrollToHour(hour) {
        const target = Math.max(0, Math.min(scroll.contentHeight - scroll.viewportHeight, hour * hourHeight - Tk.Theme.space.md))
        scroll.contentY = Math.max(0, target)
        return true
    }
    // The grid opens at the working band; later re-projections keep the
    // user's scroll position.
    function _initialScroll() {
        if (_initialScrollDone || scroll.viewportHeight <= 0 || scroll.contentHeight <= scroll.viewportHeight) return
        _initialScrollDone = true
        scrollToHour(grid.dayStartHour)
    }
    function geometry(piece) {
        const usable = Math.max(0, columnWidth - blockGap - Tk.Theme.size.border)
        const slot = usable / Math.max(1, piece.columns)
        const top = Math.min(yFor(piece.start), canvasHeight - minBlockHeight)
        return { x: piece.dayIndex * columnWidth + Tk.Theme.size.border + piece.column * slot,
                 y: top + Tk.Theme.size.border,
                 width: Math.max(0, slot * piece.span - Tk.Theme.size.border),
                 height: Math.max(minBlockHeight, yFor(piece.end) - top) - Tk.Theme.size.border }
    }

    TextMetrics {
        id: gutterMetrics
        font.family: Tk.Theme.font.family
        font.pixelSize: Tk.Theme.font.caption
        text: [view.grid.timeText(10 * 60, true), view.grid.timeText(23 * 60, true), view.grid.timeText(12 * 60, true), qsTr("all-day")]
              .reduce(function(a, b) { return b.length > a.length ? b : a }, "")
    }
    Timer {
        interval: Math.round(Tk.Theme.motion.gestureIdle / 6) // reduced motion keeps gestureIdle
        repeat: true
        running: view._autoScroll !== 0 && !!view.grid.dragState && !view.grid.dragState.pending
        onTriggered: {
            scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.viewportHeight,
                                                   scroll.contentY + view._autoScroll * view.hourHeight / 4))
            view.grid.updateDrag(view.slotFromView(view._pointer.x, view._pointer.y))
        }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        Item {
            id: header
            objectName: "calendarGridHeader"
            implicitHeight: Tk.Theme.font.caption + Tk.Theme.size.controlLg + Tk.Theme.space.sm * 2
            Tk.Flex.shrink: 0
            Tk.Caption {
                x: Tk.Theme.space.xs
                width: view.gutterWidth - Tk.Theme.space.sm
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Tk.Theme.space.xs
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                text: view.grid.zone
            }
            Repeater {
                model: view.days
                delegate: Item {
                    id: dayHeader
                    required property string modelData
                    required property int index
                    readonly property bool today: modelData === view.grid.todayDate
                    x: view.gutterWidth + index * view.columnWidth
                    width: view.columnWidth
                    height: header.height
                    Accessible.role: Accessible.ColumnHeader
                    Accessible.name: view.grid.dayLabel(modelData) + (today ? qsTr(", today") : "")
                    Tk.Flex {
                        anchors.centerIn: parent
                        direction: Tk.Flex.Column
                        align: Tk.Flex.Center
                        Tk.Caption {
                            text: view.grid.weekdayName(dayHeader.modelData, Locale.ShortFormat).toUpperCase()
                            color: dayHeader.today ? Tk.Theme.color.accentText : Tk.Theme.color.textMuted
                            font.letterSpacing: Tk.Theme.font.trackingWide
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            implicitWidth: Math.max(Tk.Theme.size.controlLg, dateNumber.implicitWidth + Tk.Theme.space.md)
                            implicitHeight: Tk.Theme.size.controlLg
                            radius: Tk.Theme.radius.full
                            color: dayHeader.today ? Tk.Theme.color.accent : "transparent"
                            Tk.Label {
                                id: dateNumber
                                anchors.centerIn: parent
                                text: String(Number(dayHeader.modelData.slice(8, 10)))
                                font.pixelSize: Tk.Theme.font.title
                                font.weight: dayHeader.today ? Font.DemiBold : Font.Normal
                                color: dayHeader.today ? Tk.Theme.color.accentContrast : Tk.Theme.color.text
                            }
                        }
                    }
                }
            }
        }
        Tk.CalendarGridAllDay {
            view: view
            grid: view.grid
            Tk.Flex.shrink: 0
        }
        Rectangle { implicitHeight: Tk.Theme.size.border; color: Tk.Theme.color.border; Tk.Flex.shrink: 0 }
        Tk.Scroll {
            id: scroll
            objectName: "calendarGridScroll"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0
            accessibleName: qsTr("Time grid")
            onContentHeightChanged: view._initialScroll()
            onViewportHeightChanged: view._initialScroll()
            Item {
                id: canvas
                implicitHeight: view.canvasHeight
                Item {
                    objectName: "calendarGridGutter"
                    width: view.gutterWidth
                    height: view.canvasHeight
                    Repeater {
                        model: 23
                        delegate: Tk.Caption {
                            required property int index
                            x: Tk.Theme.space.xs
                            width: view.gutterWidth - Tk.Theme.space.md
                            y: view.yFor((index + 1) * 60) - implicitHeight / 2
                            horizontalAlignment: Text.AlignRight
                            text: view.grid.timeText((index + 1) * 60, true)
                        }
                    }
                }
                Item {
                    id: canvasDays
                    x: view.gutterWidth
                    width: Math.max(0, canvas.width - view.gutterWidth)
                    height: view.canvasHeight
                    Repeater {
                        model: view.days
                        delegate: Item {
                            id: dayColumn
                            required property string modelData
                            required property int index
                            readonly property bool workDay: (view.grid.workDays || []).indexOf(CalendarLayout.isoWeekday(modelData)) >= 0
                            objectName: "calendarGridDay_" + modelData
                            x: index * view.columnWidth
                            width: view.columnWidth
                            height: view.canvasHeight
                            Accessible.role: Accessible.Column
                            Accessible.name: view.grid.dayLabel(modelData)
                            Rectangle {
                                width: parent.width
                                height: dayColumn.workDay ? view.yFor(view.grid.dayStartHour * 60) : parent.height
                                color: Tk.Theme.color.panelAlt
                            }
                            Rectangle {
                                visible: dayColumn.workDay
                                y: view.yFor(view.grid.dayEndHour * 60)
                                width: parent.width
                                height: Math.max(0, parent.height - y)
                                color: Tk.Theme.color.panelAlt
                            }
                            Rectangle {
                                anchors.fill: parent
                                visible: dayColumn.modelData === view.grid.todayDate
                                color: Tk.Theme.alpha(Tk.Theme.color.accent, Tk.Theme.opacity.wash)
                            }
                            Rectangle { width: Tk.Theme.size.border; height: parent.height; color: Tk.Theme.color.divider }
                        }
                    }
                    Repeater {
                        model: 48
                        delegate: Rectangle {
                            required property int index
                            visible: index > 0
                            y: index * view.hourHeight / 2
                            width: canvasDays.width
                            height: Tk.Theme.size.border
                            color: index % 2 === 0 ? Tk.Theme.color.divider : Tk.Theme.color.seam
                        }
                    }
                    MouseArea {
                        // Empty slots: double-click creates a `createMinutes`
                        // event, a drag creates the swept range.
                        id: creator
                        anchors.fill: parent
                        preventStealing: true
                        property point pressAt
                        property bool started: false
                        onPressed: function(mouse) {
                            view.grid.forceActiveFocus(Qt.MouseFocusReason)
                            view.grid.selectedOccurrenceId = ""
                            pressAt = Qt.point(mouse.x, mouse.y)
                            started = false
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed) return
                            if (!started) {
                                if (Math.abs(mouse.y - pressAt.y) < view.grid.dragDistance) return
                                started = view.grid.beginDrag("create", "timed", null, view.slotFrom(creator, pressAt.x, pressAt.y))
                                if (!started) return
                            }
                            view.dragTo(creator, mouse.x, mouse.y)
                        }
                        onReleased: { if (started) view.grid.endDrag(); started = false; view.dragTo(null, 0, 0) }
                        onCanceled: { if (started) view.grid.cancelDrag(); started = false; view.dragTo(null, 0, 0) }
                        onDoubleClicked: function(mouse) {
                            const slot = view.slotFrom(creator, mouse.x, mouse.y)
                            view.grid.createAt(slot.dayIndex, slot.minute)
                        }
                    }
                    Repeater {
                        model: view.pieces
                        delegate: Tk.CalendarGridEvent {
                            required property var modelData
                            readonly property var box: view.geometry(modelData)
                            grid: view.grid
                            piece: modelData
                            area: view
                            variant: "timed"
                            x: box.x
                            y: box.y
                            width: box.width
                            height: box.height
                        }
                    }
                    Item {
                        objectName: "calendarGridNowLine"
                        visible: view.todayIndex >= 0 && !!view.grid.nowPoint
                        x: view.todayIndex * view.columnWidth
                        y: view.grid.nowPoint ? view.yFor(view.grid.nowPoint.minute) - height / 2 : 0
                        width: view.columnWidth
                        height: Tk.Theme.size.focusRing
                        z: 2
                        Rectangle { anchors.fill: parent; color: Tk.Theme.color.danger }
                        Rectangle {
                            width: Tk.Theme.space.md + Tk.Theme.size.focusRing
                            height: width
                            radius: Tk.Theme.radius.full
                            x: -width / 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: Tk.Theme.color.danger
                        }
                    }
                    Loader {
                        active: !!view.ghost
                        z: 3
                        sourceComponent: Rectangle {
                            id: ghostBox
                            objectName: "calendarGridGhost"
                            readonly property var g: view.ghost
                            x: g.dayIndex * view.columnWidth + Tk.Theme.size.border
                            y: view.yFor(g.start) + Tk.Theme.size.border
                            width: Math.max(0, view.columnWidth - view.blockGap - Tk.Theme.size.border)
                            height: Math.max(view.minBlockHeight, view.yFor(g.end) - view.yFor(g.start)) - Tk.Theme.size.border
                            radius: Tk.Theme.radius.md
                            opacity: g.pending ? Tk.Theme.opacity.muted : Tk.Theme.opacity.full
                            color: Tk.Theme.mix(Tk.Theme.color.panel, g.tone, Tk.Theme.opacity.tintStrong)
                            border.width: Tk.Theme.size.focusRing
                            border.color: g.tone
                            Accessible.role: Accessible.Indicator
                            Accessible.name: g.title + ", " + g.label
                            Tk.Flex {
                                anchors.fill: parent
                                anchors.margins: Tk.Theme.space.xs
                                anchors.leftMargin: Tk.Theme.space.sm
                                direction: Tk.Flex.Column
                                Tk.Label { text: ghostBox.g.label; font.weight: Font.DemiBold; font.pixelSize: Tk.Theme.font.caption }
                                Tk.Caption { text: ghostBox.g.title; visible: ghostBox.height > Tk.Theme.size.row * 2 }
                            }
                        }
                    }
                }
            }
        }
    }
}
