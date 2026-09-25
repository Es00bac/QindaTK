// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "CalendarGridLayout.js" as CalendarLayout

// Month body of CalendarGridView: a weekday header over the 6 × 7 grid that
// `CalendarLayout.range("month")` returns (days of the neighbouring months
// muted). Each cell lists as many chips as fit, all-day first, then a
// "+k more" link (moreRequested). Chips drag in whole days; an empty cell
// double-clicks or drags into a whole-day create. Internal part.
Item {
    id: month

    required property Tk.CalendarGridView grid
    readonly property string areaName: "month"
    readonly property var days: grid.days
    readonly property var cells: CalendarLayout.monthLayout(grid.entries, days)
    readonly property string shownMonth: grid._anchor.slice(0, 7)
    readonly property real headerHeight: Tk.Theme.size.header
    readonly property real cellWidth: width / 7
    readonly property real cellHeight: Math.max(0, (height - headerHeight) / 6)
    readonly property real chipHeight: Tk.Theme.size.controlSm
    readonly property real chipGap: Tk.Theme.size.border
    readonly property real dateHeight: Tk.Theme.size.controlSm + Tk.Theme.space.xs * 2
    readonly property int capacity: Math.max(0, Math.floor((cellHeight - dateHeight - Tk.Theme.space.xs) / (chipHeight + chipGap)))
    readonly property var ghost: {
        const d = grid.dragState
        if (!d || d.area !== "month") return null
        const first = Math.floor(d.start / 1440), last = Math.floor((d.end - 1) / 1440)
        return { first: first, last: last, index: days.indexOf(CalendarLayout.isoFromDayNumber(first)), pending: d.pending,
                 title: d.entry ? String(d.entry.row.title || "") : qsTr("New all-day event"),
                 tone: d.entry ? grid.colorFor(d.entry.row) : Tk.Theme.color.accent }
    }

    function slotFrom(item, x, y) {
        const p = item.mapToItem(body, x, y)
        const column = Math.max(0, Math.min(6, Math.floor(p.x / Math.max(1, cellWidth))))
        const rowIndex = Math.max(0, Math.min(5, Math.floor(p.y / Math.max(1, cellHeight))))
        return { dayIndex: rowIndex * 7 + column, minute: 0 }
    }
    function dragTo(item, x, y) { if (item) grid.updateDrag(slotFrom(item, x, y)) }

    Item {
        objectName: "calendarGridHeader"
        width: month.width
        height: month.headerHeight
        Repeater {
            model: month.days.slice(0, 7)
            delegate: Tk.Caption {
                required property string modelData
                required property int index
                x: index * month.cellWidth
                width: month.cellWidth
                height: month.headerHeight
                horizontalAlignment: Text.AlignHCenter
                text: month.grid.weekdayName(modelData, Locale.ShortFormat).toUpperCase()
                font.letterSpacing: Tk.Theme.font.trackingWide
                font.weight: Font.DemiBold
                Accessible.role: Accessible.ColumnHeader
                Accessible.name: month.grid.weekdayName(modelData, Locale.LongFormat)
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Tk.Theme.size.border; color: Tk.Theme.color.border }
    }
    Item {
        id: body
        y: month.headerHeight
        width: month.width
        height: month.cellHeight * 6
        MouseArea {
            id: creator
            anchors.fill: parent
            preventStealing: true
            property point pressAt
            property bool started: false
            onPressed: function(mouse) {
                month.grid.forceActiveFocus(Qt.MouseFocusReason)
                month.grid.selectedOccurrenceId = ""
                pressAt = Qt.point(mouse.x, mouse.y)
                started = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                if (!started) {
                    if (Math.abs(mouse.x - pressAt.x) < month.grid.dragDistance
                            && Math.abs(mouse.y - pressAt.y) < month.grid.dragDistance) return
                    started = month.grid.beginDrag("create", "month", null, month.slotFrom(creator, pressAt.x, pressAt.y))
                    if (!started) return
                }
                month.dragTo(creator, mouse.x, mouse.y)
            }
            onReleased: { if (started) month.grid.endDrag(); started = false }
            onCanceled: { if (started) month.grid.cancelDrag(); started = false }
            onDoubleClicked: function(mouse) { month.grid.createDay(month.slotFrom(creator, mouse.x, mouse.y).dayIndex) }
        }
        Repeater {
            model: month.days
            delegate: Item {
                id: cell
                required property string modelData
                required property int index
                readonly property int dayNumber: CalendarLayout.dayNumber(modelData)
                readonly property bool inMonth: modelData.slice(0, 7) === month.shownMonth
                readonly property bool today: modelData === month.grid.todayDate
                readonly property var list: month.cells[index] || []
                readonly property bool overflowing: list.length > month.capacity
                readonly property int shown: overflowing ? Math.max(0, month.capacity - 1) : list.length
                readonly property bool ghosted: !!month.ghost && dayNumber >= month.ghost.first && dayNumber <= month.ghost.last
                objectName: "calendarGridMonthCell_" + modelData
                x: (index % 7) * month.cellWidth
                y: Math.floor(index / 7) * month.cellHeight
                width: month.cellWidth
                height: month.cellHeight
                Accessible.role: Accessible.Cell
                Accessible.name: month.grid.dayLabel(modelData) + (today ? qsTr(", today") : "")
                Accessible.description: qsTr("%n event(s)", "", list.length)
                Rectangle {
                    anchors.fill: parent
                    color: cell.ghosted ? Tk.Theme.color.dropZone : cell.inMonth ? Tk.Theme.color.panel : Tk.Theme.color.panelAlt
                }
                Rectangle { anchors.right: parent.right; width: Tk.Theme.size.border; height: parent.height; color: Tk.Theme.color.divider }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Tk.Theme.size.border; color: Tk.Theme.color.divider }
                Rectangle {
                    x: Tk.Theme.space.xs
                    y: Tk.Theme.space.xs
                    implicitWidth: Math.max(Tk.Theme.size.controlSm, dateLabel.implicitWidth + Tk.Theme.space.sm * 2)
                    implicitHeight: Tk.Theme.size.controlSm
                    width: implicitWidth
                    height: implicitHeight
                    radius: Tk.Theme.radius.full
                    color: cell.today ? Tk.Theme.color.accent : "transparent"
                    Tk.Label {
                        id: dateLabel
                        anchors.centerIn: parent
                        text: cell.modelData.slice(8, 10) === "01"
                              ? "1 " + month.grid.monthName(Number(cell.modelData.slice(5, 7)), Locale.ShortFormat)
                              : String(Number(cell.modelData.slice(8, 10)))
                        font.weight: cell.today || cell.modelData.slice(8, 10) === "01" ? Font.DemiBold : Font.Normal
                        color: cell.today ? Tk.Theme.color.accentContrast : cell.inMonth ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
                    }
                }
                Repeater {
                    model: cell.list.slice(0, cell.shown)
                    delegate: Tk.CalendarGridEvent {
                        required property var modelData
                        required property int index
                        grid: month.grid
                        piece: ({ item: modelData.item, first: true, continuesBefore: modelData.continuesBefore,
                                  continuesAfter: modelData.continuesAfter })
                        area: month
                        variant: "chip"
                        x: Tk.Theme.space.xs
                        y: month.dateHeight + index * (month.chipHeight + month.chipGap)
                        width: Math.max(0, cell.width - Tk.Theme.space.sm)
                        height: month.chipHeight
                    }
                }
                Tk.CalendarGridMore {
                    objectName: "calendarGridMore_" + cell.modelData
                    visible: cell.overflowing
                    count: cell.list.length - cell.shown
                    x: Tk.Theme.space.xs
                    y: month.dateHeight + cell.shown * (month.chipHeight + month.chipGap)
                    height: month.chipHeight
                    onClicked: month.grid.moreRequested(cell.modelData)
                }
            }
        }
        Loader {
            active: !!month.ghost && month.ghost.index >= 0
            z: 2
            sourceComponent: Rectangle {
                id: ghostChip
                objectName: "calendarGridGhost"
                readonly property var g: month.ghost
                x: (g.index % 7) * month.cellWidth + Tk.Theme.space.xs
                y: Math.floor(g.index / 7) * month.cellHeight + month.dateHeight
                width: Math.max(0, month.cellWidth - Tk.Theme.space.sm)
                height: month.chipHeight
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
                    text: ghostChip.g.title
                    font.weight: Font.DemiBold
                }
            }
        }
    }
}
