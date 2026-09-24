// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Tempo and meter events above timeline tracks: a meter band with the bar
// grid and meter changes, and a tempo band with the tempo step graph.
//
// AGENT-CONTRACT: events are TempoMapModel records with stable IDs and exact
// musical ticks; x comes from the shared TimelineViewport through the
// TimelineModel's TimeMap, so the lane scrolls and zooms with the timeline.
// Every change is a model transaction: a drag is one session (release
// commits one undo step; Escape, a lost grab or a replaced model cancels and
// restores), keyboard and numeric entry apply one edit each. A horizontal
// drag moves an event on the beat/bar grid; a vertical drag on a tempo event
// or its segment changes the tempo. Keyboard: Left/Right choose an event,
// Alt+Left/Right move it a beat (Shift: a bar; meter changes move by bars),
// Up/Down change tempo by 1 BPM (Shift: 10) or the meter's beats, Delete
// removes it, Return/F2 opens numeric entry, T adds a tempo change and M a
// meter change at the playhead.
Item {
    id: lane

    property Tk.TimelineViewport viewport: null
    property Tk.TempoMapModel tempoModel: null
    property string currentEventId: ""
    property bool snapEnabled: true
    property string snapGrid: "beat" // "beat", "bar" or "none"
    property string playheadTicks: "0"
    property real meterBandHeight: Tk.Theme.size.row
    property string tooltip: qsTr("Tempo and meter")
    readonly property bool dragging: internal.operationId.length > 0
    readonly property var rows: lane.tempoModel ? lane.tempoModel.events : []
    readonly property var tempoRows: lane.rows.filter(function(row) { return row.kind === "tempo" })
    readonly property var meterRows: lane.rows.filter(function(row) { return row.kind === "meter" })
    readonly property var currentEvent: {
        for (let i = 0; i < lane.rows.length; ++i)
            if (lane.rows[i].id === lane.currentEventId) return lane.rows[i]
        return ({})
    }
    readonly property real lowBpm: internal.frozen ? internal.frozenLow : lane.displayRange()[0]
    readonly property real highBpm: internal.frozen ? internal.frozenHigh : lane.displayRange()[1]
    readonly property int viewRevision: internal.viewRevision

    signal eventActivated(string eventId)
    signal editRejected(string reason)

    function report(result) {
        if (result && result.ok === false) lane.editRejected(result.error || qsTr("The tempo edit was rejected"))
        return !!result && result.ok !== false
    }
    function timeline() { return lane.viewport ? lane.viewport.model : null }
    // Bindings pass lane.viewRevision so they re-evaluate on scroll, zoom
    // and every tempo change (which moves converted positions).
    function xAt(revision, row) { return lane.xOf(row) }
    function xOf(row) {
        if (!lane.viewport || !row || row.tick === undefined) return 0
        const ticks = row.positionTicks && row.positionTicks.length > 0 ? row.positionTicks
                    : (lane.timeline() ? lane.timeline().ticksAtMusicalTick(row.tick) : "")
        return ticks.length > 0 ? lane.viewport.xForTicks(ticks) : 0
    }
    function playheadX(revision, ticks) { return lane.viewport ? lane.viewport.xForTicks(ticks) : 0 }
    function displayRange() {
        let low = Infinity, high = -Infinity
        for (let i = 0; i < lane.tempoRows.length; ++i) {
            low = Math.min(low, lane.tempoRows[i].bpm)
            high = Math.max(high, lane.tempoRows[i].bpm)
        }
        if (!isFinite(low)) return [60, 180]
        const pad = Math.max((high - low) * 0.2, high * 0.1)
        return [Math.max(1, low - pad), high + pad]
    }
    function yForBpm(bpm) {
        const top = lane.meterBandHeight + Tk.Theme.space.sm
        const span = Math.max(1, lane.height - top - Tk.Theme.space.sm)
        return top + (1 - (bpm - lane.lowBpm) / Math.max(1e-9, lane.highBpm - lane.lowBpm)) * span
    }
    function bpmAtY(y) {
        const top = lane.meterBandHeight + Tk.Theme.space.sm
        const span = Math.max(1, lane.height - top - Tk.Theme.space.sm)
        return lane.lowBpm + (1 - (y - top) / span) * (lane.highBpm - lane.lowBpm)
    }
    function musicalAt(x) {
        return lane.timeline() ? lane.timeline().musicalTickAt(lane.viewport.ticksForX(x)) : ""
    }
    function hitEvent(px, py) {
        const list = py < lane.meterBandHeight ? lane.meterRows : lane.tempoRows
        let best = "", bestDistance = Tk.Theme.size.icon + 1
        for (let i = 0; i < list.length; ++i) {
            const distance = Math.abs(px - lane.xOf(list[i]))
            if (distance <= bestDistance) { best = list[i].id; bestDistance = distance }
        }
        return best
    }
    function segmentAt(px) {
        let found = ""
        for (let i = 0; i < lane.tempoRows.length; ++i)
            if (lane.xOf(lane.tempoRows[i]) <= px) found = lane.tempoRows[i].id
        return found
    }
    function select(id) {
        lane.currentEventId = id
        const row = lane.currentEvent
        if (lane.viewport && row.positionTicks) lane.viewport.ensureTicksVisible(row.positionTicks, Tk.Theme.size.dropEdge)
    }
    function step(direction) {
        if (!lane.tempoModel) return
        const next = lane.tempoModel.adjacentEvent(lane.currentEventId, direction)
        if (next.length > 0) lane.select(next)
    }
    function moveCurrent(steps, bars) {
        const row = lane.currentEvent
        if (!lane.tempoModel || !row.id || row.initial) return false
        const meter = row.kind === "meter"
        const tick = lane.tempoModel.stepTick(row.tick, meter || bars ? "bar" : "beat", steps, meter ? row.id : "")
        return lane.report(lane.tempoModel.applyEdit({ kind: "move", eventId: row.id }, { tick: tick }))
    }
    function changeValue(delta) {
        const row = lane.currentEvent
        if (!lane.tempoModel || !row.id) return false
        if (row.kind === "meter")
            return lane.report(lane.tempoModel.applyEdit({ kind: "edit", eventId: row.id },
                                                         { numerator: Math.max(1, row.numerator + delta) }))
        return lane.report(lane.tempoModel.applyEdit({ kind: "edit", eventId: row.id },
                                                     { bpm: Math.round((row.bpm + delta) * 1000) / 1000 }))
    }
    function addAt(kind, timelineTicks) {
        if (!lane.tempoModel || !lane.timeline()) return false
        const musical = lane.timeline().musicalTickAt(timelineTicks)
        if (musical.length === 0) return false
        const grid = kind === "meter" ? "bar" : (lane.snapEnabled ? lane.snapGrid : "none")
        let tick = lane.tempoModel.snapTick(musical, grid)
        if (lane.timeline().compareTicks(tick, "0") < 0) tick = "0"
        const result = lane.tempoModel.applyEdit({ kind: kind === "meter" ? "addMeter" : "addTempo", tick: tick })
        if (!lane.report(result)) return false
        if (result.createdIds.length > 0) lane.currentEventId = result.createdIds[0]
        return true
    }
    function removeCurrent() {
        const row = lane.currentEvent
        if (!lane.tempoModel || !row.id || row.initial) return false
        const next = lane.tempoModel.adjacentEvent(row.id, -1)
        if (!lane.report(lane.tempoModel.applyEdit({ kind: "remove", eventId: row.id }))) return false
        lane.currentEventId = next
        return true
    }
    function openEditor() {
        if (lane.currentEventId.length > 0) numericEditor.openEvent(lane.currentEventId)
    }
    // Also ends the press: later moves of the same press start nothing.
    function cancelDrag() {
        if (internal.operationId.length > 0 && internal.sessionModel)
            internal.sessionModel.cancelEdit(internal.operationId)
        internal.operationId = ""
        internal.sessionModel = null
        internal.pressEvent = ""
        internal.mode = ""
        internal.frozen = false
    }
    function beginDrag(x, y) {
        const row = lane.rows.find(function(r) { return r.id === internal.pressEvent })
        if (!row) return false
        let mode = row.kind === "meter" ? "move"
                 : internal.onMarker && Math.abs(x - internal.pressX) >= Math.abs(y - internal.pressY) ? "move" : "value"
        if (mode === "move" && row.initial) mode = row.kind === "tempo" ? "value" : ""
        if (mode.length === 0) return false
        const begun = lane.tempoModel.beginEdit({ kind: mode === "move" ? "move" : "edit", eventId: row.id,
                                                  expectedRevision: lane.tempoModel.revision })
        if (!lane.report(begun)) return false
        internal.operationId = begun.operationId
        internal.sessionModel = lane.tempoModel
        internal.mode = mode
        internal.grabTick = row.tick
        internal.grabBpm = row.bpm
        internal.kind = row.kind
        internal.frozenLow = lane.lowBpm
        internal.frozenHigh = lane.highBpm
        internal.frozen = mode === "value"
        internal.pressMusical = lane.musicalAt(internal.pressX)
        internal.pressBpm = lane.bpmAtY(internal.pressY)
        return true
    }
    function updateDrag(x, y) {
        const model = internal.sessionModel
        if (internal.mode === "move") {
            const pointer = lane.musicalAt(Math.max(0, Math.min(lane.width, x)))
            const timeline = lane.timeline()
            if (pointer.length === 0 || internal.pressMusical.length === 0) return
            let tick = timeline.addTicks(internal.grabTick, timeline.subtractTicks(pointer, internal.pressMusical))
            if (timeline.compareTicks(tick, "0") <= 0) tick = "1"
            const meter = internal.kind === "meter"
            tick = model.snapTick(tick, meter ? "bar" : (lane.snapEnabled ? lane.snapGrid : "none"),
                                  meter ? internal.pressEvent : "")
            model.updateEdit(internal.operationId, { tick: tick })
        } else {
            const bpm = internal.grabBpm + lane.bpmAtY(y) - internal.pressBpm
            model.updateEdit(internal.operationId, { bpm: Math.round(bpm * 100) / 100 })
        }
    }

    objectName: "tempoLane"
    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.row * 3
    clip: true
    activeFocusOnTab: lane.enabled
    Accessible.role: Accessible.List
    Accessible.name: lane.tooltip
    Accessible.description: (lane.currentEvent.label ? lane.currentEvent.label + ". " : qsTr("No event selected. "))
                            + qsTr("Left and Right choose an event; Alt+Left and Alt+Right move it; Up and Down change it; Delete removes it; Return edits it numerically; T adds a tempo change and M a meter change at the playhead.")
    Accessible.focusable: lane.enabled
    Accessible.focused: lane.activeFocus

    onTempoModelChanged: lane.cancelDrag()
    Component.onDestruction: lane.cancelDrag()

    Keys.onPressed: function(event) {
        if (!lane.enabled || !lane.tempoModel) return
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        switch (event.key) {
        case Qt.Key_Left: if (alt) lane.moveCurrent(-1, shift); else lane.step(-1); break
        case Qt.Key_Right: if (alt) lane.moveCurrent(1, shift); else lane.step(1); break
        case Qt.Key_Up: lane.changeValue(shift ? 10 : 1); break
        case Qt.Key_Down: lane.changeValue(shift ? -10 : -1); break
        case Qt.Key_Delete: case Qt.Key_Backspace: lane.removeCurrent(); break
        case Qt.Key_F2: case Qt.Key_Return: case Qt.Key_Enter: lane.openEditor(); break
        case Qt.Key_T: lane.addAt("tempo", lane.playheadTicks); break
        case Qt.Key_M: lane.addAt("meter", lane.playheadTicks); break
        case Qt.Key_Escape: if (!lane.dragging) return; lane.cancelDrag(); break
        default: return
        }
        event.accepted = true
    }

    QtObject {
        id: internal
        property string operationId: ""
        property Tk.TempoMapModel sessionModel: null
        property string pressEvent: ""
        property bool onMarker: false
        property string mode: ""
        property string kind: ""
        property real pressX: 0
        property real pressY: 0
        property string pressMusical: ""
        property real pressBpm: 0
        property string grabTick: "0"
        property real grabBpm: 0
        property bool frozen: false
        property real frozenLow: 60
        property real frozenHigh: 180
        property int viewRevision: 0
    }
    Connections {
        target: lane.viewport
        ignoreUnknownSignals: true
        function onViewChanged() { internal.viewRevision++ }
    }
    Connections {
        target: lane.tempoModel
        ignoreUnknownSignals: true
        function onEventsChanged() { internal.viewRevision++ }
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.surface
    }
    Rectangle {
        objectName: "tempoMeterBand"
        width: parent.width
        height: lane.meterBandHeight
        color: Tk.Theme.color.panelAlt
    }
    Rectangle { y: lane.meterBandHeight; width: parent.width; height: Tk.Theme.size.border; color: Tk.Theme.color.divider }
    Rectangle { y: parent.height - height; width: parent.width; height: Tk.Theme.size.border; color: Tk.Theme.color.divider }

    Canvas {
        id: barGrid
        objectName: "tempoBarGrid"
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const model = lane.tempoModel, timeline = lane.timeline()
            if (!model || !timeline || !lane.viewport || width <= 0) return
            const start = timeline.musicalTickAt(lane.viewport.ticksForX(0))
            if (start.length === 0) return
            let bar = model.snapTick(start, "bar")
            if (timeline.compareTicks(bar, start) > 0) bar = model.stepTick(bar, "bar", -1)
            ctx.font = Tk.Theme.font.caption + "px \"" + Tk.Theme.font.family + "\""
            ctx.textBaseline = "middle"
            let lastLabel = -Infinity
            const meterTicks = {}
            for (let m = 0; m < lane.meterRows.length; ++m) meterTicks[lane.meterRows[m].tick] = true
            for (let i = 0; i < 1024; ++i) {
                const x = lane.viewport.xForTicks(timeline.ticksAtMusicalTick(bar))
                const next = model.stepTick(bar, "bar", 1)
                const nextX = lane.viewport.xForTicks(timeline.ticksAtMusicalTick(next))
                if (x > width) break
                if (nextX - x >= Tk.Theme.size.controlLg * 2) {
                    ctx.fillStyle = Tk.Theme.color.border
                    for (let beat = model.stepTick(bar, "beat", 1); timeline.compareTicks(beat, next) < 0;
                         beat = model.stepTick(beat, "beat", 1)) {
                        const bx = lane.viewport.xForTicks(timeline.ticksAtMusicalTick(beat))
                        ctx.fillRect(Math.round(bx), lane.meterBandHeight / 2, Tk.Theme.size.border, lane.meterBandHeight / 2)
                    }
                }
                if (nextX - x >= Tk.Theme.space.sm || x - lastLabel >= Tk.Theme.space.sm) {
                    ctx.fillStyle = Tk.Theme.color.divider
                    ctx.fillRect(Math.round(x), 0, Tk.Theme.size.border, height)
                }
                // A meter change labels its own bar line.
                if (!meterTicks[bar] && x - lastLabel >= Tk.Theme.size.controlLg + Tk.Theme.space.md
                    && x >= -Tk.Theme.size.controlLg) {
                    ctx.fillStyle = Tk.Theme.color.textMuted
                    ctx.fillText(model.formatPosition(bar).split(".")[0], Math.round(x) + Tk.Theme.space.xs,
                                 lane.meterBandHeight / 2)
                    lastLabel = x
                }
                bar = next
            }
        }
        Connections { target: lane; function onViewRevisionChanged() { barGrid.requestPaint() } }
        onWidthChanged: requestPaint()
    }

    Canvas {
        id: tempoGraph
        objectName: "tempoGraph"
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const list = lane.tempoRows
            if (list.length === 0) return
            const invalid = lane.tempoModel && lane.tempoModel.previewError.length > 0
            const bottom = height - Tk.Theme.size.border
            ctx.beginPath()
            let x = Math.min(0, lane.xOf(list[0]))
            ctx.moveTo(x, lane.yForBpm(list[0].bpm))
            for (let i = 0; i < list.length; ++i) {
                const y = lane.yForBpm(list[i].bpm)
                const nextX = i + 1 < list.length ? lane.xOf(list[i + 1]) : width
                ctx.lineTo(Math.max(x, lane.xOf(list[i])), y)
                ctx.lineTo(nextX, y)
                x = nextX
            }
            ctx.strokeStyle = invalid ? Tk.Theme.color.danger : Tk.Theme.color.accent
            ctx.lineWidth = Tk.Theme.size.border * 2
            ctx.stroke()
            ctx.lineTo(width, bottom)
            ctx.lineTo(Math.min(0, lane.xOf(list[0])), bottom)
            ctx.closePath()
            ctx.fillStyle = Tk.Theme.color.accentSubtle
            ctx.globalAlpha = Tk.Theme.opacity.muted
            ctx.fill()
        }
        Connections { target: lane; function onViewRevisionChanged() { tempoGraph.requestPaint() } }
        onHeightChanged: requestPaint()
    }

    Repeater {
        model: lane.rows
        delegate: Item {
            id: marker
            required property var modelData
            readonly property bool tempo: marker.modelData.kind === "tempo"
            readonly property bool current: marker.modelData.id === lane.currentEventId
            objectName: (marker.tempo ? "tempoEvent_" : "meterEvent_") + marker.modelData.id
            x: lane.xAt(lane.viewRevision, marker.modelData) - Tk.Theme.size.iconSm / 2
            y: marker.tempo ? lane.yForBpm(marker.modelData.bpm) - Tk.Theme.size.iconSm / 2 : 0
            width: Tk.Theme.size.iconSm + Tk.Theme.space.xs + label.implicitWidth
            height: marker.tempo ? Tk.Theme.size.iconSm : lane.meterBandHeight
            visible: x + width >= 0 && x <= lane.width
            Accessible.role: Accessible.ListItem
            Accessible.name: marker.modelData.label
            Accessible.selectable: true
            Accessible.selected: marker.current
            Accessible.focused: marker.current && lane.activeFocus
            Accessible.onPressAction: lane.select(marker.modelData.id)

            Rectangle {
                visible: !marker.tempo
                x: Tk.Theme.size.iconSm / 2
                width: Tk.Theme.size.border * 2
                height: parent.height
                color: marker.modelData.preview ? Tk.Theme.color.warning : Tk.Theme.color.info
            }
            Rectangle {
                visible: marker.tempo
                anchors.verticalCenter: parent.verticalCenter
                width: Tk.Theme.size.iconSm
                height: width
                rotation: 45
                scale: 0.75
                color: marker.modelData.preview ? Tk.Theme.color.warning : Tk.Theme.color.accent
                border.width: Tk.Theme.size.border
                border.color: marker.current ? Tk.Theme.color.text : Tk.Theme.color.accentContrast
            }
            Tk.Caption {
                id: label
                objectName: (marker.tempo ? "tempoLabel_" : "meterLabel_") + marker.modelData.id
                x: Tk.Theme.size.iconSm + Tk.Theme.space.xs
                y: (parent.height - height) / 2 - (marker.tempo ? Tk.Theme.space.sm : 0)
                text: marker.tempo ? marker.modelData.bpmText : marker.modelData.valueText
                color: marker.current ? Tk.Theme.color.text : Tk.Theme.color.textMuted
            }
            Rectangle {
                objectName: "tempoFocus_" + marker.modelData.id
                visible: marker.current && lane.activeFocus
                anchors.fill: parent
                anchors.margins: -Tk.Theme.size.border
                color: "transparent"
                radius: Tk.Theme.radius.xs
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.focus
            }
        }
    }

    Rectangle {
        objectName: "tempoPlayhead"
        x: lane.playheadX(lane.viewRevision, lane.playheadTicks)
        visible: x >= 0 && x <= lane.width
        width: Tk.Theme.size.border
        height: lane.height
        color: Tk.Theme.color.accent
    }

    Tk.Caption {
        objectName: "tempoLaneError"
        visible: lane.tempoModel !== null && lane.tempoModel.previewError.length > 0
        anchors.right: parent.right
        anchors.rightMargin: Tk.Theme.space.sm
        y: lane.meterBandHeight + Tk.Theme.space.xs
        text: lane.tempoModel ? lane.tempoModel.previewError : ""
        color: Tk.Theme.color.danger
    }

    Item {
        id: editorAnchor
        x: Math.max(0, Math.min(lane.width - width, lane.xAt(lane.viewRevision, lane.currentEvent)))
        y: 0
        width: Tk.Theme.size.iconSm
        height: lane.height
    }

    MouseArea {
        objectName: "tempoLanePointer"
        anchors.fill: parent
        enabled: lane.enabled && !!lane.tempoModel && !!lane.viewport
        preventStealing: internal.operationId.length > 0
        onPressed: function(mouse) {
            lane.forceActiveFocus()
            internal.pressX = mouse.x
            internal.pressY = mouse.y
            const hit = lane.hitEvent(mouse.x, mouse.y)
            internal.onMarker = hit.length > 0
            internal.pressEvent = hit.length > 0 ? hit : (mouse.y >= lane.meterBandHeight ? lane.segmentAt(mouse.x) : "")
            if (internal.pressEvent.length > 0) lane.currentEventId = internal.pressEvent
        }
        onPositionChanged: function(mouse) {
            if (!pressed || internal.pressEvent.length === 0) return
            if (internal.operationId.length === 0) {
                if (Math.hypot(mouse.x - internal.pressX, mouse.y - internal.pressY) < Qt.styleHints.startDragDistance) return
                if (!lane.beginDrag(mouse.x, mouse.y)) { internal.pressEvent = ""; return }
            }
            lane.updateDrag(mouse.x, mouse.y)
        }
        onReleased: {
            internal.pressEvent = ""
            internal.frozen = false
            if (internal.operationId.length === 0) return
            const operation = internal.operationId
            const owner = internal.sessionModel
            internal.operationId = ""
            internal.sessionModel = null
            lane.report(owner.commitEdit(operation))
        }
        onCanceled: lane.cancelDrag()
        onDoubleClicked: function(mouse) {
            const hit = lane.hitEvent(mouse.x, mouse.y)
            if (hit.length > 0) {
                lane.currentEventId = hit
                lane.eventActivated(hit)
                lane.openEditor()
                return
            }
            lane.addAt(mouse.y < lane.meterBandHeight ? "meter" : "tempo", lane.viewport.ticksForX(mouse.x))
        }
    }

    Tk.TempoEventEditor {
        id: numericEditor
        tempoModel: lane.tempoModel
        anchorItem: editorAnchor
        onEdited: function(result) { if (!result.ok) lane.editRejected(result.error) }
    }
}
