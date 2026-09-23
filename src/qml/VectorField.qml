// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Edits one component through PropertyBinding.updateComponent so each target
// retains its own untouched components, including mixed selections.
Item {
    id: field
    property Tk.PropertyBinding binding: null
    property var componentLabels: ["X", "Y", "Z"]
    property var componentUnits: []
    property int precision: 2
    property real stepSize: 1
    property bool small: false
    property string tooltip: ""
    readonly property var componentValue: binding ? binding.value : null
    readonly property int componentCount: componentValue && componentValue.length !== undefined ? componentValue.length : componentLabels.length
    readonly property bool readOnly: !binding || !binding.writable
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip || qsTr("Vector")
    Accessible.description: binding && binding.mixed ? qsTr("Multiple vectors") : ""
    opacity: readOnly ? Tk.Theme.opacity.disabled : 1.0
    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    function editComponent(index, value) {
        if (!binding || !binding.writable || !isFinite(value) || index < 0 || index >= componentCount) return false
        if (typeof binding.updateComponent !== "function") return false
        if (!binding.begin()) return false
        if (!binding.updateComponent(index, value)) { binding.cancel(); return false }
        return binding.commit()
    }
    function componentAt(index) {
        if (!componentValue) return 0
        if (componentValue.length !== undefined) return Number(componentValue[index])
        const keys = ["x", "y", "z", "w"]
        return Number(componentValue[keys[index]])
    }

    Tk.Flex {
        id: row
        anchors.fill: parent
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        Repeater {
            model: field.componentCount
            delegate: Tk.NumberField {
                objectName: "vectorComponent_" + index
                Tk.Flex.grow: 1
                label: field.componentLabels[index] || String(index + 1)
                tooltip: field.tooltip.length ? field.tooltip + " " + label : label
                value: field.binding && !field.binding.mixed ? field.componentAt(index) : 0
                mixed: field.binding ? field.binding.mixed : false
                decimals: field.precision
                stepSize: field.stepSize
                small: field.small
                editable: field.binding ? field.binding.writable : false
                suffix: field.componentUnits[index] || ""
                onValueModified: function(value) { field.editComponent(index, value) }
            }
        }
    }
}
