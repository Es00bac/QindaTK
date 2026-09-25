// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Edits a Tk.TransferFunctionModel: measured-value stops on a gradient over
// the model's domain (click the track to add at the current mapping, drag or
// arrow-key a stop to move it, Delete or right-click to remove) plus the
// selected stop's exact value, opacity and color. Sized to its content.
// A drag or slider press is one undoable step (Escape cancels a drag);
// Ctrl+Z / Ctrl+Shift+Z and the header buttons undo and redo.
// AGENT-CONTRACT (docs/volume-imaging.md): edits write only the transfer
// settings model. Voxel source data and its revision are never changed here.
// Gestures use the model's beginEdit/commitEdit/cancelEdit transaction
// (D-148), so previews repaint views but only the committed step is recorded.
T.Control {
    id: control
    objectName: "transferFunctionEditor"
    property Tk.TransferFunctionModel model: null
    property string selectedPointId: ""
    property string tooltip: qsTr("Transfer function")
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

    Keys.onPressed: function(event) {
        if (event.matches(StandardKey.Undo)) {
            event.accepted = control.undo()
        } else if (event.matches(StandardKey.Redo)) {
            event.accepted = control.redo()
        } else if (event.key === Qt.Key_Escape && control.model && control.model.editing) {
            event.accepted = control.model.cancelEdit()
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
                if (!control.model) return
                if (!control.model.editing && !control.model.beginEdit(qsTr("Move transfer point"))) return
                control.model.movePoint(String(id), control._valueAt(position))
            }
            onMoveFinished: function(id, position) {
                if (!control.model) return
                control.model.movePoint(String(id), control._valueAt(position))
                if (control.model.editing) control.model.commitEdit()
            }
            onMoveCanceled: function(id) {
                if (control.model && control.model.editing) control.model.cancelEdit()
            }
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
                    if (!control.model) return
                    if (pointOpacity.pressed && !control.model.editing)
                        control.model.beginEdit(qsTr("Change transfer opacity"))
                    else if (!pointOpacity.pressed && control.model.editing)
                        control.model.commitEdit()
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
                colors: [{name: qsTr("Text"), color: Tk.Theme.color.text},
                         {name: qsTr("Accent"), color: Tk.Theme.color.accent},
                         {name: qsTr("Warning"), color: Tk.Theme.color.warning},
                         {name: qsTr("Danger"), color: Tk.Theme.color.danger},
                         {name: qsTr("Success"), color: Tk.Theme.color.success}]
                onColorSelected: function(color) {
                    if (control.model && !control.model.setPointColor(control.selectedPointId, color))
                        control._syncFields()
                }
                onCustomRequested: control.customColorRequested(control.selectedPointId)
            }
        }
    }
}
