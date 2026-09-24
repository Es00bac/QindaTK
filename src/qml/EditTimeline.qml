// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import "EditTimelineCommands.js" as Commands

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
    property Tk.TimelineClipboard clipboard: fallbackClipboard
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
    readonly property string playheadLabel: {
        if (!timeline.model) return ""
        void timeline.model.revision
        return timeline.model.formatTicks(timeline.effectivePlayheadTicks, timeline.displayMode, timeline.dropFrame)
    }
    readonly property bool dragging: lanesItem.gestureActive || markerLaneItem.dragging
                                     || headerColumn.gestureActive
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
    // Keyboard and assistive commands; each is one applyEdit (see EditTimelineCommands.js).
    function selectClip(id, modifiers) {
        Commands.selectClip(timeline, id, modifiers, Qt.ShiftModifier, Qt.ControlModifier)
    }
    function moveCurrentTrack(delta) { if (timeline.model) Commands.moveCurrentTrack(timeline, delta) }
    function moveCurrentClip(direction, extend) {
        if (timeline.model) Commands.moveCurrentClip(timeline, direction, extend, Qt.ShiftModifier)
    }
    function nudgeSelection(steps, trackDelta) { return Commands.nudgeSelection(timeline, steps, trackDelta) }
    function trimSelection(edge, steps) { return Commands.trimSelection(timeline, edge, steps) }
    function splitAtPlayhead() { return Commands.splitAtPlayhead(timeline) }
    function deleteSelection(ripple) { return Commands.deleteSelection(timeline, ripple) }
    function reorderCurrentTrack(delta) { return Commands.reorderCurrentTrack(timeline, delta) }
    // Copy/cut go through the WP-016 transfer codec; cut is copy plus one delete.
    function copySelection(cut) {
        const ids = Commands.selectionOrCurrent(timeline)
        if (!timeline.model || !timeline.clipboard || ids.length === 0) return false
        if (!timeline.clipboard.copyClips(timeline.model, ids))
            return timeline.report({ ok: false, error: timeline.clipboard.lastError })
        return cut ? timeline.deleteSelection(timeline.rippleMode) : true
    }
    function pasteAtPlayhead() {
        if (!timeline.model || !timeline.clipboard) return false
        const track = timeline.currentTrackId.length > 0 ? timeline.currentTrackId : timeline.model.visibleTrackIds()[0]
        const result = timeline.clipboard.paste(timeline.model, track || "", timeline.effectivePlayheadTicks)
        if (!timeline.report(result)) return false
        timeline.selectionModel.select(result.clipIds[0])
        for (let i = 1; i < result.clipIds.length; ++i) timeline.selectionModel.toggle(result.clipIds[i])
        timeline.setCurrentClip(result.clipIds[0], true)
        return true
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
    function openRangeEditor(selection) {
        if (!timeline.model || !selection || !selection.hasRange) return false
        internal.rangeTarget = selection
        timingAnchor.x = Math.max(0, Math.min(lanesItem.width - Tk.Theme.size.icon, selection.startX))
        timingAnchor.y = 0
        timingAnchor.width = Tk.Theme.size.icon
        timingAnchor.height = Tk.Theme.size.icon
        return timingEditor.openRange(selection.startTicks, selection.endTicks)
    }
    function zoomBy(factor) {
        view.zoomAround(Math.max(0, Math.min(view.viewportWidth, lanesItem.xOf(timeline.effectivePlayheadTicks))), factor)
    }
    function cancelActiveEdit() {
        const lanesCancelled = lanesItem.cancelGesture()
        if (markerLaneItem.dragging) markerLaneItem.cancelDrag()
        return headerColumn.cancelGesture() || lanesCancelled
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
        case Qt.Key_C: if (!ctrl) return; timeline.copySelection(false); break
        case Qt.Key_X: if (!ctrl) return; timeline.copySelection(true); break
        case Qt.Key_V: if (!ctrl) return; timeline.pasteAtPlayhead(); break
        case Qt.Key_Plus: case Qt.Key_Equal: if (!ctrl) return; timeline.zoomBy(2); break
        case Qt.Key_Minus: if (!ctrl) return; timeline.zoomBy(0.5); break
        case Qt.Key_0: if (!ctrl) return; view.fitToContent(); break
        default: return
        }
        event.accepted = true
    }

    Tk.SelectionModel { id: fallbackSelection }
    Tk.TimelineClipboard { id: fallbackClipboard }

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
        property var rangeTarget: null
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
    }

    Tk.TimelineTimingEditor {
        id: timingEditor
        model: timeline.model
        displayMode: timeline.displayMode
        dropFrame: timeline.dropFrame
        rippleMode: timeline.rippleMode
        anchorItem: timingAnchor
        onEdited: function(result) { timeline.report(result) }
        onRangeEdited: function(start, end) {
            if (internal.rangeTarget) internal.rangeTarget.commitRange(start, end)
        }
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
                text: timeline.playheadLabel
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
            // AGENT-CONTRACT: the loop band shows the transport's acknowledged
            // loop (WP-047); a committed drag only requests a new loop.
            Tk.TimeSelection {
                id: loopRange
                objectName: "timelineLoopRange"
                anchors.fill: parent
                kind: "loop"
                allowEmpty: false
                viewport: view
                model: timeline.model
                enabled: !!timeline.transportModel && timeline.transportModel.canLoop
                startTicks: timeline.transportModel && timeline.transportModel.loopEnabled && timeline.model
                            ? timeline.model.samplesToTicksText(timeline.transportModel.loopStartSample) : ""
                endTicks: timeline.transportModel && timeline.transportModel.loopEnabled && timeline.model
                          ? timeline.model.samplesToTicksText(timeline.transportModel.loopEndSample) : ""
                snapEnabled: timeline.snapEnabled
                snapGrid: timeline.snapGrid
                snapPixels: timeline.snapPixels
                playheadTicks: timeline.effectivePlayheadTicks
                displayMode: timeline.displayMode
                dropFrame: timeline.dropFrame
                onRangeCommitted: function(start, end) {
                    timeline.transportModel.requestLoop(true, timeline.model.ticksToSamplesText(start, Tk.TimeMap.Floor),
                                                        timeline.model.ticksToSamplesText(end, Tk.TimeMap.Ceil))
                }
                onRangeRejected: function(reason) { timeline.report({ ok: false, error: reason }) }
                onRangeCleared: timeline.transportModel.requestLoop(false, timeline.transportModel.loopStartSample,
                                                                    timeline.transportModel.loopEndSample)
                onNumericEditRequested: timeline.openRangeEditor(loopRange)
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
        Tk.EditTimelineHeaders {
            id: headerColumn
            Tk.Grid.row: 2
            Tk.Grid.column: 0
            viewport: view
            model: timeline.model
            currentTrackId: timeline.currentTrackId
            onTrackSelected: function(id) {
                timeline.currentTrackId = id
                timeline.forceActiveFocus()
            }
            onEditReported: function(result) { timeline.report(result) }
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
            onRangeNumericEditRequested: timeline.openRangeEditor(lanesItem.editRange)
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
