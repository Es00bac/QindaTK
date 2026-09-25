// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// One visible piece of an occurrence in CalendarGridView: a timed block, an
// all-day bar or a month chip. Internal part; the owner positions it and
// supplies `area` (CalendarGridTime, CalendarGridAllDay or CalendarGridMonth),
// which maps pointer positions to {dayIndex, minute} slots.
// AGENT-GUARD: a drag only reports slots to the grid; the grid turns them
// into a request and never moves this item or edits `row` (D-301).
T.Control {
    id: block

    required property Tk.CalendarGridView grid
    required property var piece       // {item, first, continuesBefore, continuesAfter, …}
    required property var area
    property string variant: "timed"  // timed | bar | chip

    readonly property var entry: piece.item
    readonly property var row: entry.row
    readonly property string key: entry.key
    readonly property bool calendarGridEvent: true
    readonly property color tone: grid.colorFor(row)
    readonly property bool selected: grid.selectedOccurrenceId === key
    readonly property bool editable: grid.canEdit(row)
    readonly property bool hot: pointer.containsMouse
    readonly property bool dragging: !!grid.dragState && !!grid.dragState.entry && grid.dragState.entry.key === key
    readonly property bool filled: variant !== "chip" || entry.lane
    readonly property bool resizable: variant === "timed" && editable && !piece.continuesAfter
    readonly property real edgeWidth: Tk.Theme.space.xs + Tk.Theme.size.border
    readonly property real resizeZone: Tk.Theme.space.sm + Tk.Theme.size.border
    readonly property color fill: Tk.Theme.mix(Tk.Theme.color.panel, tone,
                                               selected || hot ? Tk.Theme.opacity.tintStrong : Tk.Theme.opacity.tint)
    readonly property string title: String(row.title || qsTr("Untitled event"))
    readonly property string timeRange: entry.lane ? qsTr("All day")
        : grid.rangeText(entry.start, entry.end)
    readonly property real textHeight: availableHeight
    readonly property bool twoLines: textHeight >= titleLabel.implicitHeight + timeLabel.implicitHeight
    readonly property bool threeLines: twoLines && textHeight >= titleLabel.implicitHeight + timeLabel.implicitHeight * 2
    // Narrow pieces (overlaps at phone widths) drop decoration before the
    // title, and a sliver too thin for a glyph shows only its colour.
    readonly property bool roomy: width > Tk.Theme.size.fieldWidth
    readonly property bool legible: width >= Tk.Theme.size.controlLg

    objectName: "calendarGridEvent_" + key
    implicitWidth: Tk.Theme.size.fieldWidth
    implicitHeight: Tk.Theme.size.controlSm
    opacity: dragging ? Tk.Theme.opacity.ghost : Tk.Theme.opacity.full
    focusPolicy: Qt.StrongFocus
    // AGENT-GUARD: roving tab stop (one event per grid is in the Tab chain;
    // Tab/arrows then walk events in time order). Qt refuses clearing
    // activeFocusOnTab on the focused item, so the focused piece keeps it.
    activeFocusOnTab: activeFocus || (piece.first && grid.tabStopKey === key)
    leftPadding: filled && variant !== "chip" ? edgeWidth + Tk.Theme.space.sm : Tk.Theme.space.sm
    rightPadding: Tk.Theme.space.xs
    topPadding: variant === "timed" ? Tk.Theme.space.xs : 0
    bottomPadding: variant === "timed" ? Tk.Theme.space.xs : 0

    Accessible.role: Accessible.Button
    Accessible.name: grid.accessibleName(entry)
    Accessible.description: grid.accessibleDescription(entry)
    Accessible.focusable: true
    Accessible.focused: activeFocus
    Accessible.onPressAction: grid.open(row)

    Component.onCompleted: if (piece.first) grid._registerEvent(key, block)
    Component.onDestruction: grid._unregisterEvent(key, block)
    onActiveFocusChanged: if (activeFocus) grid._noteFocus(entry)
    Keys.onPressed: function(event) { event.accepted = block.grid.handleEventKey(block.entry, event) }

    background: Item {
        Rectangle {
            // The calendar colour shows as the left edge; the tinted fill
            // keeps text in the theme's text role on every theme.
            anchors.fill: parent
            visible: block.filled
            radius: block.variant === "timed" ? Tk.Theme.radius.md : Tk.Theme.radius.sm
            color: block.tone
            Rectangle {
                x: block.edgeWidth
                width: parent.width - block.edgeWidth
                height: parent.height
                radius: parent.radius
                color: block.fill
            }
            Rectangle {
                x: block.edgeWidth
                width: Math.min(parent.radius, Math.max(0, parent.width - block.edgeWidth))
                height: parent.height
                color: block.fill
            }
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.width: Tk.Theme.size.border
                border.color: block.selected ? block.tone : Tk.Theme.color.panel
            }
        }
        Rectangle {
            anchors.fill: parent
            visible: !block.filled
            radius: Tk.Theme.radius.sm
            color: block.selected ? Tk.Theme.color.selection : block.hot ? Tk.Theme.color.hover : "transparent"
        }
        Rectangle {
            objectName: "calendarGridFocusRing"
            anchors.fill: parent
            anchors.margins: -Tk.Theme.size.focusRing
            visible: block.activeFocus
            radius: Tk.Theme.radius.md + Tk.Theme.size.focusRing
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.focus
        }
    }

    contentItem: Item {
        clip: true
        visible: block.legible
        Tk.Flex {
            anchors.fill: parent
            visible: block.variant === "timed"
            direction: Tk.Flex.Column
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                Tk.Label {
                    id: titleLabel
                    text: block.title
                    font.weight: Font.DemiBold
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.Label {
                    visible: !block.twoLines && !block.entry.lane && block.roomy
                    text: block.grid.timeText(block.entry.start % 1440, true)
                    muted: true
                    font.pixelSize: Tk.Theme.font.caption
                    Tk.Flex.shrink: 0
                }
                Tk.Icon { visible: block.entry.recurring && block.roomy; name: "repeat"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
                Tk.Icon { visible: !block.editable && block.roomy; name: "lock"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
            }
            Tk.Caption { id: timeLabel; visible: block.twoLines; text: block.timeRange }
            Tk.Caption { visible: block.threeLines && !!block.row.location; text: String(block.row.location || "") }
            Tk.Flex {
                visible: block.threeLines && !!block.row.overridden && block.availableWidth >= movedBadge.implicitWidth
                Tk.Badge { id: movedBadge; objectName: "calendarGridMoved"; text: qsTr("Moved"); variant: "info" }
            }
        }
        Tk.Flex {
            anchors.fill: parent
            visible: block.variant !== "timed"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Icon { visible: block.piece.continuesBefore && block.variant === "bar"; name: "chevron-left"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
            Rectangle {
                visible: !block.filled
                implicitWidth: Tk.Theme.space.md
                implicitHeight: Tk.Theme.space.md
                radius: Tk.Theme.radius.full
                color: block.tone
            }
            Tk.Label {
                visible: !block.entry.lane && block.roomy
                text: block.grid.timeText(block.piece.continuesBefore ? 0 : block.entry.start % 1440, true)
                muted: true
                font.pixelSize: Tk.Theme.font.caption
                Tk.Flex.shrink: 0
            }
            Tk.Label {
                text: block.title
                font.weight: block.filled ? Font.DemiBold : Font.Normal
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Icon { visible: block.entry.recurring && block.roomy; name: "repeat"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
            Tk.Icon { visible: block.piece.continuesAfter && block.variant === "bar"; name: "chevron-right"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: block.resizable && pointer.containsMouse && pointer.mouseY >= height - block.resizeZone ? Qt.SizeVerCursor
                   : block.editable ? Qt.OpenHandCursor : Qt.PointingHandCursor
        property point pressAt
        property string pressKind: "move"
        property bool started: false
        property bool refused: false
        onPressed: function(mouse) {
            block.forceActiveFocus(Qt.MouseFocusReason)
            block.grid.select(block.row)
            pressAt = Qt.point(mouse.x, mouse.y)
            pressKind = block.resizable && mouse.y >= height - block.resizeZone ? "resize" : "move"
            started = false
            refused = false
        }
        onPositionChanged: function(mouse) {
            if (!pressed || refused) return
            if (!started) {
                if (Math.abs(mouse.x - pressAt.x) < block.grid.dragDistance
                        && Math.abs(mouse.y - pressAt.y) < block.grid.dragDistance) return
                started = block.grid.beginDrag(pressKind, block.area.areaName, block.entry, block.area.slotFrom(pointer, pressAt.x, pressAt.y))
                refused = !started
                if (!started) return
            }
            block.area.dragTo(pointer, mouse.x, mouse.y)
        }
        onReleased: {
            if (started) block.grid.endDrag()
            started = false
            block.area.dragTo(null, 0, 0)
        }
        onCanceled: {
            if (started) block.grid.cancelDrag()
            started = false
            block.area.dragTo(null, 0, 0)
        }
        onDoubleClicked: block.grid.open(block.row)
    }
    Tk.ToolTip {
        text: block.title + "\n" + block.grid.spanText(block.entry) + (block.row.location ? "\n" + String(block.row.location) : "")
        visible: block.hot && !block.grid.dragState
    }
}
