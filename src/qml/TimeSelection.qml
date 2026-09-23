// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// An edit, loop or punch range over timeline lanes, with precise endpoints.
//
// AGENT-CONTRACT: startTicks/endTicks are owner state (decimal tick strings).
// A pointer, keyboard or numeric change is a draft until it commits once
// through rangeCommitted; rangeCanceled restores the committed range. While
// an endpoint is dragged the other endpoint is the anchor: dragging past it
// swaps which handle is the start and the range stays normalized
// (start <= end). An empty range commits only when allowEmpty is true;
// otherwise the drag is rejected and the committed range is restored. All
// tick arithmetic happens in TimelineModel, never in QML Numbers.
Item {
    id: selection

    property Tk.TimelineViewport viewport: null
    property Tk.TimelineModel model: null
    property string startTicks: ""
    property string endTicks: ""
    property string kind: "edit" // edit, loop or punch
    property bool allowEmpty: true
    property bool interactive: true
    property bool snapEnabled: true
    property string snapGrid: "frames"
    property real snapPixels: Tk.Theme.space.md
    property string playheadTicks: ""
    property string displayMode: "timecode"
    property bool dropFrame: false
    property string tooltip: selection.kind === "loop" ? qsTr("Loop range")
                           : selection.kind === "punch" ? qsTr("Punch range") : qsTr("Edit range")
    readonly property bool dragging: internal.dragging
    readonly property string draftStartTicks: internal.dragging ? internal.draftStart : selection.startTicks
    readonly property string draftEndTicks: internal.dragging ? internal.draftEnd : selection.endTicks
    readonly property bool hasRange: selection.draftStartTicks.length > 0 && selection.draftEndTicks.length > 0
    readonly property color tint: selection.kind === "loop" ? Tk.Theme.color.success
                                : selection.kind === "punch" ? Tk.Theme.color.danger : Tk.Theme.color.accent
    readonly property real startX: selection.xOf(selection.draftStartTicks)
    readonly property real endX: selection.xOf(selection.draftEndTicks)

    signal rangePreviewed(string startTicks, string endTicks)
    signal rangeCommitted(string startTicks, string endTicks)
    signal rangeCanceled()
    signal rangeRejected(string reason)
    signal rangeCleared()
    signal numericEditRequested(string handle)

    function xOf(ticks) {
        if (!selection.viewport || ticks.length === 0) return 0
        void selection.viewport.originTicks
        void selection.viewport.pixelsPerSecond
        const x = selection.viewport.xForTicks(ticks)
        return Math.max(-selection.width, Math.min(2 * selection.width, x))
    }
    function formatted(ticks) {
        if (!selection.model || ticks.length === 0) return ""
        void selection.model.revision
        return selection.model.formatTicks(ticks, selection.displayMode, selection.dropFrame)
    }
    function snapped(ticks) {
        if (!selection.model || !selection.viewport || !selection.snapEnabled) return ticks
        return selection.model.snapTicks(ticks, {
            toleranceTicks: selection.viewport.ticksForPixels(selection.snapPixels),
            grid: selection.snapGrid, playheadTicks: selection.playheadTicks, editRange: false
        }).ticks
    }
    function normalize(a, b) {
        return selection.model.compareTicks(a, b) <= 0 ? [a, b] : [b, a]
    }
    // handle: "start", "end", or "create" (a new range anchored at ticks).
    function beginDrag(handle, ticks) {
        if (!selection.model || !selection.enabled) return false
        internal.originalStart = selection.startTicks
        internal.originalEnd = selection.endTicks
        if (handle === "create" || !selection.hasRange) {
            internal.anchor = selection.snapped(ticks)
            internal.draftStart = internal.anchor
            internal.draftEnd = internal.anchor
        } else {
            internal.anchor = handle === "start" ? selection.endTicks : selection.startTicks
            internal.draftStart = selection.startTicks
            internal.draftEnd = selection.endTicks
        }
        internal.dragging = true
        return true
    }
    function updateDrag(ticks) {
        if (!internal.dragging || ticks.length === 0) return
        const range = selection.normalize(internal.anchor, selection.snapped(ticks))
        internal.draftStart = range[0]
        internal.draftEnd = range[1]
        selection.rangePreviewed(range[0], range[1])
    }
    function commitDrag() {
        if (!internal.dragging) return false
        internal.dragging = false
        return selection.commitRange(internal.draftStart, internal.draftEnd)
    }
    function cancelDrag() {
        if (!internal.dragging) return
        internal.dragging = false
        selection.rangeCanceled()
    }
    function commitRange(start, end) {
        if (!selection.model || start.length === 0 || end.length === 0) return false
        const range = selection.normalize(start, end)
        if (!selection.allowEmpty && selection.model.compareTicks(range[0], range[1]) === 0) {
            selection.rangeRejected(qsTr("%1 cannot be empty").arg(selection.tooltip))
            selection.rangeCanceled()
            return false
        }
        selection.rangeCommitted(range[0], range[1])
        return true
    }
    // Moves one endpoint by whole grid steps; crossing the other endpoint swaps them.
    function nudge(handle, steps) {
        if (!selection.model || !selection.hasRange || !selection.enabled) return false
        const grid = selection.snapGrid === "none" ? "frames" : selection.snapGrid
        const moving = handle === "start" ? selection.startTicks : selection.endTicks
        const other = handle === "start" ? selection.endTicks : selection.startTicks
        const next = selection.model.stepTicks(moving, grid, steps)
        return next.length > 0 && selection.commitRange(other, next)
    }
    function setNumeric(startText, endText) {
        if (!selection.model) return false
        const start = selection.model.parseTicks(startText, selection.displayMode, selection.dropFrame)
        const end = selection.model.parseTicks(endText, selection.displayMode, selection.dropFrame)
        if (!start.ok || !end.ok) {
            selection.rangeRejected(start.ok ? end.error : start.error)
            return false
        }
        return selection.commitRange(start.value, end.value)
    }
    function autoScrollBy(pixels) {
        if (!selection.viewport || !internal.dragging || pixels === 0) return
        selection.viewport.scrollByPixels(pixels, 0)
        selection.updateDrag(selection.viewport.ticksForX(internal.pointerX))
    }

    objectName: "timeSelection_" + selection.kind
    visible: selection.hasRange

    // A draft measured against another model's ticks must not commit.
    onModelChanged: selection.cancelDrag()

    QtObject {
        id: internal
        property bool dragging: false
        property string anchor: ""
        property string draftStart: ""
        property string draftEnd: ""
        property string originalStart: ""
        property string originalEnd: ""
        property real pointerX: 0
        property real autoScroll: 0
    }

    Rectangle {
        objectName: "timeSelectionBand"
        x: Math.min(selection.startX, selection.endX)
        width: Math.max(Tk.Theme.size.border, Math.abs(selection.endX - selection.startX))
        height: selection.height
        // Loop and punch reuse the selection role's translucency in their own hue.
        color: selection.kind === "edit" ? Tk.Theme.color.selection
                                         : Tk.Theme.alpha(selection.tint, Tk.Theme.color.selection.a)
        border.width: Tk.Theme.size.border
        border.color: selection.tint
    }

    FrameAnimation {
        running: internal.dragging && internal.autoScroll !== 0
        onTriggered: selection.autoScrollBy(internal.autoScroll * Tk.Theme.size.dropEdge * frameTime * 30)
    }

    Repeater {
        model: ["start", "end"]
        delegate: Item {
            id: handle
            required property string modelData
            readonly property string ticks: handle.modelData === "start" ? selection.draftStartTicks
                                                                          : selection.draftEndTicks
            objectName: "timeSelectionHandle_" + handle.modelData
            x: (handle.modelData === "start" ? selection.startX : selection.endX) - width / 2
            width: Tk.Theme.size.seamTarget
            height: selection.height
            activeFocusOnTab: selection.enabled
            Accessible.role: Accessible.Slider
            Accessible.name: handle.modelData === "start" ? qsTr("%1 start").arg(selection.tooltip)
                                                          : qsTr("%1 end").arg(selection.tooltip)
            Accessible.description: selection.formatted(handle.ticks)
            Accessible.focusable: selection.enabled
            Accessible.focused: handle.activeFocus
            Accessible.onIncreaseAction: selection.nudge(handle.modelData, 1)
            Accessible.onDecreaseAction: selection.nudge(handle.modelData, -1)
            Keys.onPressed: function(event) {
                const step = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
                if (event.key === Qt.Key_Left) selection.nudge(handle.modelData, -step)
                else if (event.key === Qt.Key_Right) selection.nudge(handle.modelData, step)
                else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) selection.rangeCleared()
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    selection.numericEditRequested(handle.modelData)
                else if (event.key === Qt.Key_Escape && selection.dragging) selection.cancelDrag()
                else return
                event.accepted = true
            }

            Rectangle {
                objectName: "timeSelectionGrip_" + handle.modelData
                x: (handle.width - width) / 2
                width: handle.activeFocus || handleArea.containsMouse || handleArea.pressed
                       ? Tk.Theme.size.seam : Tk.Theme.size.border
                height: handle.height
                color: selection.tint
            }
            Rectangle {
                objectName: "timeSelectionFocus_" + handle.modelData
                visible: handle.activeFocus
                anchors.fill: parent
                color: "transparent"
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.focus
                radius: Tk.Theme.radius.xs
            }
            MouseArea {
                id: handleArea
                anchors.fill: parent
                enabled: selection.interactive && selection.enabled
                hoverEnabled: true
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                onPressed: function(mouse) {
                    handle.forceActiveFocus()
                    internal.pointerX = handle.mapToItem(selection, mouse.x, mouse.y).x
                    selection.beginDrag(handle.modelData, handle.ticks)
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const x = handle.mapToItem(selection, mouse.x, mouse.y).x
                    internal.pointerX = x
                    const edge = Tk.Theme.size.dropEdge
                    internal.autoScroll = x < edge ? -Math.min(1, (edge - x) / edge)
                                        : x > selection.width - edge ? Math.min(1, (x - selection.width + edge) / edge) : 0
                    selection.updateDrag(selection.viewport.ticksForX(x))
                }
                onReleased: {
                    internal.autoScroll = 0
                    selection.commitDrag()
                }
                onCanceled: {
                    internal.autoScroll = 0
                    selection.cancelDrag()
                }
            }
        }
    }
}
