// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/volume-imaging.md): edits write only the transfer
// settings model. Voxel source data and its revision are never changed here.
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
    onSelectedPointChanged: {
        if (pointValue) pointValue.value = selectedPoint.value || 0
        if (pointOpacity) pointOpacity.value = selectedPoint.opacity || 0
        if (pointColor) pointColor.color = selectedPoint.color || Tk.Theme.color.text
    }
    signal customColorRequested(string pointId)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.waveformHeight + Tk.Theme.size.controlLg + Tk.Theme.space.lg
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
            Tk.Label { text: qsTr("Transfer function"); Tk.Flex.grow: 1 }
            Tk.Mono { text: control.model ? control.model.unit : "" }
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
                control.selectedPointId = control.model.addPoint(value, control.model.colorAt(value), 0.5)
            }
            onMoveFinished: function(id, position) {
                if (!control.model) return
                const value = control.model.domainMin + position * (control.model.domainMax - control.model.domainMin)
                control.model.movePoint(String(id), value)
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
                enabled: !!control.selectedPoint.id && control.selectedPoint.value > control.model.domainMin
                    && control.selectedPoint.value < control.model.domainMax
                value: control.selectedPoint.value || 0
                from: control.model ? control.model.domainMin : 0
                to: control.model ? control.model.domainMax : 1
                decimals: 2
                tooltip: qsTr("Selected transfer value")
                onValueModified: function(value) { if (control.model) control.model.movePoint(control.selectedPointId, value) }
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
                onValueModified: function(value) { if (control.model) control.model.setPointOpacity(control.selectedPointId, value) }
            }
            Tk.ColorButton {
                id: pointColor
                objectName: "transferPointColor"
                enabled: !!control.selectedPoint.id
                tooltip: qsTr("Selected transfer color")
                color: control.selectedPoint.color || Tk.Theme.color.text
                colors: [Tk.Theme.color.text, Tk.Theme.color.accent, Tk.Theme.color.warning,
                         Tk.Theme.color.danger, Tk.Theme.color.success]
                onColorSelected: function(color) { if (control.model) control.model.setPointColor(control.selectedPointId, color) }
                onCustomRequested: control.customColorRequested(control.selectedPointId)
            }
        }
    }
}
