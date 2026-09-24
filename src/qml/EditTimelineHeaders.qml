// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The track-header column of an EditTimeline: one TrackHeader per visible
// track row, scrolled with the lanes, and the resize/reorder sessions.
//
// AGENT-CONTRACT: header rows come from TimelineViewport, so they share the
// lanes' vertical scroll and virtualization. A resize or reorder drag is one
// TimelineModel session previewed live (the rows move before anything is
// committed); release commits one undo step. A lost grab, Escape, a replaced
// model or destruction cancels it, and the rest of that press does nothing.
// Reorder targets are computed from the committed layout captured at the
// start, so the previewed order never feeds back into its own destination.
// AGENT-NOTE: the column's own MouseArea, under headers that run with
// gestures: false, owns press, drag and double-click, like the lanes'
// pointer controller: a previewed reorder or an autoscroll recycles header
// delegates, and a gesture held by a delegate would die with it.
Item {
    id: headers

    property Tk.TimelineViewport viewport: null
    property Tk.TimelineModel model: null
    property string currentTrackId: ""
    readonly property bool gestureActive: internal.operation.length > 0
    readonly property string gestureKind: headers.gestureActive ? internal.kind : ""

    signal trackSelected(string trackId)
    signal editReported(var result)

    function report(result) {
        headers.editReported(result)
        return !!result && result.ok !== false
    }
    function requestProperty(trackId, property, value) {
        return headers.report(headers.model.applyEdit({ kind: "trackProperty", trackId: trackId,
                                                        property: property, value: value }))
    }
    function headerFor(trackId) {
        for (let i = 0; i < headerRepeater.count; ++i) {
            const item = headerRepeater.itemAt(i) as Tk.TrackHeader
            if (item && item.trackId === trackId) return item
        }
        return null
    }
    // A press on a header's background: the bottom seam resizes the track,
    // anywhere else selects it and may start a reorder.
    function pointerPressed(y) {
        const trackId = headers.viewport.trackIdAtY(y)
        internal.pressY = y
        internal.pointerY = y
        internal.trackId = trackId
        internal.phase = ""
        if (trackId.length === 0) return
        const bottom = headers.viewport.trackY(trackId) + headers.viewport.trackHeight(trackId)
        const seam = y >= bottom - Tk.Theme.size.seamTarget / 2
        internal.phase = seam ? "resizePending" : "reorderPending"
        if (!seam) headers.trackSelected(trackId)
    }
    function pointerMoved(y) {
        internal.pointerY = y
        // A resize starts on the first movement, so a click on the seam is not an edit.
        if (internal.phase === "resizePending" && y !== internal.pressY)
            headers.beginGesture("resizeTrack")
        else if (internal.phase === "reorderPending" && Math.abs(y - internal.pressY) >= Qt.styleHints.startDragDistance)
            headers.beginGesture("reorderTrack")
        if (!headers.gestureActive) return
        const edge = Tk.Theme.size.dropEdge
        internal.autoY = internal.kind !== "reorderTrack" ? 0
                       : y < edge ? -Math.min(1, (edge - y) / edge)
                       : y > headers.height - edge ? Math.min(1, (y - headers.height + edge) / edge) : 0
        headers.updateGesture()
    }
    function beginGesture(kind) {
        internal.phase = ""
        const begun = headers.model.beginEdit({ kind: kind, trackId: internal.trackId,
                                                expectedRevision: headers.model.revision })
        if (!headers.report(begun)) return
        internal.operation = begun.operationId
        internal.sessionModel = headers.model
        internal.kind = kind
        internal.heightAtStart = headers.viewport.trackHeight(internal.trackId)
        const slots = []
        for (let i = 0; i < headers.viewport.trackCount; ++i) {
            const id = headers.viewport.trackIdAt(i)
            slots.push({ id: id, top: headers.viewport.trackY(id) + headers.viewport.scrollY,
                         height: headers.viewport.trackHeight(id) })
        }
        internal.slots = slots
    }
    // Insert before the first slot whose midpoint is below the pointer. The
    // dragged track's own slot (or the one after it) plans no change at all.
    function reorderTarget(y) {
        const content = y + headers.viewport.scrollY
        for (let i = 0; i < internal.slots.length; ++i) {
            const slot = internal.slots[i]
            if (content < slot.top + slot.height / 2) return slot.id
        }
        return ""
    }
    function updateGesture() {
        if (!headers.gestureActive) return
        if (internal.kind === "resizeTrack") {
            const height = Math.max(Tk.Theme.size.row, internal.heightAtStart + internal.pointerY - internal.pressY)
            internal.sessionModel.updateEdit(internal.operation, { trackHeight: Math.round(height) })
        } else {
            internal.sessionModel.updateEdit(internal.operation,
                                             { beforeTrackId: headers.reorderTarget(internal.pointerY) })
        }
    }
    function finishGesture() {
        internal.phase = ""
        internal.autoY = 0
        const operation = internal.operation
        const owner = internal.sessionModel
        internal.operation = ""
        internal.sessionModel = null
        if (operation.length > 0) headers.report(owner.commitEdit(operation))
    }
    function cancelGesture() {
        internal.phase = ""
        internal.autoY = 0
        if (!headers.gestureActive) return false
        if (internal.sessionModel) internal.sessionModel.cancelEdit(internal.operation)
        internal.operation = ""
        internal.sessionModel = null
        return true
    }
    // One autoscroll step while a reorder drag is held past either edge.
    function autoScrollStep(seconds) {
        if (!headers.gestureActive || internal.autoY === 0) return
        headers.viewport.scrollByPixels(0, internal.autoY * Tk.Theme.size.dropEdge * 30 * seconds)
        headers.updateGesture()
    }
    function renameAt(y) {
        const header = headers.headerFor(headers.viewport.trackIdAtY(y))
        if (header) header.startRename()
    }

    objectName: "timelineTrackHeaders"
    clip: true
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Tracks")

    onModelChanged: headers.cancelGesture()
    Component.onDestruction: headers.cancelGesture()

    QtObject {
        id: internal
        property string phase: ""      // "", resizePending, reorderPending
        property string kind: ""       // resizeTrack or reorderTrack while operation is set
        property string operation: ""
        property Tk.TimelineModel sessionModel: null
        property string trackId: ""
        property real pressY: 0
        property real pointerY: 0
        property real autoY: 0
        property real heightAtStart: 0
        property var slots: []
    }

    Connections {
        target: headers.model
        ignoreUnknownSignals: true
        function onEditInvalidated(operationId, reason) {
            if (operationId !== internal.operation) return
            internal.operation = ""
            internal.sessionModel = null
            internal.autoY = 0
            headers.report({ ok: false, error: reason })
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.headerBg
    }

    FrameAnimation {
        running: headers.gestureActive && internal.autoY !== 0
        onTriggered: headers.autoScrollStep(frameTime)
    }

    MouseArea {
        objectName: "trackHeaderPointer"
        anchors.fill: parent
        enabled: headers.enabled && !!headers.model && !!headers.viewport
        preventStealing: headers.gestureActive
        cursorShape: internal.kind === "resizeTrack" && headers.gestureActive ? Qt.SizeVerCursor
                   : internal.kind === "reorderTrack" && headers.gestureActive ? Qt.ClosedHandCursor : Qt.ArrowCursor
        onPressed: function(mouse) { headers.pointerPressed(mouse.y) }
        onPositionChanged: function(mouse) { if (pressed) headers.pointerMoved(mouse.y) }
        onReleased: headers.finishGesture()
        onCanceled: headers.cancelGesture()
        onDoubleClicked: function(mouse) { headers.renameAt(mouse.y) }
    }

    Repeater {
        id: headerRepeater
        model: headers.viewport ? headers.viewport.tracks : null
        delegate: Tk.TrackHeader {
            required property string key
            required trackId
            required name
            required kind
            required muted
            required solo
            required armed
            required locked
            required hidden
            required routing
            required trackColor
            required clipCount
            required preview
            required property real trackY
            required property real trackHeight
            y: trackY
            width: headers.width
            height: trackHeight
            gestures: false
            current: trackId === headers.currentTrackId
            resizing: headers.gestureKind === "resizeTrack" && trackId === internal.trackId
            onPropertyRequested: function(id, property, value) { headers.requestProperty(id, property, value) }
            onRenameRequested: function(id, text) { headers.requestProperty(id, "name", text) }
            onHeightStepRequested: function(id, steps) {
                headers.requestProperty(id, "height",
                                        Math.round(headers.viewport.trackHeight(id) + steps * Tk.Theme.space.lg))
            }
        }
    }
}
