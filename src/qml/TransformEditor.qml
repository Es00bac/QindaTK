// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Transform parts remain separately typed properties. A host adapter owns
// coordinate conventions and any matrix decomposition policy.
Item {
    id: editor
    property Tk.PropertyBinding translationBinding: null
    property Tk.PropertyBinding rotationBinding: null
    property Tk.PropertyBinding scaleBinding: null
    property Tk.PropertyBinding pivotBinding: null
    property Tk.PropertyBinding boundsBinding: null
    property string tooltip: ""
    property bool small: false
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip || qsTr("Transform")
    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.PropertyRow { label: qsTr("Position"); Tk.VectorField { binding: editor.translationBinding; tooltip: qsTr("Position") } }
        Tk.PropertyRow { label: qsTr("Rotation"); Tk.UnitField { binding: editor.rotationBinding; label: qsTr("Rotation"); displayUnit: "°"; unitScale: 1; precision: 1 } }
        Tk.PropertyRow { label: qsTr("Scale"); Tk.VectorField { binding: editor.scaleBinding; tooltip: qsTr("Scale"); precision: 3; stepSize: 0.1 } }
        Tk.PropertyRow { label: qsTr("Pivot"); Tk.VectorField { binding: editor.pivotBinding; tooltip: qsTr("Pivot") } }
        Tk.PropertyRow {
            visible: editor.boundsBinding !== null
            label: qsTr("Bounds")
            Tk.Label { text: editor.boundsBinding && !editor.boundsBinding.mixed ? String(editor.boundsBinding.value) : qsTr("Multiple values") }
        }
    }
}
