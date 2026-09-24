// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: rows and per-step snapshots belong to the pattern model.
// Taps send semantic requests by stable pattern/row/step IDs; playback,
// probability seed policy, and accepted changes stay in the sequencer host.
T.Control {
    id: root

    property string patternId: ""
    property string patternName: qsTr("Pattern")
    property var rows: [] // {id, name, steps:[{id, enabled, velocity, probability, gate}]}
    property int stepCount: 16
    property string selectedStepId: ""
    property string tooltip: ""
    property string statusText: ""
    property real probabilityStep: 0.05
    property int _selectedRowIndex: 0
    property int _selectedStepIndex: 0
    property int _serial: 0

    signal stepEditRequested(string requestId, string patternId, string rowId,
                             string stepId, string field, var value)
    signal stepSelected(string rowId, string stepId)
    signal probabilityEditRequested(string requestId, string patternId, string rowId,
                                    string stepId, real probability)
    signal zoomRequested(real factor)
    signal scrollRequested(int rowDelta, int stepDelta)

    objectName: "stepSequencer"
    implicitWidth: Tk.Theme.size.sparkline * 3
    implicitHeight: Tk.Theme.size.waveformHeight * 3
    Accessible.role: Accessible.Table
    Accessible.name: root.tooltip.length ? root.tooltip : root.patternName
    Accessible.description: root.statusText.length ? root.statusText
        : qsTr("%1 step rows; cells show gate, velocity and probability.").arg(root.rows.length)
    activeFocusOnTab: true
    focus: true

    function css(c) { return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")" }
    function cellAt(x, y) {
        if (!root.rows.length || root.stepCount < 1) return null
        const rowIndex = Math.min(root.rows.length - 1, Math.floor(y / Math.max(1, root.height) * root.rows.length))
        const stepIndex = Math.min(root.stepCount - 1, Math.floor(x / Math.max(1, root.width) * root.stepCount))
        const row = root.rows[rowIndex]
        if (!row.steps || stepIndex >= row.steps.length) return null
        return { row: row, step: row.steps[stepIndex], rowIndex: rowIndex, stepIndex: stepIndex }
    }
    function toggleAt(x, y) {
        if (!root.enabled) return false
        const cell = root.cellAt(x, y)
        if (!cell) return false
        root._serial += 1
        root.selectedStepId = String(cell.step.id)
        root._selectedRowIndex = cell.rowIndex
        root._selectedStepIndex = cell.stepIndex
        root.stepSelected(String(cell.row.id), root.selectedStepId)
        root.stepEditRequested("step-edit-" + root._serial, root.patternId,
                               String(cell.row.id), String(cell.step.id), "enabled", !Boolean(cell.step.enabled))
        return true
    }
    function setStepValue(rowId, stepId, field, value) {
        if (!root.enabled || ["velocity", "probability", "gate"].indexOf(field) < 0) return false
        root._serial += 1
        root.stepEditRequested("step-edit-" + root._serial, root.patternId,
                               String(rowId), String(stepId), field, value)
        return true
    }
    function requestProbability(rowId, stepId, value) {
        if (!root.enabled || !isFinite(value)) return false
        root._serial += 1
        root.probabilityEditRequested("step-probability-" + root._serial, root.patternId,
                                      String(rowId), String(stepId), Math.max(0, Math.min(1, value)))
        return true
    }

    background: Rectangle {
        color: Tk.Theme.color.panelAlt
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        radius: Tk.Theme.radius.sm
    }
    Canvas {
        id: gridCanvas
        objectName: "stepSequencerCanvas"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.xs
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            const w = width, h = height
            const labelWidth = Math.min(Tk.Theme.size.labelWidth, w / 3)
            const gridX = labelWidth, gridW = Math.max(1, w - gridX)
            const rowHeight = h / Math.max(1, root.rows.length)
            const cellWidth = gridW / Math.max(1, root.stepCount)
            ctx.fillStyle = root.css(Tk.Theme.color.inputBg); ctx.fillRect(0, 0, w, h)
            for (let r = 0; r < root.rows.length; ++r) {
                const row = root.rows[r]
                const y = r * rowHeight
                ctx.fillStyle = root.css(Tk.Theme.color.panelAlt); ctx.fillRect(0, y, labelWidth, rowHeight)
                ctx.font = Tk.Theme.font.caption + "px " + Tk.Theme.font.family
                ctx.fillStyle = root.css(Tk.Theme.color.text)
                ctx.textBaseline = "middle"
                ctx.fillText(String(row.name || row.id), Tk.Theme.space.xs, y + rowHeight / 2, labelWidth - Tk.Theme.space.md)
                for (let s = 0; s < Math.min(root.stepCount, row.steps.length); ++s) {
                    const step = row.steps[s], x = gridX + s * cellWidth
                    if (s % 4 === 0) {
                        ctx.fillStyle = root.css(Tk.Theme.color.panelAlt)
                        ctx.fillRect(x, y, Tk.Theme.size.border, rowHeight)
                    }
                    if (step.enabled) {
                        ctx.fillStyle = root.css(step.id === root.selectedStepId ? Tk.Theme.color.accent : Tk.Theme.color.info)
                        ctx.globalAlpha = Tk.Theme.opacity.disabled + (Tk.Theme.opacity.full - Tk.Theme.opacity.disabled) * Math.max(0, Math.min(1, Number(step.velocity) || 0))
                        const barHeight = (rowHeight - 2 * Tk.Theme.space.xs) * Math.max(Tk.Theme.opacity.disabled, Math.min(1, Number(step.gate) || 0.8))
                        ctx.fillRect(x + Tk.Theme.space.xs, y + rowHeight - barHeight - Tk.Theme.space.xs,
                                     Math.max(Tk.Theme.size.border, cellWidth - 2 * Tk.Theme.space.xs), barHeight)
                        ctx.globalAlpha = Tk.Theme.opacity.full
                    }
                    ctx.strokeStyle = root.css(Tk.Theme.color.divider)
                    ctx.lineWidth = Tk.Theme.size.border
                    ctx.strokeRect(x, y, cellWidth, rowHeight)
                    if (step.enabled && Number(step.probability) < 1) {
                        ctx.font = Tk.Theme.font.micro + "px " + Tk.Theme.font.monoFamily
                        ctx.fillStyle = root.css(Tk.Theme.color.textMuted)
                        ctx.textBaseline = "top"
                        ctx.fillText(Math.round(Number(step.probability) * 100) + "%", x + Tk.Theme.space.xs, y + Tk.Theme.space.xs)
                    }
                }
            }
        }
    }
    onRowsChanged: gridCanvas.requestPaint()
    onStepCountChanged: gridCanvas.requestPaint()
    onSelectedStepIdChanged: gridCanvas.requestPaint()
    onWidthChanged: gridCanvas.requestPaint()
    onHeightChanged: gridCanvas.requestPaint()
    TapHandler {
        onTapped: function(eventPoint) { root.toggleAt(eventPoint.position.x, eventPoint.position.y) }
    }
    Keys.onSpacePressed: event => {
        const stepWidth = root.width / Math.max(1, root.stepCount)
        const rowHeight = root.height / Math.max(1, root.rows.length)
        root.toggleAt((root._selectedStepIndex + 0.5) * stepWidth,
                      (root._selectedRowIndex + 0.5) * rowHeight)
        event.accepted = true
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            root.scrollRequested(0, event.key === Qt.Key_Left ? -1 : 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            root.scrollRequested(event.key === Qt.Key_Up ? -1 : 1, 0)
            event.accepted = true
        }
    }
    WheelHandler {
        target: null
        onWheel: function(event) {
            if (event.modifiers & Qt.ControlModifier)
                root.zoomRequested(event.angleDelta.y > 0 ? 1.2 : 0.83)
            else
                root.scrollRequested(event.angleDelta.y > 0 ? -1 : 1, event.angleDelta.x > 0 ? 1 : 0)
            event.accepted = true
        }
    }
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
