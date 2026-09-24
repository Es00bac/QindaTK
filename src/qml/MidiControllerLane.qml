// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: events and exact tick boundaries are host snapshots. This lane
// sends stable event/controller IDs and requested values; encoding, quantization,
// per-note association, acceptance, and history belong to the MIDI host.
T.Control {
    id: root

    property string patternId: ""
    property string controllerId: "velocity"
    property string controllerLabel: qsTr("Velocity")
    property real minimumValue: 0
    property real maximumValue: 1
    property real defaultValue: 0.8
    property int resolutionBits: 7
    property var events: [] // {id, ticks, value, x, noteId}
    property var tickBoundaries: []
    property string selectedEventId: ""
    property string tooltip: ""
    property string statusText: ""
    property string _gestureId: ""
    property string _gestureEventId: ""
    property int _serial: 0

    signal eventSelected(string eventId)
    signal valuePreviewRequested(string requestId, string patternId, string controllerId,
                                 string eventId, string ticks, real value)
    signal editFinished(string requestId)
    signal editCanceled(string requestId)
    signal eventValueRequested(string patternId, string controllerId, string eventId,
                               string ticks, real value)
    signal eventDeleteRequested(string patternId, string controllerId, string eventId)
    signal zoomRequested(string anchorTicks, real factor)
    signal scrollRequested(real fractionDelta)

    objectName: "midiControllerLane"
    implicitWidth: Tk.Theme.size.sparkline * 3
    implicitHeight: Tk.Theme.size.waveformHeight * 2
    Accessible.role: Accessible.Graphic
    Accessible.name: root.tooltip.length ? root.tooltip : root.controllerLabel
    Accessible.description: root.statusText.length ? root.statusText
        : qsTr("%1 controller values. Drag a stem or bar to request an edit.").arg(root.controllerLabel)
    activeFocusOnTab: true
    focus: true

    function css(c) { return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")" }
    function tickAt(x) {
        if (!root.tickBoundaries.length) return "0"
        const i = Math.min(root.tickBoundaries.length - 1, Math.floor(Math.max(0, Math.min(0.999999, x / Math.max(1, root.width))) * root.tickBoundaries.length))
        return String(root.tickBoundaries[i])
    }
    function eventAt(x) {
        let nearest = null, distance = Tk.Theme.size.controlSm
        for (let i = 0; i < root.events.length; ++i) {
            const event = root.events[i]
            const d = Math.abs(Number(event.x) * root.width - x)
            if (d <= distance) { nearest = event; distance = d }
        }
        return nearest
    }
    function valueAt(y) {
        const fraction = 1 - Math.max(0, Math.min(1, y / Math.max(1, root.height)))
        return root.minimumValue + fraction * (root.maximumValue - root.minimumValue)
    }
    function beginEditAt(x) {
        if (!root.enabled || root._gestureId.length) return false
        const event = root.eventAt(x)
        if (!event) return false
        root._serial += 1
        root._gestureId = "midi-controller-" + root._serial
        root._gestureEventId = String(event.id)
        root.selectedEventId = root._gestureEventId
        root.eventSelected(root.selectedEventId)
        return true
    }
    function updateEditAt(x, y) {
        if (!root._gestureId.length) return false
        const ticks = root.tickAt(x)
        const steps = Math.pow(2, Math.max(1, root.resolutionBits)) - 1
        const raw = root.valueAt(y)
        const value = root.minimumValue + Math.round((raw - root.minimumValue) / Math.max(1e-12, root.maximumValue - root.minimumValue) * steps)
                               / steps * (root.maximumValue - root.minimumValue)
        root.valuePreviewRequested(root._gestureId, root.patternId, root.controllerId,
                                   root._gestureEventId, ticks, value)
        laneCanvas.requestPaint()
        return true
    }
    function finishEdit() {
        if (!root._gestureId.length) return false
        const id = root._gestureId; root._gestureId = ""; root.editFinished(id); laneCanvas.requestPaint(); return true
    }
    function cancelEdit() {
        if (!root._gestureId.length) return false
        const id = root._gestureId; root._gestureId = ""; root.editCanceled(id); laneCanvas.requestPaint(); return true
    }
    function requestSetValue(eventId, ticks, value) {
        if (!root.enabled) return false
        root.eventValueRequested(root.patternId, root.controllerId, String(eventId), String(ticks), Number(value))
        return true
    }
    function requestDelete(eventId) {
        if (!root.enabled || !eventId) return false
        root.eventDeleteRequested(root.patternId, root.controllerId, String(eventId))
        return true
    }

    background: Rectangle {
        color: Tk.Theme.color.panelAlt
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        radius: Tk.Theme.radius.sm
    }
    Canvas {
        id: laneCanvas
        objectName: "midiControllerCanvas"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.xs
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            const w = width, h = height
            ctx.fillStyle = root.css(Tk.Theme.color.inputBg); ctx.fillRect(0, 0, w, h)
            for (let i = 0; i < root.tickBoundaries.length; ++i) {
                const x = i / Math.max(1, root.tickBoundaries.length - 1) * w
                ctx.strokeStyle = root.css(i % 4 === 0 ? Tk.Theme.color.border : Tk.Theme.color.divider)
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke()
            }
            for (let i = 0; i < root.events.length; ++i) {
                const event = root.events[i], x = Number(event.x) * w
                const fraction = Math.max(0, Math.min(1, (Number(event.value) - root.minimumValue) / Math.max(1e-12, root.maximumValue - root.minimumValue)))
                ctx.strokeStyle = root.css(event.id === root.selectedEventId ? Tk.Theme.color.accent : Tk.Theme.color.info)
                ctx.lineWidth = event.id === root.selectedEventId ? Tk.Theme.size.focusRing : Tk.Theme.size.border
                ctx.beginPath(); ctx.moveTo(x, h); ctx.lineTo(x, h * (1 - fraction)); ctx.stroke()
                ctx.fillStyle = ctx.strokeStyle; ctx.fillRect(x - Tk.Theme.size.controlSm / 4, h * (1 - fraction) - Tk.Theme.size.controlSm / 4,
                                                              Tk.Theme.size.controlSm / 2, Tk.Theme.size.controlSm / 2)
            }
        }
    }
    onEventsChanged: laneCanvas.requestPaint()
    onTickBoundariesChanged: laneCanvas.requestPaint()
    onSelectedEventIdChanged: laneCanvas.requestPaint()
    onWidthChanged: laneCanvas.requestPaint()
    onHeightChanged: laneCanvas.requestPaint()
    DragHandler {
        target: null
        onActiveChanged: {
            if (active) root.beginEditAt(centroid.position.x)
            else if (root._gestureId.length) root.finishEdit()
        }
        onCentroidChanged: if (active) root.updateEditAt(centroid.position.x, centroid.position.y)
    }
    TapHandler {
        onTapped: function(eventPoint) {
            const event = root.eventAt(eventPoint.position.x)
            if (event) { root.selectedEventId = String(event.id); root.eventSelected(root.selectedEventId) }
        }
        onDoubleTapped: function(eventPoint) {
            const event = root.eventAt(eventPoint.position.x)
            if (event) root.requestSetValue(event.id, event.ticks, root.valueAt(eventPoint.position.y))
        }
    }
    WheelHandler {
        target: null
        onWheel: function(event) {
            if (event.modifiers & Qt.ControlModifier) root.zoomRequested(root.tickAt(event.x), event.angleDelta.y > 0 ? 1.2 : 0.83)
            else root.scrollRequested(event.angleDelta.y > 0 ? -0.1 : 0.1)
            event.accepted = true
        }
    }
    Keys.onEscapePressed: event => { root.cancelEdit(); event.accepted = true }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Delete && root.selectedEventId.length) {
            root.requestDelete(root.selectedEventId); event.accepted = true
        }
    }
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
