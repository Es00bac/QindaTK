// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// A virtualized multitrack edit timeline: ruler, marker lane, track headers,
// clip lanes, edit range and scrollbars over one TimelineModel.
//
// AGENT-CONTRACT: TimelineModel owns placement, history and policy;
// TimelineViewport owns only the view (exact origin tick, zoom, scroll) and
// the bounded delegate rows. This control keeps the current track/clip IDs
// and routes pointer, keyboard, numeric and assistive input to the same
// typed model edits, so each accepted gesture is one undo step and a rejected
// or cancelled one leaves the document unchanged. Clip selection lives in a
// SelectionModel by stable ID and survives reorder, moves and scrolling.
T.Control {
    id: timeline

    property Tk.TimelineModel model: null
    property Tk.TimelineMediaProvider mediaProvider: null
    property Tk.SelectionModel selectionModel: fallbackSelection
    property Tk.TransportModel transportModel: null
    property string playheadTicks: "0"
    property bool snapEnabled: true
    property string snapGrid: "frames"
    property bool rippleMode: false
    property string displayMode: "timecode"
    property bool dropFrame: false
    property real headerWidth: Tk.Theme.size.labelWidth * 2
    property real snapPixels: Tk.Theme.space.md
    property string currentTrackId: ""
    property string currentClipId: ""
    property string tooltip: qsTr("Edit timeline")
    property string statusText: ""
    readonly property Tk.TimelineViewport viewport: view
    readonly property Item lanes: lanesItem
    readonly property Item markerLane: markerLaneItem
    readonly property Item editRange: lanesItem.editRange
    readonly property Item ruler: rulerItem
    readonly property var selectedClipIds: timeline.selectionModel ? timeline.selectionModel.selectedIds : []
    readonly property var selectedMap: {
        const map = {}
        for (let i = 0; i < timeline.selectedClipIds.length; ++i) map[timeline.selectedClipIds[i]] = true
        return map
    }
    readonly property string effectivePlayheadTicks: {
        if (!timeline.transportModel || !timeline.model) return timeline.playheadTicks
        void timeline.model.timebaseNumerator
        const ticks = timeline.model.samplesToTicksText(timeline.transportModel.positionSample)
        return ticks.length > 0 ? ticks : timeline.playheadTicks
    }
    readonly property bool dragging: lanesItem.gestureActive || markerLaneItem.dragging
                                     || internal.trackOperation.length > 0
    readonly property real originSeconds: {
        void view.originTicks
        return timeline.model ? timeline.model.ticksToSeconds(view.originTicks) : 0
    }
    readonly property real visibleSeconds: view.viewportWidth / Math.max(1e-9, view.pixelsPerSecond)
    readonly property real rangeStartSeconds: Math.min(0, timeline.originSeconds)
    readonly property real rangeEndSeconds: {
        void (timeline.model ? timeline.model.revision : "")
        const content = timeline.model ? timeline.model.ticksToSeconds(timeline.model.contentEndTicks) : 0
        return Math.max(content + timeline.visibleSeconds / 2, timeline.originSeconds + timeline.visibleSeconds)
    }

    signal seekRequested(string ticks)
    signal clipActivated(string clipId)
    signal editRejected(string reason)
    signal editCommitted(string transactionId)

    function report(result) {
        if (result && result.ok === false) {
            timeline.statusText = result.error || qsTr("The edit was rejected")
            timeline.editRejected(timeline.statusText)
            return false
        }
        if (result && result.transactionId) timeline.editCommitted(result.transactionId)
        timeline.statusText = ""
        return !!result
    }
    function clipRecord(id) {
        return timeline.model && id.length > 0 ? timeline.model.clipData(id) : ({})
    }
    function seek(ticks) {
        if (!timeline.model || ticks.length === 0) return
        if (timeline.transportModel && timeline.transportModel.canSeek)
            timeline.transportModel.requestSeek(timeline.model.ticksToSamplesText(ticks))
        timeline.seekRequested(ticks)
    }
    function setCurrentClip(id, reveal) {
        timeline.currentClipId = id
        if (id.length > 0) timeline.currentTrackId = timeline.clipRecord(id).trackId
        if (reveal && id.length > 0) view.ensureClipVisible(id, Tk.Theme.size.dropEdge)
    }
    function selectClip(id, modifiers) {
        if (!timeline.selectionModel || id.length === 0) return
        const anchor = timeline.selectionModel.anchorId
        if (modifiers & Qt.ControlModifier) {
            timeline.selectionModel.toggle(id)
        } else if ((modifiers & Qt.ShiftModifier) && anchor.length > 0 && timeline.model.hasClip(anchor)) {
            const a = timeline.clipRecord(anchor), b = timeline.clipRecord(id)
            const start = timeline.model.compareTicks(a.start, b.start) <= 0 ? a.start : b.start
            const end = timeline.model.compareTicks(a.end, b.end) >= 0 ? a.end : b.end
            const inside = timeline.model.clipIdsInRect(a.trackId, b.trackId, start, end)
                           .filter(function(other) { return other !== anchor && other !== id })
            timeline.selectionModel.extendTo(id, [anchor].concat(inside, [id]))
        } else {
            timeline.selectionModel.select(id)
        }
        timeline.setCurrentClip(id, false)
    }
    function moveCurrentTrack(delta) {
        if (!timeline.model) return
        const ids = timeline.model.visibleTrackIds()
        if (ids.length === 0) return
        const index = Math.max(0, Math.min(ids.length - 1, ids.indexOf(timeline.currentTrackId) + delta))
        timeline.currentTrackId = ids[index]
        view.ensureTrackVisible(timeline.currentTrackId)
        const reference = timeline.currentClipId.length > 0 ? timeline.clipRecord(timeline.currentClipId).start
                                                            : timeline.effectivePlayheadTicks
        const next = timeline.model.nearestClip(timeline.currentTrackId, reference)
        if (next.length > 0) {
            timeline.selectionModel.select(next)
            timeline.setCurrentClip(next, true)
        } else {
            timeline.currentClipId = ""
        }
    }
    function moveCurrentClip(direction, extend) {
        if (!timeline.model) return
        if (timeline.currentTrackId.length === 0) timeline.currentTrackId = timeline.model.visibleTrackIds()[0] || ""
        const next = timeline.currentClipId.length > 0
                   ? timeline.model.adjacentClip(timeline.currentClipId, direction)
                   : timeline.model.edgeClip(timeline.currentTrackId, direction < 0)
        if (next.length === 0) return
        timeline.selectClip(next, extend ? Qt.ShiftModifier : Qt.NoModifier)
        timeline.setCurrentClip(next, true)
    }
    function selectionOrCurrent() {
        if (timeline.selectedClipIds.length > 0) return timeline.selectedClipIds
        return timeline.currentClipId.length > 0 ? [timeline.currentClipId] : []
    }
    function anchorOf(ids) {
        return ids.indexOf(timeline.currentClipId) >= 0 ? timeline.currentClipId : ids[0]
    }
    function stepGrid() {
        return timeline.snapGrid === "none" ? "frames" : timeline.snapGrid
    }
    function nudgeSelection(steps, trackDelta) {
        const ids = timeline.selectionOrCurrent()
        if (!timeline.model || ids.length === 0) return false
        const anchor = timeline.anchorOf(ids)
        const start = timeline.clipRecord(anchor).start
        const target = steps === 0 ? start : timeline.model.stepTicks(start, timeline.stepGrid(), steps)
        const ok = timeline.report(timeline.model.applyEdit(
            { kind: "move", clipIds: ids, anchorClipId: anchor, grabTicks: start, ripple: timeline.rippleMode },
            { pointerTicks: target, trackDelta: trackDelta || 0, snap: false }))
        if (ok) timeline.setCurrentClip(anchor, true)
        return ok
    }
    function trimSelection(edge, steps) {
        const ids = timeline.selectionOrCurrent()
        if (!timeline.model || ids.length === 0) return false
        const anchor = timeline.anchorOf(ids)
        const from = edge === "start" ? timeline.clipRecord(anchor).start : timeline.clipRecord(anchor).end
        return timeline.report(timeline.model.applyEdit(
            { kind: edge === "start" ? "trimStart" : "trimEnd", clipIds: ids, anchorClipId: anchor,
              grabTicks: from, ripple: timeline.rippleMode },
            { pointerTicks: timeline.model.stepTicks(from, timeline.stepGrid(), steps), snap: false }))
    }
    function splitAtPlayhead() {
        const ids = timeline.selectionOrCurrent()
        if (!timeline.model || ids.length === 0) return false
        return timeline.report(timeline.model.applyEdit({ kind: "split", clipIds: ids },
                                                        { pointerTicks: timeline.effectivePlayheadTicks, snap: false }))
    }
    function deleteSelection(ripple) {
        const ids = timeline.selectionOrCurrent()
        if (!timeline.model || ids.length === 0) return false
        const next = timeline.model.adjacentClip(timeline.currentClipId, 1)
        if (!timeline.report(timeline.model.applyEdit({ kind: "delete", clipIds: ids, ripple: ripple }))) return false
        if (next.length > 0 && timeline.model.hasClip(next)) timeline.selectClip(next, Qt.NoModifier)
        return true
    }
    function reorderCurrentTrack(delta) {
        if (!timeline.model || timeline.currentTrackId.length === 0) return false
        const ids = timeline.model.trackIds()
        const index = ids.indexOf(timeline.currentTrackId)
        const target = index + (delta > 0 ? 2 : -1)
        if (index < 0 || target < 0 || (delta > 0 && index + 1 >= ids.length)) return false
        const ok = timeline.report(timeline.model.applyEdit({ kind: "reorderTrack", trackId: timeline.currentTrackId,
                                                              beforeTrackId: target < ids.length ? ids[target] : "" }))
        view.ensureTrackVisible(timeline.currentTrackId)
        return ok
    }
    function requestTrackProperty(trackId, property, value) {
        return timeline.report(timeline.model.applyEdit({ kind: "trackProperty", trackId: trackId,
                                                          property: property, value: value }))
    }
    function openTimingEditor(id) {
        if (!timeline.model || id.length === 0) return false
        timeline.setCurrentClip(id, true)
        const geometry = view.clipGeometry(id)
        timingAnchor.x = Math.max(0, Math.min(lanesItem.width - Tk.Theme.size.icon, geometry.x || 0))
        timingAnchor.y = Math.max(0, Math.min(lanesItem.height - Tk.Theme.size.icon, geometry.y || 0))
        timingAnchor.width = Math.max(Tk.Theme.size.icon, Math.min(lanesItem.width - timingAnchor.x, geometry.width || 0))
        timingAnchor.height = Math.max(Tk.Theme.size.icon, geometry.height || 0)
        return timingEditor.openClip(id)
    }
    function zoomBy(factor) {
        view.zoomAround(Math.max(0, Math.min(view.viewportWidth, lanesItem.xOf(timeline.effectivePlayheadTicks))), factor)
    }
    function cancelActiveEdit() {
        const lanesCancelled = lanesItem.cancelGesture()
        if (markerLaneItem.dragging) markerLaneItem.cancelDrag()
        if (internal.trackOperation.length > 0) timeline.model.cancelEdit(internal.trackOperation)
        internal.trackOperation = ""
        return lanesCancelled
    }
    function beginTrackGesture(kind, trackId) {
        const begun = timeline.model.beginEdit({ kind: kind, trackId: trackId, expectedRevision: timeline.model.revision })
        internal.trackOperation = begun.ok ? begun.operationId : ""
        internal.trackHeightAtStart = view.trackHeight(trackId)
        internal.slots = []
        for (let i = 0; i < view.trackCount; ++i) {
            const id = view.trackIdAt(i)
            internal.slots.push({ id: id, top: view.trackY(id) + view.scrollY, height: view.trackHeight(id) })
        }
        if (!begun.ok) timeline.report(begun)
    }
    function reorderTarget(trackId, y) {
        const content = y + view.scrollY
        for (let i = 0; i < internal.slots.length; ++i) {
            const slot = internal.slots[i]
            if (content < slot.top + slot.height / 2) return slot.id === trackId ? "" : slot.id
        }
        return ""
    }
    function finishTrackGesture() {
        const operation = internal.trackOperation
        internal.trackOperation = ""
        if (operation.length > 0) timeline.report(timeline.model.commitEdit(operation))
    }

    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.panelMinHeight * 3
    activeFocusOnTab: timeline.enabled
    focusPolicy: Qt.StrongFocus
    clip: true

    Accessible.role: Accessible.Pane
    Accessible.name: timeline.tooltip
    Accessible.description: {
        void (timeline.model ? timeline.model.revision : "")
        const track = timeline.model && timeline.currentTrackId.length > 0 ? timeline.model.trackData(timeline.currentTrackId) : ({})
        const item = timeline.clipRecord(timeline.currentClipId)
        return [track.name ? qsTr("Track %1").arg(track.name) : qsTr("No track"),
                item.id ? qsTr("clip %1, %2").arg(item.label || qsTr("Clip")).arg(lanesItem.timeText(item.start, item.duration)) : "",
                qsTr("%n clips selected", "", timeline.selectedClipIds.length),
                qsTr("Arrows move between tracks and clips; Alt+arrows move clips; Enter edits timing")]
               .filter(function(part) { return part.length > 0 }).join(". ")
    }
    Accessible.focusable: timeline.enabled
    Accessible.focused: timeline.activeFocus

    Keys.onPressed: function(event) {
        if (!timeline.enabled || !timeline.model) return
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const steps = shift && !ctrl ? 10 : 1
        switch (event.key) {
        case Qt.Key_Left: case Qt.Key_Right: {
            const direction = event.key === Qt.Key_Left ? -1 : 1
            if (alt && shift) timeline.trimSelection("end", direction)
            else if (ctrl && shift) timeline.trimSelection("start", direction)
            else if (alt) timeline.nudgeSelection(direction * steps, 0)
            else timeline.moveCurrentClip(direction, shift)
            break
        }
        case Qt.Key_Up: case Qt.Key_Down: {
            const direction = event.key === Qt.Key_Up ? -1 : 1
            if (alt) timeline.nudgeSelection(0, direction)
            else if (ctrl) timeline.reorderCurrentTrack(direction)
            else timeline.moveCurrentTrack(direction)
            break
        }
        case Qt.Key_Home: case Qt.Key_End:
            timeline.currentClipId = ""
            timeline.moveCurrentClip(event.key === Qt.Key_Home ? 1 : -1, false)
            break
        case Qt.Key_S: if (ctrl || alt) return; timeline.splitAtPlayhead(); break
        case Qt.Key_Delete: case Qt.Key_Backspace: timeline.deleteSelection(shift); break
        case Qt.Key_M: markerLaneItem.addAt(timeline.effectivePlayheadTicks); break
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_F2: timeline.openTimingEditor(timeline.currentClipId); break
        case Qt.Key_Escape:
            if (!timeline.cancelActiveEdit()) timeline.selectionModel.clear()
            break
        case Qt.Key_Z:
            if (!ctrl || timeline.model.externalUndo) return
            if (shift) timeline.model.redo(); else timeline.model.undo()
            break
        case Qt.Key_Y: if (!ctrl || timeline.model.externalUndo) return; timeline.model.redo(); break
        case Qt.Key_Plus: case Qt.Key_Equal: if (!ctrl) return; timeline.zoomBy(2); break
        case Qt.Key_Minus: if (!ctrl) return; timeline.zoomBy(0.5); break
        case Qt.Key_0: if (!ctrl) return; view.fitToContent(); break
        default: return
        }
        event.accepted = true
    }

    Tk.SelectionModel { id: fallbackSelection }

    Tk.TimelineViewport {
        id: view
        model: timeline.model
        viewportWidth: lanesItem.width
        viewportHeight: lanesItem.height
        defaultTrackHeight: Tk.Theme.size.rowLg + Tk.Theme.size.waveformHeight
        minimumTrackHeight: Tk.Theme.size.row
        overscanPixels: Tk.Theme.size.thumbnail
        summaryPixels: Tk.Theme.space.xs
        handlePixels: Tk.Theme.size.seamTarget / 2
    }

    QtObject {
        id: internal
        property string trackOperation: ""
        property real trackHeightAtStart: 0
        property var slots: []
    }

    Connections {
        target: timeline.model
        ignoreUnknownSignals: true
        function onClipsRemoved(ids) {
            if (timeline.selectionModel) timeline.selectionModel.removeIds(ids)
            if (ids.indexOf(timeline.currentClipId) >= 0) timeline.currentClipId = ""
        }
        function onContentReset() {
            if (timeline.selectionModel)
                timeline.selectionModel.resetModel(timeline.model.allClipIds(), timeline.model.resetGeneration)
            if (!timeline.model.hasClip(timeline.currentClipId)) timeline.currentClipId = ""
            if (timeline.model.trackIds().indexOf(timeline.currentTrackId) < 0) timeline.currentTrackId = ""
        }
        function onEditInvalidated(operationId, reason) {
            if (operationId !== internal.trackOperation) return
            internal.trackOperation = ""
            timeline.report({ ok: false, error: reason })
        }
    }

    Tk.TimelineTimingEditor {
        id: timingEditor
        model: timeline.model
        displayMode: timeline.displayMode
        dropFrame: timeline.dropFrame
        rippleMode: timeline.rippleMode
        anchorItem: timingAnchor
        onEdited: function(result) { timeline.report(result) }
        onClosed: timeline.forceActiveFocus()
    }

    contentItem: Tk.Grid {
        columns: timeline.headerWidth + "px 1fr " + Tk.Theme.size.scrollbar + "px"
        rows: Tk.Theme.size.timeRuler + "px " + Tk.Theme.size.row + "px 1fr " + Tk.Theme.size.scrollbar + "px"

        Rectangle {
            objectName: "timelineCorner"
            Tk.Grid.row: 0
            Tk.Grid.column: 0
            Tk.Grid.rowSpan: 2
            color: Tk.Theme.color.headerBg
            Tk.Mono {
                objectName: "timelinePlayheadReadout"
                anchors.centerIn: parent
                text: timeline.model ? (void timeline.model.revision,
                      timeline.model.formatTicks(timeline.effectivePlayheadTicks, timeline.displayMode, timeline.dropFrame)) : ""
                color: Tk.Theme.color.text
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Playhead %1").arg(text)
            }
        }
        Tk.TimeRuler {
            id: rulerItem
            objectName: "timelineRuler"
            Tk.Grid.row: 0
            Tk.Grid.column: 1
            pixelsPerUnit: view.pixelsPerSecond
            originUnits: timeline.originSeconds
            playheadUnits: timeline.model ? timeline.model.ticksToSeconds(timeline.effectivePlayheadTicks) : -1
            // Ruler ticks fall on whole seconds; label them with the nearest
            // frame so 2 s at 29.97 fps reads 00:00:02:00, not frame 59.
            formatUnit: function(seconds) {
                if (!timeline.model) return ""
                const ticks = timeline.model.secondsToTicks(seconds)
                if (timeline.displayMode !== "timecode" && timeline.displayMode !== "frames")
                    return timeline.model.formatTicks(ticks, timeline.displayMode, timeline.dropFrame)
                const frame = timeline.model.ticksToFramesText(ticks, Tk.TimeMap.NearestAway)
                if (timeline.displayMode === "frames") return frame
                const label = timeline.model.timeMap.formatTimecode(frame, timeline.dropFrame)
                return label.ok ? label.value : ""
            }
            tooltip: qsTr("Timeline ruler")
            onScrubbed: function(units) {
                timeline.seek(view.ticksForX((units - timeline.originSeconds) * view.pixelsPerSecond))
            }
        }
        Tk.MarkerLane {
            id: markerLaneItem
            Tk.Grid.row: 1
            Tk.Grid.column: 1
            viewport: view
            model: timeline.model
            snapEnabled: timeline.snapEnabled
            snapGrid: timeline.snapGrid
            snapPixels: timeline.snapPixels
            playheadTicks: timeline.effectivePlayheadTicks
            displayMode: timeline.displayMode
            dropFrame: timeline.dropFrame
            onEditRejected: function(reason) { timeline.report({ ok: false, error: reason }) }
            onMarkerActivated: function(id) { timeline.seek(timeline.model.markerData(id).position) }
        }
        Item {
            id: headerColumn
            objectName: "timelineTrackHeaders"
            Tk.Grid.row: 2
            Tk.Grid.column: 0
            clip: true
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Tracks")
            Rectangle { anchors.fill: parent; color: Tk.Theme.color.headerBg }
            Repeater {
                model: view.tracks
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
                    width: headerColumn.width
                    height: trackHeight
                    current: trackId === timeline.currentTrackId
                    onSelectRequested: function(id) { timeline.currentTrackId = id; timeline.forceActiveFocus() }
                    onPropertyRequested: function(id, property, value) { timeline.requestTrackProperty(id, property, value) }
                    onRenameRequested: function(id, text) { timeline.requestTrackProperty(id, "name", text) }
                    onHeightStepRequested: function(id, steps) {
                        timeline.requestTrackProperty(id, "height", Math.round(view.trackHeight(id) + steps * Tk.Theme.space.lg))
                    }
                    onResizeStarted: function(id) { timeline.beginTrackGesture("resizeTrack", id) }
                    onResizeMoved: function(id, delta) {
                        if (internal.trackOperation.length > 0)
                            timeline.model.updateEdit(internal.trackOperation, { trackHeight: Math.round(Math.max(
                                Tk.Theme.size.row, internal.trackHeightAtStart + delta)) })
                    }
                    onResizeFinished: timeline.finishTrackGesture()
                    onResizeCanceled: timeline.cancelActiveEdit()
                    onReorderStarted: function(id, y) { timeline.beginTrackGesture("reorderTrack", id) }
                    onReorderMoved: function(id, y) {
                        if (internal.trackOperation.length > 0)
                            timeline.model.updateEdit(internal.trackOperation, { beforeTrackId: timeline.reorderTarget(id, y) })
                    }
                    onReorderFinished: timeline.finishTrackGesture()
                    onReorderCanceled: timeline.cancelActiveEdit()
                }
            }
        }
        Tk.EditTimelineLanes {
            id: lanesItem
            Tk.Grid.row: 2
            Tk.Grid.column: 1
            viewport: view
            model: timeline.model
            mediaProvider: timeline.mediaProvider
            selectedMap: timeline.selectedMap
            selectedClipIds: timeline.selectedClipIds
            currentClipId: timeline.currentClipId
            currentTrackId: timeline.currentTrackId
            focusActive: timeline.activeFocus
            playheadTicks: timeline.effectivePlayheadTicks
            snapEnabled: timeline.snapEnabled
            snapGrid: timeline.snapGrid
            snapPixels: timeline.snapPixels
            rippleMode: timeline.rippleMode
            displayMode: timeline.displayMode
            dropFrame: timeline.dropFrame
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Clips")
            onClipPressed: function(id, trackId, modifiers) {
                timeline.forceActiveFocus()
                if (!(modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) && timeline.selectedMap[id]) timeline.setCurrentClip(id, false)
                else timeline.selectClip(id, modifiers)
            }
            onEmptyPressed: function(trackId, modifiers) {
                timeline.forceActiveFocus()
                timeline.currentTrackId = trackId
            }
            onEmptyClicked: function(ticks, modifiers) {
                if (!(modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) timeline.selectionModel.clear()
                timeline.seek(ticks)
            }
            onClipActivated: function(id) { timeline.clipActivated(id); timeline.openTimingEditor(id) }
            onNudgeRequested: function(id, steps) { timeline.selectClip(id, Qt.NoModifier); timeline.nudgeSelection(steps, 0) }
            onEditRejected: function(reason) { timeline.report({ ok: false, error: reason }) }
            onEditCommitted: function(transactionId) { timeline.report({ ok: true, transactionId: transactionId }) }
            onRangeCommitted: function(start, end) {
                if (!timeline.model.setEditRange(start, end)) timeline.report({ ok: false, error: timeline.model.lastError })
            }
            onRangeCleared: timeline.model.clearEditRange()
            Item {
                id: timingAnchor
                objectName: "timelineTimingAnchor"
            }
            Rectangle {
                objectName: "timelineFocusRing"
                anchors.fill: parent
                z: 300000
                visible: timeline.activeFocus && timeline.currentClipId.length === 0
                color: "transparent"
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.focus
            }
        }
        Tk.ScrollBar {
            id: verticalBar
            objectName: "timelineVerticalScrollBar"
            Tk.Grid.row: 2
            Tk.Grid.column: 2
            orientation: Qt.Vertical
            size: view.contentHeight > 0 ? Math.min(1, view.viewportHeight / view.contentHeight) : 1
            onPositionChanged: if (pressed) view.scrollY = position * view.contentHeight
            Binding on position {
                value: view.contentHeight > 0 ? view.scrollY / view.contentHeight : 0
                when: !verticalBar.pressed
                restoreMode: Binding.RestoreNone
            }
        }
        Tk.ScrollBar {
            id: horizontalBar
            objectName: "timelineHorizontalScrollBar"
            Tk.Grid.row: 3
            Tk.Grid.column: 1
            orientation: Qt.Horizontal
            size: Math.min(1, timeline.visibleSeconds / Math.max(1e-9, timeline.rangeEndSeconds - timeline.rangeStartSeconds))
            onPositionChanged: {
                if (!pressed || !timeline.model) return
                const span = timeline.rangeEndSeconds - timeline.rangeStartSeconds
                view.originTicks = timeline.model.secondsToTicks(timeline.rangeStartSeconds + position * span)
            }
            Binding on position {
                value: (timeline.originSeconds - timeline.rangeStartSeconds)
                       / Math.max(1e-9, timeline.rangeEndSeconds - timeline.rangeStartSeconds)
                when: !horizontalBar.pressed
                restoreMode: Binding.RestoreNone
            }
        }
    }
}
