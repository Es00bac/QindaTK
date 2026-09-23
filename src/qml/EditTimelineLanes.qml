// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The track-lane surface of an EditTimeline: lane stripes, clip delegates,
// the edit range, playhead, snap guide and the single pointer controller.
//
// AGENT-NOTE: one MouseArea owns every clip gesture and hit-tests through
// TimelineViewport instead of per-delegate handlers. Delegates are recycled
// as clips scroll in and out (and a dragged clip may leave the viewport
// during autoscroll); a gesture held by a delegate would die with it.
// AGENT-GUARD: a drag is exactly one TimelineModel edit session. Every exit
// path (release, Escape, lost grab, model invalidation) either commits or
// cancels that session; never leave internal.operationId set on return.
Item {
    id: lanes

    property Tk.TimelineViewport viewport: null
    property Tk.TimelineModel model: null
    property var mediaProvider: null
    property var selectedMap: ({})
    property var selectedClipIds: []
    property string currentClipId: ""
    property string currentTrackId: ""
    property bool focusActive: false
    property string playheadTicks: "0"
    property bool snapEnabled: true
    property string snapGrid: "frames"
    property real snapPixels: Tk.Theme.space.md
    property bool rippleMode: false
    property string displayMode: "timecode"
    property bool dropFrame: false
    readonly property alias editRange: editRangeItem
    readonly property bool gestureActive: internal.phase === "drag" || internal.phase === "range"
    readonly property string gestureKind: internal.phase === "drag" ? internal.kind : internal.phase
    readonly property var preview: internal.preview
    readonly property string hoverClipId: internal.hoverClipId

    signal clipPressed(string clipId, string trackId, int modifiers)
    signal emptyPressed(string trackId, int modifiers)
    signal emptyClicked(string ticks, int modifiers)
    signal clipActivated(string clipId)
    signal nudgeRequested(string clipId, int steps)
    signal editRejected(string reason)
    signal editCommitted(string transactionId)
    signal rangeCommitted(string startTicks, string endTicks)
    signal rangeCleared()
    signal rangeNumericEditRequested()

    function xOf(ticks) {
        if (!lanes.viewport || ticks.length === 0) return -lanes.width
        void lanes.viewport.originTicks
        void lanes.viewport.pixelsPerSecond
        return Math.max(-lanes.width, Math.min(2 * lanes.width, lanes.viewport.xForTicks(ticks)))
    }
    function timeText(start, duration) {
        if (!lanes.model) return ""
        return qsTr("Starts %1, length %2").arg(lanes.model.formatTicks(start, lanes.displayMode, lanes.dropFrame))
                                            .arg(lanes.model.formatDuration(duration, lanes.displayMode))
    }
    function trackIndexFor(y) {
        const index = lanes.viewport.trackIndexAtY(Math.max(0, y))
        return index >= 0 ? index : Math.max(0, lanes.viewport.trackCount - 1)
    }
    function hover(x, y) {
        const hit = lanes.viewport.hitTest(x, y)
        internal.hoverClipId = hit.clipId || ""
        internal.hoverPart = hit.kind
        internal.hoverX = x
        internal.hoverY = y
    }
    function pointerPressed(x, y, modifiers) {
        const hit = lanes.viewport.hitTest(x, y)
        internal.hit = hit
        internal.pressX = x
        internal.pressY = y
        internal.pointerX = x
        internal.pointerY = y
        internal.pressTicks = lanes.viewport.ticksForX(x)
        internal.pressTrackIndex = lanes.viewport.trackIndexAtY(y)
        internal.phase = hit.kind === "none" ? "" : "pending"
        if (hit.clipId) lanes.clipPressed(hit.clipId, hit.trackId, modifiers)
        else if (hit.kind === "track") lanes.emptyPressed(hit.trackId, modifiers)
    }
    function pointerMoved(x, y) {
        internal.pointerX = x
        internal.pointerY = y
        if (internal.phase === "pending") {
            const distance = Qt.styleHints.startDragDistance
            if (Math.abs(x - internal.pressX) < distance && Math.abs(y - internal.pressY) < distance) return
            lanes.startGesture()
        }
        if (!lanes.gestureActive) return
        const edge = Tk.Theme.size.dropEdge
        internal.autoX = x < edge ? -Math.min(1, (edge - x) / edge)
                       : x > lanes.width - edge ? Math.min(1, (x - lanes.width + edge) / edge) : 0
        internal.autoY = internal.kind !== "move" || internal.phase !== "drag" ? 0
                       : y < edge ? -Math.min(1, (edge - y) / edge)
                       : y > lanes.height - edge ? Math.min(1, (y - lanes.height + edge) / edge) : 0
        lanes.updateGesture()
    }
    function startGesture() {
        const hit = internal.hit
        if (!hit.clipId) {
            internal.phase = editRangeItem.beginDrag("create", internal.pressTicks) ? "range" : ""
            return
        }
        if (hit.locked) {
            internal.phase = ""
            lanes.editRejected(qsTr("This clip or its track is locked"))
            return
        }
        const kind = hit.kind === "clip" ? "move" : hit.kind
        const ids = kind === "move" && lanes.selectedMap[hit.clipId] ? lanes.selectedClipIds : [hit.clipId]
        const begun = lanes.model.beginEdit({ kind: kind, clipIds: ids, anchorClipId: hit.clipId,
                                              grabTicks: internal.pressTicks, ripple: lanes.rippleMode,
                                              expectedRevision: lanes.model.revision })
        if (!begun.ok) {
            internal.phase = ""
            lanes.editRejected(begun.error)
            return
        }
        internal.operationId = begun.operationId
        internal.kind = kind
        internal.phase = "drag"
    }
    function updateGesture() {
        const x = Math.max(0, Math.min(lanes.width, internal.pointerX))
        const ticks = lanes.viewport.ticksForX(x)
        if (internal.phase === "range") {
            editRangeItem.updateDrag(ticks)
            return
        }
        if (internal.phase !== "drag") return
        const trackDelta = internal.kind === "move"
                ? lanes.trackIndexFor(Math.min(lanes.height - 1, internal.pointerY)) - internal.pressTrackIndex : 0
        internal.preview = lanes.model.updateEdit(internal.operationId, {
            pointerTicks: ticks, trackDelta: trackDelta, snap: lanes.snapEnabled,
            snapToleranceTicks: lanes.viewport.ticksForPixels(lanes.snapPixels), grid: lanes.snapGrid,
            playheadTicks: lanes.playheadTicks })
    }
    function pointerReleased(modifiers) {
        const phase = internal.phase
        internal.phase = ""
        internal.autoX = 0
        internal.autoY = 0
        if (phase === "drag") {
            const operation = internal.operationId
            internal.operationId = ""
            internal.preview = ({})
            const result = lanes.model.commitEdit(operation)
            if (result.ok) lanes.editCommitted(result.transactionId)
            else lanes.editRejected(result.error)
        } else if (phase === "range") {
            editRangeItem.commitDrag()
        } else if (phase === "pending" && !internal.hit.clipId) {
            lanes.emptyClicked(internal.pressTicks, modifiers)
        }
    }
    // Escape, a lost grab or a replaced model: the document stays unchanged.
    function cancelGesture() {
        if (internal.operationId.length > 0 && lanes.model) lanes.model.cancelEdit(internal.operationId)
        if (internal.phase === "range") editRangeItem.cancelDrag()
        const active = internal.phase === "drag" || internal.phase === "range"
        internal.operationId = ""
        internal.phase = ""
        internal.preview = ({})
        internal.autoX = 0
        internal.autoY = 0
        return active
    }
    // One autoscroll step; the pointer stays put while the content moves under it.
    function autoScrollStep(seconds) {
        if (!lanes.gestureActive || (internal.autoX === 0 && internal.autoY === 0)) return
        const distance = Tk.Theme.size.dropEdge * 30 * seconds
        lanes.viewport.scrollByPixels(internal.autoX * distance, internal.autoY * distance)
        lanes.updateGesture()
    }

    objectName: "timelineLanes"
    clip: true

    QtObject {
        id: internal
        property string phase: ""       // "", pending, drag, range
        property string kind: ""        // move, trimStart, trimEnd
        property string operationId: ""
        property var hit: ({})
        property var preview: ({})
        property real pressX: 0
        property real pressY: 0
        property string pressTicks: "0"
        property int pressTrackIndex: -1
        property real pointerX: 0
        property real pointerY: 0
        property real autoX: 0
        property real autoY: 0
        property string hoverClipId: ""
        property string hoverPart: ""
        property real hoverX: 0
        property real hoverY: 0
    }

    Connections {
        target: lanes.model
        ignoreUnknownSignals: true
        function onEditInvalidated(operationId, reason) {
            if (operationId !== internal.operationId) return
            internal.operationId = ""
            internal.phase = ""
            internal.preview = ({})
            lanes.editRejected(reason)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
    }

    Repeater {
        objectName: "timelineLaneStripes"
        model: lanes.viewport ? lanes.viewport.tracks : null
        delegate: Rectangle {
            required property string trackId
            required property int trackIndex
            required property real trackY
            required property real trackHeight
            objectName: "timelineLane_" + trackId
            y: trackY
            width: lanes.width
            height: trackHeight
            color: trackId === lanes.currentTrackId && lanes.focusActive ? Tk.Theme.color.hover
                 : trackIndex % 2 === 1 ? Tk.Theme.color.panelAlt : Tk.Theme.color.panel
            Rectangle {
                width: parent.width
                height: Tk.Theme.size.border
                y: parent.height - height
                color: Tk.Theme.color.divider
            }
        }
    }

    Repeater {
        id: clipRepeater
        objectName: "timelineClipRepeater"
        model: lanes.viewport ? lanes.viewport.clips : null
        delegate: Tk.ClipItem {
            required property string key
            required clipId
            required trackId
            required label
            required clipColor
            required kind
            required resourceId
            required locked
            required preview
            required ghost
            required invalid
            required summary
            required summaryCount
            required clippedStart
            required clippedEnd
            required startTicks
            required durationTicks
            required visibleSourceStartTicks
            required visibleSourceEndTicks
            required property real clipX
            required property real laneY
            required property real clipWidth
            required property real laneHeight
            required property int stackOrder
            x: clipX
            y: laneY + Tk.Theme.size.border
            // Lane stripes are z 0; ghosts (stackOrder -1) sit above them and
            // below clips, and previewed clips above everything else.
            z: stackOrder + 2
            width: Math.max(Tk.Theme.size.border, clipWidth)
            height: Math.max(Tk.Theme.size.border, laneHeight - 2 * Tk.Theme.size.border)
            selected: !ghost && !summary && lanes.selectedMap[clipId] === true
            current: !ghost && clipId === lanes.currentClipId && lanes.focusActive
            hoverPart: !ghost && clipId === internal.hoverClipId && !lanes.gestureActive ? internal.hoverPart : ""
            timeText: lanes.timeText(startTicks, durationTicks)
            mediaProvider: lanes.mediaProvider
            ticksPerSecondNumerator: lanes.model ? lanes.model.timebaseNumerator : "705600000"
            ticksPerSecondDenominator: lanes.model ? lanes.model.timebaseDenominator : "1"
            onSelectRequested: function(id, modifiers) { lanes.clipPressed(id, trackId, modifiers) }
            onNudgeRequested: function(id, steps) { lanes.nudgeRequested(id, steps) }
            onActivated: function(id) { lanes.clipActivated(id) }
        }
    }

    Tk.TimeSelection {
        id: editRangeItem
        objectName: "timelineEditRange"
        anchors.fill: parent
        z: 200000
        viewport: lanes.viewport
        model: lanes.model
        startTicks: lanes.model && lanes.model.hasEditRange ? lanes.model.editRangeStart : ""
        endTicks: lanes.model && lanes.model.hasEditRange ? lanes.model.editRangeEnd : ""
        snapEnabled: lanes.snapEnabled
        snapGrid: lanes.snapGrid
        snapPixels: lanes.snapPixels
        playheadTicks: lanes.playheadTicks
        displayMode: lanes.displayMode
        dropFrame: lanes.dropFrame
        onRangeCommitted: function(start, end) { lanes.rangeCommitted(start, end) }
        onRangeRejected: function(reason) { lanes.editRejected(reason) }
        onRangeCleared: lanes.rangeCleared()
        onNumericEditRequested: lanes.rangeNumericEditRequested()
    }

    Rectangle {
        objectName: "timelineSnapGuide"
        visible: lanes.gestureActive && !!internal.preview.snapped
        z: 200001
        x: lanes.xOf(internal.preview.snapTarget || "") - width / 2
        width: Tk.Theme.size.border * 2
        height: lanes.height
        color: Tk.Theme.color.accent
    }

    Rectangle {
        objectName: "timelinePlayhead"
        z: 200002
        x: lanes.xOf(lanes.playheadTicks) - width / 2
        width: Tk.Theme.size.border * 2
        height: lanes.height
        color: Tk.Theme.color.danger
    }

    FrameAnimation {
        running: lanes.gestureActive && (internal.autoX !== 0 || internal.autoY !== 0)
        onTriggered: lanes.autoScrollStep(frameTime)
    }

    WheelHandler {
        objectName: "timelineWheel"
        enabled: lanes.enabled && !!lanes.viewport
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function(event) {
            if (event.modifiers & Qt.ControlModifier) {
                lanes.viewport.zoomAround(point.position.x, Math.pow(2, event.angleDelta.y / 240))
            } else if (event.modifiers & Qt.ShiftModifier) {
                lanes.viewport.scrollByPixels(-event.angleDelta.y / 120 * Tk.Theme.size.thumbnail, 0)
            } else {
                lanes.viewport.scrollByPixels(-event.angleDelta.x / 120 * Tk.Theme.size.thumbnail,
                                              -event.angleDelta.y / 120 * Tk.Theme.size.row * 3)
            }
            event.accepted = true
            if (lanes.gestureActive) lanes.updateGesture()
        }
    }

    MouseArea {
        id: pointer
        objectName: "timelinePointer"
        anchors.fill: parent
        enabled: lanes.enabled && !!lanes.model && !!lanes.viewport
        hoverEnabled: true
        preventStealing: lanes.gestureActive
        cursorShape: internal.hoverPart === "trimStart" || internal.hoverPart === "trimEnd"
                     || internal.kind.indexOf("trim") === 0 && lanes.gestureActive ? Qt.SizeHorCursor
                   : lanes.gestureActive && internal.kind === "move" ? Qt.ClosedHandCursor : Qt.ArrowCursor
        onPressed: function(mouse) { lanes.pointerPressed(mouse.x, mouse.y, mouse.modifiers) }
        onPositionChanged: function(mouse) {
            if (pressed) lanes.pointerMoved(mouse.x, mouse.y)
            else lanes.hover(mouse.x, mouse.y)
        }
        onReleased: function(mouse) { lanes.pointerReleased(mouse.modifiers) }
        onCanceled: lanes.cancelGesture()
        onExited: internal.hoverClipId = ""
        onDoubleClicked: function(mouse) {
            const hit = lanes.viewport.hitTest(mouse.x, mouse.y)
            if (hit.clipId) lanes.clipActivated(hit.clipId)
        }
    }

    Tk.ToolTip {
        objectName: "timelineClipToolTip"
        readonly property var clipData: {
            void (lanes.model ? lanes.model.revision : "")
            return lanes.model && internal.hoverClipId.length > 0 ? lanes.model.clipData(internal.hoverClipId) : ({})
        }
        x: internal.hoverX + Tk.Theme.space.md
        y: internal.hoverY + Tk.Theme.space.md
        visible: pointer.containsMouse && internal.hoverClipId.length > 0 && !lanes.gestureActive
        text: clipData.id ? (clipData.label || qsTr("Clip")) + "\n" + lanes.timeText(clipData.start, clipData.duration) : ""
    }
}
