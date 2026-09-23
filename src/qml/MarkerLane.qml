// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Editable cues and regions above timeline tracks.
//
// AGENT-CONTRACT: markers are TimelineModel records with stable IDs and
// exact positions; this lane draws TimelineViewport marker rows and edits
// through model transactions only. A drag is one MoveMarker session: Escape
// or a lost grab cancels it and restores the committed position; release
// commits one undo step. Keyboard: Left/Right choose the previous/next
// marker, Alt+Left/Right move it one grid step, Delete removes it, F2 or
// Return renames it and M adds one at the playhead.
Item {
    id: lane

    property Tk.TimelineViewport viewport: null
    property Tk.TimelineModel model: null
    property string currentMarkerId: ""
    property bool snapEnabled: true
    property string snapGrid: "frames"
    property real snapPixels: Tk.Theme.space.md
    property string playheadTicks: "0"
    property string displayMode: "timecode"
    property bool dropFrame: false
    property string tooltip: qsTr("Markers")
    readonly property bool dragging: internal.operationId.length > 0
    readonly property var currentMarker: {
        void (lane.model ? lane.model.revision : "")
        return lane.model && lane.currentMarkerId.length > 0 ? lane.model.markerData(lane.currentMarkerId) : ({})
    }

    signal markerActivated(string markerId)
    signal editRejected(string reason)

    function report(result) {
        if (result && result.ok === false) lane.editRejected(result.error || qsTr("The marker edit was rejected"))
        return !!result && result.ok !== false
    }
    function formatted(ticks) {
        return lane.model ? lane.model.formatTicks(ticks, lane.displayMode, lane.dropFrame) : ""
    }
    function markerAt(x) {
        if (!lane.viewport) return ""
        const rows = lane.viewport.markers
        const reach = Tk.Theme.size.icon
        let best = ""
        let bestDistance = reach + 1
        for (let i = 0; i < rows.count; ++i) {
            const row = rows.get(i)
            if (row.ghost) continue
            const inside = row.markerWidth > 0 && x >= row.markerX && x <= row.markerX + row.markerWidth
            const distance = inside ? 0 : Math.abs(x - row.markerX)
            if (distance <= bestDistance) {
                best = row.markerId
                bestDistance = distance
            }
        }
        return best
    }
    function select(markerId) {
        lane.currentMarkerId = markerId
        if (lane.viewport && markerId.length > 0)
            lane.viewport.ensureTicksVisible(lane.model.markerData(markerId).position, Tk.Theme.size.dropEdge)
    }
    function step(direction) {
        if (!lane.model) return
        const from = lane.currentMarkerId.length > 0 ? lane.currentMarker.position : lane.playheadTicks
        const next = lane.model.adjacentMarker(from, direction)
        if (next.length > 0) lane.select(next)
    }
    function moveCurrent(steps) {
        if (!lane.model || lane.currentMarkerId.length === 0) return false
        const position = lane.currentMarker.position
        const grid = lane.snapGrid === "none" ? "frames" : lane.snapGrid
        return lane.report(lane.model.applyEdit({ kind: "moveMarker", markerId: lane.currentMarkerId,
                                                  grabTicks: position },
                                                { pointerTicks: lane.model.stepTicks(position, grid, steps),
                                                  snap: false }))
    }
    function addAt(ticks) {
        if (!lane.model || ticks.length === 0) return false
        const result = lane.model.applyEdit({ kind: "addMarker",
                                              marker: { position: ticks, label: qsTr("Marker") } })
        if (!lane.report(result)) return false
        lane.currentMarkerId = lane.model.adjacentMarker(lane.model.subtractTicks(ticks, "1"), 1)
        return true
    }
    function removeCurrent() {
        if (!lane.model || lane.currentMarkerId.length === 0) return false
        const removed = lane.currentMarkerId
        const next = lane.model.adjacentMarker(lane.currentMarker.position, 1)
        if (!lane.report(lane.model.applyEdit({ kind: "removeMarker", markerId: removed }))) return false
        lane.currentMarkerId = next
        return true
    }
    function startRename() {
        if (lane.currentMarkerId.length === 0) return
        renameEditor.text = lane.currentMarker.label
        renameEditor.visible = true
        renameEditor.forceActiveFocus()
        renameEditor.selectAll()
    }
    function finishRename(accept) {
        if (!renameEditor.visible) return
        renameEditor.visible = false
        lane.forceActiveFocus()
        if (accept && renameEditor.text !== lane.currentMarker.label)
            lane.report(lane.model.applyEdit({ kind: "editMarker", markerId: lane.currentMarkerId,
                                               marker: { label: renameEditor.text } }))
    }
    function cancelDrag() {
        if (internal.operationId.length > 0 && lane.model) lane.model.cancelEdit(internal.operationId)
        internal.operationId = ""
        internal.autoScroll = 0
    }
    function updateDrag() {
        if (internal.operationId.length === 0) return
        lane.model.updateEdit(internal.operationId, {
            pointerTicks: lane.viewport.ticksForX(Math.max(0, Math.min(lane.width, internal.pointerX))),
            snap: lane.snapEnabled, snapToleranceTicks: lane.viewport.ticksForPixels(lane.snapPixels),
            grid: lane.snapGrid, playheadTicks: lane.playheadTicks })
    }
    // One autoscroll step while a marker is dragged near either edge.
    function autoScrollStep(seconds) {
        if (internal.operationId.length === 0 || internal.autoScroll === 0) return
        lane.viewport.scrollByPixels(internal.autoScroll * Tk.Theme.size.dropEdge * 30 * seconds, 0)
        lane.updateDrag()
    }

    objectName: "markerLane"
    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.row
    clip: true
    activeFocusOnTab: lane.enabled

    Accessible.role: Accessible.List
    Accessible.name: lane.tooltip
    Accessible.description: lane.currentMarkerId.length > 0
                            ? qsTr("%1 at %2. Left and Right choose a marker; Alt+Left and Alt+Right move it; Delete removes it; F2 renames it; M adds a marker at the playhead.")
                              .arg(lane.currentMarker.label || qsTr("Marker")).arg(lane.formatted(lane.currentMarker.position || "0"))
                            : qsTr("No marker selected. Left and Right choose a marker; M adds a marker at the playhead.")
    Accessible.focusable: lane.enabled
    Accessible.focused: lane.activeFocus

    Keys.onPressed: function(event) {
        if (!lane.enabled || !lane.model) return
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        const steps = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
        switch (event.key) {
        case Qt.Key_Left: if (alt) lane.moveCurrent(-steps); else lane.step(-1); break
        case Qt.Key_Right: if (alt) lane.moveCurrent(steps); else lane.step(1); break
        case Qt.Key_Delete: case Qt.Key_Backspace: lane.removeCurrent(); break
        case Qt.Key_F2: case Qt.Key_Return: case Qt.Key_Enter: lane.startRename(); break
        case Qt.Key_M: case Qt.Key_Insert: lane.addAt(lane.playheadTicks); break
        case Qt.Key_Escape: if (!lane.dragging) return; lane.cancelDrag(); break
        default: return
        }
        event.accepted = true
    }

    QtObject {
        id: internal
        property string operationId: ""
        property real pointerX: 0
        property real autoScroll: 0
    }

    FrameAnimation {
        running: internal.operationId.length > 0 && internal.autoScroll !== 0
        onTriggered: lane.autoScrollStep(frameTime)
    }

    Connections {
        target: lane.model
        ignoreUnknownSignals: true
        function onEditInvalidated(operationId, reason) {
            if (operationId === internal.operationId) {
                internal.operationId = ""
                lane.editRejected(reason)
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.surface
        Rectangle {
            width: parent.width
            height: Tk.Theme.size.border
            y: parent.height - height
            color: Tk.Theme.color.divider
        }
    }

    Repeater {
        model: lane.viewport ? lane.viewport.markers : null
        delegate: Item {
            id: marker
            required property string key
            required property string markerId
            required property real markerX
            required property real markerWidth
            required property string positionTicks
            required property string label
            required property string markerColor
            required property bool locked
            required property bool preview
            required property bool ghost
            readonly property color ink: marker.markerColor.length > 0 ? marker.markerColor : Tk.Theme.color.warning
            readonly property bool current: marker.markerId === lane.currentMarkerId && !marker.ghost
            objectName: marker.ghost ? "markerGhost_" + marker.markerId : "marker_" + marker.markerId
            x: marker.markerX
            width: Math.max(marker.markerWidth, Tk.Theme.size.iconSm)
            height: lane.height
            opacity: marker.ghost ? Tk.Theme.opacity.ghost : Tk.Theme.opacity.full
            Accessible.role: Accessible.ListItem
            Accessible.ignored: marker.ghost
            Accessible.name: marker.label.length > 0 ? marker.label : qsTr("Marker")
            Accessible.description: lane.formatted(marker.positionTicks) + (marker.locked ? ", " + qsTr("locked") : "")
            Accessible.selectable: true
            Accessible.selected: marker.current
            Accessible.focused: marker.current && lane.activeFocus
            Accessible.onPressAction: lane.select(marker.markerId)
            Accessible.onIncreaseAction: { lane.select(marker.markerId); lane.moveCurrent(1) }
            Accessible.onDecreaseAction: { lane.select(marker.markerId); lane.moveCurrent(-1) }

            Rectangle {
                objectName: "markerRegion_" + marker.markerId
                visible: marker.markerWidth > 0
                width: marker.markerWidth
                height: parent.height
                color: Tk.Theme.alpha(marker.ink, Tk.Theme.color.selection.a)
            }
            Rectangle {
                width: Tk.Theme.size.border
                height: parent.height
                color: marker.ink
            }
            Tk.Icon {
                x: Tk.Theme.size.border
                y: (parent.height - height) / 2
                name: "flag"
                size: Tk.Theme.size.iconSm
                color: marker.ink
            }
            Tk.Caption {
                objectName: "markerLabel_" + marker.markerId
                x: Tk.Theme.size.iconSm + Tk.Theme.space.xs
                y: (parent.height - height) / 2
                width: Math.max(0, (marker.markerWidth > 0 ? marker.markerWidth : Tk.Theme.size.labelWidth) - x)
                text: marker.label
                color: marker.current ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                elide: Text.ElideRight
            }
            Rectangle {
                objectName: "markerFocus_" + marker.markerId
                visible: marker.current && lane.activeFocus
                width: Math.max(Tk.Theme.size.iconSm + Tk.Theme.size.border, marker.markerWidth)
                height: parent.height
                color: "transparent"
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.focus
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: lane.enabled && !!lane.model && !!lane.viewport
        preventStealing: internal.operationId.length > 0
        property real pressX: 0
        property string pressTicks: ""
        property string pressMarker: ""
        onPressed: function(mouse) {
            lane.forceActiveFocus()
            pressX = mouse.x
            pressTicks = lane.viewport.ticksForX(mouse.x)
            pressMarker = lane.markerAt(mouse.x)
            if (pressMarker.length > 0) lane.currentMarkerId = pressMarker
        }
        onPositionChanged: function(mouse) {
            if (!pressed || pressMarker.length === 0) return
            if (internal.operationId.length === 0) {
                if (Math.abs(mouse.x - pressX) < Qt.styleHints.startDragDistance) return
                const begun = lane.model.beginEdit({ kind: "moveMarker", markerId: pressMarker,
                                                     grabTicks: pressTicks,
                                                     expectedRevision: lane.model.revision })
                if (!lane.report(begun)) {
                    pressMarker = ""
                    return
                }
                internal.operationId = begun.operationId
            }
            internal.pointerX = mouse.x
            const edge = Tk.Theme.size.dropEdge
            internal.autoScroll = mouse.x < edge ? -Math.min(1, (edge - mouse.x) / edge)
                                : mouse.x > lane.width - edge ? Math.min(1, (mouse.x - lane.width + edge) / edge) : 0
            lane.updateDrag()
        }
        onReleased: {
            internal.autoScroll = 0
            if (internal.operationId.length === 0) return
            const operation = internal.operationId
            internal.operationId = ""
            lane.report(lane.model.commitEdit(operation))
        }
        onCanceled: lane.cancelDrag()
        onDoubleClicked: function(mouse) {
            const hit = lane.markerAt(mouse.x)
            if (hit.length > 0) {
                lane.currentMarkerId = hit
                lane.markerActivated(hit)
                lane.startRename()
                return
            }
            const ticks = lane.viewport.ticksForX(mouse.x)
            lane.addAt(lane.snapEnabled ? lane.model.snapTicks(ticks, {
                toleranceTicks: lane.viewport.ticksForPixels(lane.snapPixels), grid: lane.snapGrid,
                playheadTicks: lane.playheadTicks }).ticks : ticks)
        }
    }

    Tk.TextField {
        id: renameEditor
        objectName: "markerRenameEditor"
        visible: false
        small: true
        z: 2
        x: {
            if (!lane.viewport) return 0
            void lane.viewport.originTicks
            void lane.viewport.pixelsPerSecond
            return Math.max(0, lane.viewport.xForTicks(lane.currentMarker.position || "0"))
        }
        width: Tk.Theme.size.fieldWidth
        height: lane.height
        tooltip: qsTr("Marker name")
        onAccepted: lane.finishRename(true)
        onActiveFocusChanged: if (!activeFocus) lane.finishRename(false)
        Keys.onEscapePressed: function(event) {
            event.accepted = true
            lane.finishRename(false)
        }
    }
}
