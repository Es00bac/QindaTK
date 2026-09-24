// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// One parameter's automation curve on the timeline's time axis.
//
// AGENT-CONTRACT: the curve is AutomationModel data keyed by `parameterId`;
// this lane hosts Tk.CurveEditor for point and handle interaction and maps
// its normalized plot to exact timeline ticks (TimelineViewport) and
// canonical values (AutomationModel). A coordinate the gesture did not move
// is sent back as the stored exact value, never re-derived from pixels. Each
// knot drag, handle drag or segment drag is one model session: release
// commits one undo step, Escape or a replaced model cancels. Keyboard: arrows
// on a focused point nudge it, Left/Right on the lane choose a point,
// Alt+Left/Right move it a grid step, Up/Down change it 1% of the range,
// I cycles its segment's interpolation, Delete removes it, Return/F2 opens
// numeric entry. Double-click adds a point; dragging near a segment moves
// both of its ends. Modes and recording belong to the model (writeState).
Item {
    id: lane

    property Tk.TimelineViewport viewport: null
    property Tk.AutomationModel model: null
    property string parameterId: ""
    property string currentPointId: ""
    property bool snapEnabled: false
    property string snapGrid: "frames"
    property real snapPixels: Tk.Theme.space.md
    property string playheadTicks: "0"
    property string displayMode: "timecode"
    property bool dropFrame: false
    property string tooltip: qsTr("Automation")
    readonly property real plotPadding: Tk.Theme.space.sm
    readonly property bool dragging: internal.operationId.length > 0
    // Dependencies are passed as arguments so every binding re-evaluates on
    // internal.revision (model signals, viewport changes, resize).
    readonly property var parameter: lane.parameterFor(internal.revision, lane.model, lane.parameterId)
    readonly property var writeState: lane.writeStateFor(internal.revision, lane.model, lane.parameterId)
    readonly property var visiblePoints: lane.pointsFor(internal.revision, lane.model, lane.viewport, lane.width)
    function parameterFor(revision, model, id) { return model && id.length > 0 ? model.parameterData(id) : ({}) }
    function writeStateFor(revision, model, id) { return model && id.length > 0 ? model.writeState(id) : ({}) }
    function pointsFor(revision, model, viewport, width) {
        if (!model || !viewport || width <= 0) return []
        const rows = lane.model.points(lane.parameterId)
        const result = []
        for (let i = 0; i < rows.length; ++i) {
            const x = lane.viewport.xForTicks(rows[i].ticks)
            result.push({ row: rows[i], x: x, index: i, prev: i > 0 ? rows[i - 1] : null,
                          next: i + 1 < rows.length ? rows[i + 1] : null })
        }
        return result
    }
    readonly property var currentPoint: {
        for (let i = 0; i < lane.visiblePoints.length; ++i)
            if (lane.visiblePoints[i].row.id === lane.currentPointId) return lane.visiblePoints[i].row
        return ({})
    }
    readonly property var knots: {
        const result = []
        for (let i = 0; i < lane.visiblePoints.length; ++i) {
            const entry = lane.visiblePoints[i]
            if (entry.x < 0 || entry.x > lane.width) continue
            const knot = { id: entry.row.id, x: entry.x / lane.width, y: entry.row.normalizedValue,
                           label: qsTr("%1 %2 at %3").arg(lane.parameter.name || "").arg(entry.row.valueText)
                                  .arg(lane.viewport.model ? lane.viewport.model.formatTicks(entry.row.ticks, lane.displayMode, lane.dropFrame) : entry.row.ticks) }
            if (entry.row.id === lane.currentPointId) {
                if (entry.next && entry.row.interpolation === "cubic") {
                    const h = entry.row.outHandle || { x: 1 / 3, y: entry.row.normalizedValue + (entry.next.normalizedValue - entry.row.normalizedValue) / 3 }
                    const hx = entry.x + h.x * (lane.viewport.xForTicks(entry.next.ticks) - entry.x)
                    if (hx >= 0 && hx <= lane.width) knot.outHandle = { x: hx / lane.width, y: h.y }
                }
                if (entry.prev && entry.prev.interpolation === "cubic") {
                    const h = entry.row.inHandle || { x: 2 / 3, y: entry.prev.normalizedValue + 2 * (entry.row.normalizedValue - entry.prev.normalizedValue) / 3 }
                    const px = lane.viewport.xForTicks(entry.prev.ticks)
                    const hx = px + h.x * (entry.x - px)
                    if (hx >= 0 && hx <= lane.width) knot.inHandle = { x: hx / lane.width, y: h.y }
                }
            }
            result.push(knot)
        }
        return result
    }

    signal editRejected(string reason)

    function samplesFor(revision, model, viewport, width) {
        if (!model || !viewport || width <= 0) return []
        return model.sampleLane(lane.parameterId, viewport.ticksForX(0), viewport.ticksForX(width),
                                Math.max(2, Math.round(width / Tk.Theme.space.sm)))
    }
    function xAt(revision, ticks) { return lane.viewport && ticks ? lane.viewport.xForTicks(ticks) : 0 }

    function report(result) {
        if (result && result.ok === false) lane.editRejected(result.error || qsTr("The automation edit was rejected"))
        return !!result && result.ok !== false
    }
    function plotHeight() { return Math.max(1, lane.height - 2 * lane.plotPadding) }
    function yForNormalized(n) { return lane.plotPadding + (1 - n) * lane.plotHeight() }
    function normalizedAtY(y) { return 1 - (y - lane.plotPadding) / lane.plotHeight() }
    function rawValueAtY(y) {
        const n = lane.normalizedAtY(y)
        return lane.parameter.minimum + n * (lane.parameter.maximum - lane.parameter.minimum)
    }
    function snapped(ticks) {
        if (!lane.snapEnabled || !lane.viewport.model) return ticks
        return lane.viewport.model.snapTicks(ticks, { toleranceTicks: lane.viewport.ticksForPixels(lane.snapPixels),
                                                      grid: lane.snapGrid, playheadTicks: lane.playheadTicks }).ticks
    }
    function beginSession(request) {
        if (internal.operationId.length > 0) return true
        request.parameterId = lane.parameterId
        request.expectedRevision = lane.model.revision
        const begun = lane.model.beginEdit(request)
        if (!lane.report(begun)) return false
        internal.operationId = begun.operationId
        internal.sessionModel = lane.model
        return true
    }
    function previewKnot(id, nx, ny) {
        if (internal.operationId.length === 0) {
            const knot = lane.knots.find(function(k) { return k.id === id })
            const row = lane.model.pointData(lane.parameterId, id)
            if (!knot || !row.id) return
            internal.grab = { x: knot.x, y: knot.y, ticks: row.ticks, value: row.value }
            if (!lane.beginSession({ kind: "movePoints", pointIds: [id] })) return
        }
        const grab = internal.grab
        const ticks = nx === grab.x ? grab.ticks : lane.snapped(lane.viewport.ticksForX(nx * lane.width))
        const value = ny === grab.y ? grab.value : lane.model.denormalize(lane.parameterId, ny)
        lane.model.updateEdit(internal.operationId, { ticks: ticks, value: value })
    }
    function previewHandle(id, side, nx, ny) {
        const entry = lane.visiblePoints.find(function(p) { return p.row.id === id })
        if (!entry || (side === "out" ? !entry.next : !entry.prev)) return
        if (!lane.beginSession({ kind: "setHandle", pointId: id, side: side })) return
        const from = side === "out" ? entry.x : lane.viewport.xForTicks(entry.prev.ticks)
        const to = side === "out" ? lane.viewport.xForTicks(entry.next.ticks) : entry.x
        const fraction = Math.max(0, Math.min(1, (nx * lane.width - from) / Math.max(1e-9, to - from)))
        lane.model.updateEdit(internal.operationId, { handle: { x: fraction, y: ny } })
    }
    function finishSession() {
        if (internal.operationId.length === 0) return
        const operation = internal.operationId
        const owner = internal.sessionModel
        internal.operationId = ""
        internal.sessionModel = null
        internal.pressSegment = ""
        lane.report(owner.commitEdit(operation))
    }
    function cancelSession() {
        if (internal.operationId.length > 0 && internal.sessionModel)
            internal.sessionModel.cancelEdit(internal.operationId)
        internal.operationId = ""
        internal.sessionModel = null
        internal.pressSegment = ""
    }
    function select(id) {
        lane.currentPointId = id
        const row = lane.currentPoint
        if (row.ticks && lane.viewport) lane.viewport.ensureTicksVisible(row.ticks, Tk.Theme.size.dropEdge)
    }
    function addAt(x, y) {
        const result = lane.model.applyEdit({ kind: "addPoint", parameterId: lane.parameterId,
                                              ticks: lane.snapped(lane.viewport.ticksForX(x)),
                                              value: lane.model.denormalize(lane.parameterId, lane.normalizedAtY(y)) })
        if (lane.report(result) && result.createdIds.length > 0) lane.currentPointId = result.createdIds[0]
    }
    function removeCurrent() {
        if (lane.currentPointId.length === 0) return false
        const next = lane.model.adjacentPoint(lane.parameterId, lane.currentPointId, -1)
        if (!lane.report(lane.model.applyEdit({ kind: "removePoints", parameterId: lane.parameterId,
                                                pointIds: [lane.currentPointId] }))) return false
        lane.currentPointId = next === lane.currentPointId ? "" : next
        return true
    }
    function nudgeValue(fraction) {
        const row = lane.currentPoint
        if (!row.id) return false
        const range = lane.parameter.maximum - lane.parameter.minimum
        return lane.report(lane.model.applyEdit({ kind: "movePoints", parameterId: lane.parameterId, pointIds: [row.id] },
                                                { ticks: row.ticks, value: row.value + fraction * range }))
    }
    function nudgeTime(steps) {
        const row = lane.currentPoint
        if (!row.id || !lane.viewport.model) return false
        const grid = lane.snapGrid === "none" ? "frames" : lane.snapGrid
        return lane.report(lane.model.applyEdit({ kind: "movePoints", parameterId: lane.parameterId, pointIds: [row.id] },
                                                { ticks: lane.viewport.model.stepTicks(row.ticks, grid, steps) }))
    }
    function cycleInterpolation() {
        const row = lane.currentPoint
        if (!row.id) return false
        const order = ["step", "linear", "cubic"]
        return lane.report(lane.model.applyEdit({ kind: "setInterpolation", parameterId: lane.parameterId, pointIds: [row.id] },
                                                { interpolation: order[(order.indexOf(row.interpolation) + 1) % order.length] }))
    }

    objectName: "automationLane"
    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.row * 3
    clip: true
    activeFocusOnTab: lane.enabled
    Accessible.role: Accessible.Pane
    Accessible.name: lane.parameter.name ? qsTr("%1 automation").arg(lane.parameter.name) : lane.tooltip
    Accessible.description: qsTr("Mode %1. Left and Right choose a point; arrows on a point nudge it; Alt+Left and Alt+Right move it; I changes its curve; Delete removes it; Return edits it numerically; double-click adds a point.")
                            .arg(lane.writeState.mode || "")
    Accessible.focusable: lane.enabled
    Accessible.focused: lane.activeFocus

    onModelChanged: lane.cancelSession()
    onParameterIdChanged: { lane.cancelSession(); lane.currentPointId = "" }
    onWidthChanged: internal.revision++
    onHeightChanged: internal.revision++
    Component.onDestruction: lane.cancelSession()

    Keys.onPressed: function(event) {
        if (!lane.enabled || !lane.model) return
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        switch (event.key) {
        case Qt.Key_Left: if (alt) lane.nudgeTime(-1); else lane.select(lane.model.adjacentPoint(lane.parameterId, lane.currentPointId, -1)); break
        case Qt.Key_Right: if (alt) lane.nudgeTime(1); else lane.select(lane.model.adjacentPoint(lane.parameterId, lane.currentPointId, 1)); break
        case Qt.Key_Up: lane.nudgeValue(0.01); break
        case Qt.Key_Down: lane.nudgeValue(-0.01); break
        case Qt.Key_I: lane.cycleInterpolation(); break
        case Qt.Key_Delete: case Qt.Key_Backspace: lane.removeCurrent(); break
        case Qt.Key_F2: case Qt.Key_Return: case Qt.Key_Enter:
            if (lane.currentPointId.length > 0) pointEditor.openPoint(lane.parameterId, lane.currentPointId)
            break
        case Qt.Key_Escape: if (!lane.dragging) return; lane.cancelSession(); break
        default: return
        }
        event.accepted = true
    }

    QtObject {
        id: internal
        property int revision: 0
        property string operationId: ""
        property Tk.AutomationModel sessionModel: null
        property var grab: ({})
        property string pressSegment: ""
        property real pressY: 0
        property real grabValue: 0
        property real pressValue: 0
    }
    Connections {
        target: lane.viewport
        ignoreUnknownSignals: true
        function onViewChanged() { internal.revision++ }
    }
    Connections {
        target: lane.model
        ignoreUnknownSignals: true
        function onPointsChanged(parameterId) { if (parameterId === lane.parameterId) internal.revision++ }
        function onParametersChanged() { internal.revision++ }
        function onModeChanged() { internal.revision++ }
        function onWriteStateChanged(parameterId) { if (parameterId === lane.parameterId) internal.revision++ }
        function onEditInvalidated(operationId, reason) {
            if (operationId !== internal.operationId) return
            internal.operationId = ""
            internal.sessionModel = null
            lane.editRejected(reason)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.surface
    }
    Repeater {
        model: 3
        delegate: Rectangle {
            required property int index
            y: lane.yForNormalized((index + 1) / 4)
            width: lane.width
            height: Tk.Theme.size.border
            color: Tk.Theme.color.divider
            opacity: Tk.Theme.opacity.muted
        }
    }
    Rectangle {
        objectName: "automationWriteBand"
        visible: lane.writeState.writing === true && !!lane.viewport
        x: Math.max(0, lane.xAt(internal.revision, lane.writeState.passStart || "0"))
        width: lane.model ? Math.max(Tk.Theme.size.border, lane.xAt(internal.revision, lane.model.transportTicks) - x) : 0
        height: lane.height
        color: Tk.Theme.color.dangerSubtle
    }
    MouseArea {
        objectName: "automationLanePointer"
        anchors.fill: parent
        enabled: lane.enabled && !!lane.model && !!lane.viewport && lane.parameterId.length > 0
        preventStealing: internal.operationId.length > 0
        onPressed: function(mouse) {
            lane.forceActiveFocus()
            internal.pressSegment = ""
            const ticks = lane.viewport.ticksForX(mouse.x)
            const segment = lane.model.segmentAt(lane.parameterId, ticks)
            if (segment.length === 0) return
            const curveY = lane.yForNormalized(lane.model.normalize(lane.parameterId, lane.model.valueAt(lane.parameterId, ticks)))
            if (Math.abs(curveY - mouse.y) > Tk.Theme.size.icon) return
            internal.pressSegment = segment
            internal.pressY = mouse.y
            lane.currentPointId = segment
        }
        onPositionChanged: function(mouse) {
            if (!pressed || internal.pressSegment.length === 0) return
            if (internal.operationId.length === 0) {
                if (Math.abs(mouse.y - internal.pressY) < Qt.styleHints.startDragDistance) return
                internal.grabValue = lane.model.pointData(lane.parameterId, internal.pressSegment).value
                internal.pressValue = lane.rawValueAtY(internal.pressY)
                if (!lane.beginSession({ kind: "moveSegment", pointId: internal.pressSegment })) {
                    internal.pressSegment = ""
                    return
                }
            }
            lane.model.updateEdit(internal.operationId,
                                  { value: internal.grabValue + lane.rawValueAtY(mouse.y) - internal.pressValue })
        }
        onReleased: lane.finishSession()
        onCanceled: lane.cancelSession()
        onDoubleClicked: function(mouse) { lane.addAt(mouse.x, mouse.y) }
    }
    Tk.CurveEditor {
        id: editor
        objectName: "automationCurveEditor"
        x: -editor.plotPadding
        width: lane.width + 2 * editor.plotPadding
        height: lane.height
        plotPadding: lane.plotPadding
        referenceLine: false
        knots: lane.knots
        samples: lane.samplesFor(internal.revision, lane.model, lane.viewport, lane.width)
        selectedKnotId: lane.currentPointId
        tooltip: lane.parameter.name ? qsTr("%1: drag to edit, double-click to add").arg(lane.parameter.name) : lane.tooltip
        accessibleName: lane.Accessible.name
        onKnotSelected: function(id) { lane.currentPointId = id }
        onMovePreview: function(id, x, y) { lane.previewKnot(id, x, y) }
        onMoveFinished: lane.finishSession()
        onMoveCancelled: lane.cancelSession()
        onTangentPreviewRequested: function(knotId, side, x, y) { lane.previewHandle(knotId, side, x, y) }
        onTangentEditFinished: function(knotId, side, x, y) { lane.previewHandle(knotId, side, x, y); lane.finishSession() }
        onTangentEditCanceled: lane.cancelSession()
    }
    Tk.Caption {
        objectName: "automationModeBadge"
        x: Tk.Theme.space.sm
        y: Tk.Theme.space.xs
        text: [lane.parameter.name || "", lane.writeState.mode || "",
               lane.writeState.writing ? qsTr("writing") : (lane.writeState.latched ? qsTr("latched") : "")]
              .filter(function(part) { return part.length > 0 }).join(" · ")
        color: lane.writeState.writing ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
    }
    Tk.Caption {
        objectName: "automationValueReadout"
        anchors.right: parent.right
        anchors.rightMargin: Tk.Theme.space.sm
        y: Tk.Theme.space.xs
        text: lane.model && lane.model.previewError.length > 0 ? lane.model.previewError : (lane.currentPoint.valueText || "")
        color: lane.model && lane.model.previewError.length > 0 ? Tk.Theme.color.danger : Tk.Theme.color.text
    }
    Rectangle {
        objectName: "automationPlayhead"
        x: lane.xAt(internal.revision, lane.playheadTicks)
        visible: x >= 0 && x <= lane.width
        width: Tk.Theme.size.border
        height: lane.height
        color: Tk.Theme.color.accent
    }
    Item {
        id: editorAnchor
        x: Math.max(0, Math.min(lane.width, lane.xAt(internal.revision, lane.currentPoint.ticks)))
        width: Tk.Theme.size.iconSm
        height: lane.height
    }
    Tk.AutomationPointEditor {
        id: pointEditor
        model: lane.model
        timeline: lane.viewport ? lane.viewport.model : null
        displayMode: lane.displayMode
        dropFrame: lane.dropFrame
        anchorItem: editorAnchor
        onEdited: function(result) { if (!result.ok) lane.editRejected(result.error) }
    }
}
