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
// committed); release commits one undo step, a lost grab or Escape cancels.
// Reorder targets are computed from the committed layout captured at the
// start, so the previewed order never feeds back into its own destination.
Item {
    id: headers

    property Tk.TimelineViewport viewport: null
    property Tk.TimelineModel model: null
    property string currentTrackId: ""
    readonly property bool gestureActive: internal.operation.length > 0

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
    function beginGesture(kind, trackId) {
        const begun = headers.model.beginEdit({ kind: kind, trackId: trackId,
                                                expectedRevision: headers.model.revision })
        internal.operation = begun.ok ? begun.operationId : ""
        internal.heightAtStart = headers.viewport.trackHeight(trackId)
        internal.slots = []
        for (let i = 0; i < headers.viewport.trackCount; ++i) {
            const id = headers.viewport.trackIdAt(i)
            internal.slots.push({ id: id, top: headers.viewport.trackY(id) + headers.viewport.scrollY,
                                  height: headers.viewport.trackHeight(id) })
        }
        if (!begun.ok) headers.report(begun)
    }
    function reorderTarget(trackId, y) {
        const content = y + headers.viewport.scrollY
        for (let i = 0; i < internal.slots.length; ++i) {
            const slot = internal.slots[i]
            if (content < slot.top + slot.height / 2) return slot.id === trackId ? "" : slot.id
        }
        return ""
    }
    function updateGesture(input) {
        if (internal.operation.length > 0) headers.model.updateEdit(internal.operation, input)
    }
    function finishGesture() {
        const operation = internal.operation
        internal.operation = ""
        if (operation.length > 0) headers.report(headers.model.commitEdit(operation))
    }
    function cancelGesture() {
        if (internal.operation.length === 0) return false
        headers.model.cancelEdit(internal.operation)
        internal.operation = ""
        return true
    }

    objectName: "timelineTrackHeaders"
    clip: true
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Tracks")

    QtObject {
        id: internal
        property string operation: ""
        property real heightAtStart: 0
        property var slots: []
    }

    Connections {
        target: headers.model
        ignoreUnknownSignals: true
        function onEditInvalidated(operationId, reason) {
            if (operationId !== internal.operation) return
            internal.operation = ""
            headers.report({ ok: false, error: reason })
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.headerBg
    }

    Repeater {
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
            current: trackId === headers.currentTrackId
            onSelectRequested: function(id) { headers.trackSelected(id) }
            onPropertyRequested: function(id, property, value) { headers.requestProperty(id, property, value) }
            onRenameRequested: function(id, text) { headers.requestProperty(id, "name", text) }
            onHeightStepRequested: function(id, steps) {
                headers.requestProperty(id, "height",
                                        Math.round(headers.viewport.trackHeight(id) + steps * Tk.Theme.space.lg))
            }
            onResizeStarted: function(id) { headers.beginGesture("resizeTrack", id) }
            onResizeMoved: function(id, delta) {
                headers.updateGesture({ trackHeight: Math.round(Math.max(Tk.Theme.size.row, internal.heightAtStart + delta)) })
            }
            onResizeFinished: headers.finishGesture()
            onResizeCanceled: headers.cancelGesture()
            onReorderStarted: function(id, y) { headers.beginGesture("reorderTrack", id) }
            onReorderMoved: function(id, y) { headers.updateGesture({ beforeTrackId: headers.reorderTarget(id, y) }) }
            onReorderFinished: headers.finishGesture()
            onReorderCanceled: headers.cancelGesture()
        }
    }
}
