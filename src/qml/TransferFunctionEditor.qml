// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Edits a Tk.TransferFunctionModel: measured-value stops on a gradient over
// the model's domain (click the track to add at the current mapping, drag or
// arrow-key a stop to move it, Delete or right-click to remove) plus the
// selected stop's exact value, opacity and color. Sized to its content.
// A drag or slider press is one undoable step (Escape cancels a drag);
// Ctrl+Z / Ctrl+Shift+Z and the header buttons undo and redo. The header
// picks a built-in preset; the range row sets what values below or above the
// domain and missing values map to; `histogram` (a VoxelVolume.histogram()
// map) draws the value distribution above the gradient with its explicit
// below/above/missing counts.
// AGENT-CONTRACT (docs/volume-imaging.md): edits write only the transfer
// settings model. Voxel source data and its revision are never changed here.
// Gestures use the model's beginEdit/commitEdit/cancelEdit transaction
// (D-148), so previews repaint views but only the committed step is recorded.
// AGENT-GUARD: a gesture this editor opened is closed by this editor: a model
// swap or the editor's destruction cancels it (_cancelGesture), otherwise the
// model stays `editing` and refuses undo, redo and every later gesture.
T.Control {
    id: control
    objectName: "transferFunctionEditor"
    property Tk.TransferFunctionModel model: null
    property string selectedPointId: ""
    property string tooltip: qsTr("Transfer function")
    // A VoxelVolume.histogram() result over the model's domain, or null.
    property var histogram: null
    readonly property var selectedPoint: {
        if (!model) return ({})
        const revision = model.revision
        return revision >= 0 ? model.point(selectedPointId) : ({})
    }
    signal customColorRequested(string pointId)

    readonly property real _span: model ? model.domainMax - model.domainMin : 1
    readonly property int _magnitude: Math.floor(Math.log10(_span))
    readonly property bool _innerSelected: !!model && !!selectedPoint.id
        && selectedPoint.value > model.domainMin && selectedPoint.value < model.domainMax
    // AGENT-NOTE: field values are pushed from the model rather than bound. A
    // user edit breaks a binding, and an edit the model rejects must snap back
    // to the stored point instead of showing a value that was never applied.
    function _syncFields() {
        if (pointValue) pointValue.value = selectedPoint.value || 0
        if (pointOpacity) pointOpacity.value = selectedPoint.opacity || 0
        if (pointColor) pointColor.color = selectedPoint.color || Tk.Theme.color.text
    }
    onSelectedPointChanged: _syncFields()
    function _valueAt(position) {
        return model.domainMin + position * (model.domainMax - model.domainMin)
    }
    function undo() { return !!model && model.undo() }
    function redo() { return !!model && model.redo() }

    // The model whose gesture this editor opened (null when none is open).
    property Tk.TransferFunctionModel _gestureModel: null
    function _beginGesture(label) {
        if (!model) return false
        if (model.editing) return _gestureModel === model // another client's gesture: stay out
        if (!model.beginEdit(label)) return false
        _gestureModel = model
        return true
    }
    function _commitGesture() {
        const owner = _gestureModel
        _gestureModel = null
        return !!owner && owner.editing && owner.commitEdit()
    }
    function _cancelGesture() {
        const owner = _gestureModel
        _gestureModel = null
        return !!owner && owner.editing && owner.cancelEdit()
    }
    onModelChanged: _cancelGesture()
    Component.onDestruction: _cancelGesture()
    readonly property var _rangeColors: [
        {name: qsTr("Text"), color: Tk.Theme.color.text},
        {name: qsTr("Accent"), color: Tk.Theme.color.accent},
        {name: qsTr("Warning"), color: Tk.Theme.color.warning},
        {name: qsTr("Danger"), color: Tk.Theme.color.danger},
        {name: qsTr("Success"), color: Tk.Theme.color.success}]
    function _rangeTooltip(which, range) {
        if (!range || !range.mode) return ""
        if (which === "missing")
            return range.opacity > 0 ? qsTr("Missing values: %1 at %2 opacity").arg(range.color).arg(range.opacity)
                                     : qsTr("Missing values: transparent")
        const side = which === "below" ? qsTr("Below the domain") : qsTr("Above the domain")
        return range.mode === "clamp" ? qsTr("%1: clamps to the end stop").arg(side)
                                      : qsTr("%1: %2 at %3 opacity").arg(side).arg(range.color).arg(range.opacity)
    }

    Keys.onPressed: function(event) {
        if (event.matches(StandardKey.Undo)) {
            event.accepted = control.undo()
        } else if (event.matches(StandardKey.Redo)) {
            event.accepted = control.redo()
        } else if (event.key === Qt.Key_Escape && control._gestureModel) {
            event.accepted = control._cancelGesture()
        } else {
            event.accepted = false
        }
    }

    implicitWidth: Math.max(Tk.Theme.size.panelMinWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    padding: Tk.Theme.space.md
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: model ? qsTr("%1 transfer points, %2 to %3 %4")
        .arg(model.points.length).arg(model.domainMin).arg(model.domainMax).arg(model.unit) : qsTr("No transfer function")
    Tk.ToolTip { text: control.tooltip; visible: hover.hovered }
    HoverHandler { id: hover }

    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Label { text: qsTr("Transfer function"); Tk.Flex.grow: 1 }
            Tk.Mono { text: control.model ? control.model.unit : "" }
            Tk.ComboBox {
                id: presetPicker
                objectName: "transferPreset"
                small: true
                enabled: !!control.model
                tooltip: qsTr("Apply a built-in transfer preset")
                textRole: "name"
                model: control.model ? [{id: "", name: qsTr("Custom")}].concat(control.model.presets()) : []
                currentIndex: {
                    if (!control.model) return -1
                    const id = control.model.presetId
                    const index = model.findIndex(function(entry) { return entry.id === id })
                    return Math.max(0, index)
                }
                onActivated: function(index) {
                    const entry = model[index]
                    if (control.model && entry && entry.id.length > 0) control.model.applyPreset(entry.id)
                }
            }
            Tk.IconButton {
                objectName: "transferUndo"
                iconName: "undo"
                small: true
                enabled: !!control.model && control.model.canUndo
                tooltip: control.model && control.model.undoText.length > 0
                    ? qsTr("Undo %1").arg(control.model.undoText) : qsTr("Undo")
                onClicked: control.undo()
            }
            Tk.IconButton {
                objectName: "transferRedo"
                iconName: "redo"
                small: true
                enabled: !!control.model && control.model.canRedo
                tooltip: control.model && control.model.redoText.length > 0
                    ? qsTr("Redo %1").arg(control.model.redoText) : qsTr("Redo")
                onClicked: control.redo()
            }
        }
        Item {
            id: histogramStrip
            objectName: "transferHistogram"
            visible: !!control.histogram && !!control.histogram.bins
            implicitHeight: Tk.Theme.size.waveformHeight
            readonly property var bins: visible ? control.histogram.bins : []
            readonly property real largest: visible ? Math.max(1, control.histogram.largest) : 1
            Accessible.role: Accessible.Chart
            Accessible.name: qsTr("Value histogram")
            Repeater {
                model: histogramStrip.bins.length
                Rectangle {
                    required property int index
                    objectName: "transferHistogramBar"
                    x: index * histogramStrip.width / histogramStrip.bins.length
                    width: Math.max(Tk.Theme.size.border, histogramStrip.width / histogramStrip.bins.length)
                    // Log-scaled: a background bin must not flatten the rest.
                    height: histogramStrip.height * Math.log(1 + histogramStrip.bins[index])
                            / Math.log(1 + histogramStrip.largest)
                    y: histogramStrip.height - height
                    color: Tk.Theme.color.textMuted
                    opacity: Tk.Theme.opacity.muted
                }
            }
        }
        Tk.GradientEditor {
            id: gradient
            objectName: "transferGradient"
            enabled: !!control.model
            tooltip: qsTr("Select, add, move or remove a transfer point")
            selectedStopId: control.selectedPointId
            stops: control.model ? control.model.points.map(function(point) {
                return {id: point.id, position: (point.value - control.model.domainMin)
                    / (control.model.domainMax - control.model.domainMin), color: point.color}
            }) : []
            previewSamples: {
                if (!control.model) return []
                const revision = control.model.revision
                return revision >= 0 ? control.model.previewColors(96) : []
            }
            onSelectionRequested: function(id) { control.selectedPointId = String(id) }
            onAddStopRequested: function(position) {
                if (!control.model) return
                const value = control.model.domainMin + position * (control.model.domainMax - control.model.domainMin)
                // A new stop takes the current mapping, so adding one never
                // changes the rendered result until it is edited.
                const id = control.model.addPoint(value, control.model.colorAt(value), control.model.opacityAt(value))
                if (id.length > 0) control.selectedPointId = id
            }
            // A drag, or a keyboard nudge (preview then finish), previews
            // inside one transaction and commits it once on finish.
            onMovePreviewRequested: function(id, position) {
                if (!control._beginGesture(qsTr("Move transfer point"))) return
                control.model.movePoint(String(id), control._valueAt(position))
            }
            onMoveFinished: function(id, position) {
                if (!control.model) return
                if (control.model.editing && control._gestureModel !== control.model) return
                control.model.movePoint(String(id), control._valueAt(position))
                control._commitGesture()
            }
            onMoveCanceled: function(id) { control._cancelGesture() }
            onDeleteStopRequested: function(id) {
                if (control.model && control.model.removePoint(String(id))) control.selectedPointId = ""
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Caption { text: qsTr("Value") }
            Tk.NumberField {
                id: pointValue
                objectName: "transferPointValue"
                Tk.Flex.grow: 1
                enabled: control._innerSelected
                value: control.selectedPoint.value || 0
                from: control.model ? control.model.domainMin : 0
                to: control.model ? control.model.domainMax : 1
                decimals: Math.max(0, 3 - control._magnitude)
                stepSize: Math.pow(10, control._magnitude - 2)
                tooltip: qsTr("Selected transfer value")
                onValueModified: function(value) {
                    if (control.model && !control.model.movePoint(control.selectedPointId, value))
                        control._syncFields()
                }
            }
            Tk.Caption { text: qsTr("Opacity") }
            Tk.Slider {
                id: pointOpacity
                objectName: "transferPointOpacity"
                Tk.Flex.grow: 1
                enabled: !!control.selectedPoint.id
                from: 0; to: 1; decimals: 2; showValue: true
                value: control.selectedPoint.opacity || 0
                tooltip: qsTr("Selected transfer opacity")
                // One press-drag-release of the handle is one history step.
                onPressedChanged: {
                    if (pointOpacity.pressed) control._beginGesture(qsTr("Change transfer opacity"))
                    else control._commitGesture()
                }
                onValueModified: function(value) {
                    if (control.model && !control.model.setPointOpacity(control.selectedPointId, value))
                        control._syncFields()
                }
            }
            Tk.ColorButton {
                id: pointColor
                objectName: "transferPointColor"
                enabled: !!control.selectedPoint.id
                tooltip: qsTr("Selected transfer color")
                color: control.selectedPoint.color || Tk.Theme.color.text
                // ColorSwatches reads strings or {name, color}; a bare color
                // value has no .color and would paint an undefined swatch.
                colors: control._rangeColors
                onColorSelected: function(color) {
                    if (control.model && !control.model.setPointColor(control.selectedPointId, color))
                        control._syncFields()
                }
                onCustomRequested: control.customColorRequested(control.selectedPointId)
            }
        }
        Tk.Flex {
            objectName: "transferRanges"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Repeater {
                model: ["below", "above", "missing"]
                Tk.Flex {
                    id: rangeCell
                    required property string modelData
                    readonly property var range: {
                        if (!control.model) return ({})
                        const revision = control.model.revision
                        return revision < 0 ? ({}) : modelData === "below" ? control.model.belowRange
                            : modelData === "above" ? control.model.aboveRange : control.model.missingValue
                    }
                    Tk.Flex.grow: 1
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    Tk.Caption {
                        text: rangeCell.modelData === "below" ? qsTr("Below")
                            : rangeCell.modelData === "above" ? qsTr("Above") : qsTr("Missing")
                    }
                    Tk.ColorButton {
                        objectName: "transferRange_" + rangeCell.modelData
                        small: true
                        enabled: !!control.model
                        color: rangeCell.range.color || Tk.Theme.color.text
                        colors: control._rangeColors
                        tooltip: control._rangeTooltip(rangeCell.modelData, rangeCell.range)
                        onColorSelected: function(color) {
                            if (control.model) control.model.setRangeColor(rangeCell.modelData, color, 1)
                        }
                    }
                    Tk.IconButton {
                        objectName: "transferRangeReset_" + rangeCell.modelData
                        small: true
                        iconName: rangeCell.modelData === "missing" ? "eye-off" : "unlink"
                        enabled: !!control.model && (rangeCell.modelData === "missing"
                            ? rangeCell.range.opacity > 0 : rangeCell.range.mode === "color")
                        tooltip: rangeCell.modelData === "missing" ? qsTr("Make missing values transparent")
                            : qsTr("Clamp to the end stop")
                        onClicked: {
                            if (rangeCell.modelData === "missing")
                                control.model.setRangeColor("missing", rangeCell.range.color, 0)
                            else
                                control.model.clampRange(rangeCell.modelData)
                        }
                    }
                    Tk.Mono {
                        objectName: "transferRangeCount_" + rangeCell.modelData
                        visible: !!control.histogram && control.histogram[rangeCell.modelData] !== undefined
                        text: visible ? String(control.histogram[rangeCell.modelData]) : ""
                    }
                }
            }
        }
    }
}
