// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Numeric display boundary for canonical values. displayUnit entries use
// {id, label, scale, offset}: display = (canonical - offset) / scale.
Item {
    id: field
    property Tk.PropertyBinding binding: null
    property string label: ""
    property string tooltip: ""
    property string displayUnit: ""
    property var units: []
    property real unitScale: 1
    property real unitOffset: 0
    property int precision: 2
    property real from: -1e9
    property real to: 1e9
    property real stepSize: 1
    property bool small: false
    property bool linked: false
    readonly property bool conversionValid: isFinite(unitScale) && unitScale !== 0 && isFinite(unitOffset)
    readonly property real canonicalValue: binding && !binding.mixed && binding.value !== undefined ? Number(binding.value) : 0
    readonly property real displayValue: conversionValid ? (canonicalValue - unitOffset) / unitScale : 0
    readonly property bool mixed: binding ? binding.mixed : false
    readonly property bool readOnly: !binding || !binding.writable || !conversionValid
    signal valueModified(real canonicalValue)

    implicitWidth: numberField.implicitWidth
    implicitHeight: numberField.implicitHeight
    Accessible.role: Accessible.SpinBox
    Accessible.name: tooltip || label
    Accessible.description: mixed ? qsTr("Multiple values") : conversionValid ? displayUnit : qsTr("Invalid unit conversion")
    opacity: readOnly ? Tk.Theme.opacity.disabled : 1.0

    function canonical(display) { return display * unitScale + unitOffset }
    function setUnit(unit) {
        for (let i = 0; i < units.length; ++i) {
            if (units[i].id === unit) {
                if (!isFinite(Number(units[i].scale)) || Number(units[i].scale) === 0 || !isFinite(Number(units[i].offset))) return false
                displayUnit = unit
                unitScale = Number(units[i].scale)
                unitOffset = Number(units[i].offset)
                return true
            }
        }
        return false
    }
    function edit(value) {
        if (readOnly || !isFinite(value)) return false
        const next = canonical(value)
        if (!isFinite(next) || !binding.begin()) return false
        if (!binding.update(next)) { binding.cancel(); return false }
        const ok = binding.commit()
        if (ok) valueModified(next)
        return ok
    }

    Tk.NumberField {
        id: numberField
        anchors.fill: parent
        value: field.displayValue
        mixed: field.mixed
        from: field.from
        to: field.to
        stepSize: field.stepSize
        decimals: field.precision
        small: field.small
        editable: !field.readOnly
        error: field.binding ? field.binding.validationError.length > 0 : !field.conversionValid
        suffix: field.displayUnit
        tooltip: field.tooltip || field.label
        onValueModified: function(value) { field.edit(value) }
    }
}
