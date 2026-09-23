// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: evaluator returns canonical numeric values and source
// offsets; PropertyBinding owns the only document transaction. Bad drafts
// never change the last committed value.
T.Control {
    id: control
    property Tk.PropertyBinding binding: null
    property Tk.LocaleService localeService: null
    property var symbols: ({})
    property var units: ({})
    property string label: ""
    property string tooltip: ""
    property string placeholderText: qsTr("Expression")
    property string draft: ""
    property string errorMessage: ""
    property int errorOffset: -1
    readonly property real evaluatedValue: binding && !binding.mixed ? Number(binding.value) : 0
    readonly property bool readOnly: !binding || !binding.writable
    readonly property alias inputItem: input
    signal previewed(real canonicalValue, string dimension)
    signal expressionCommitted(string expression, real canonicalValue, string dimension)

    implicitWidth: input.implicitWidth
    implicitHeight: input.implicitHeight
    padding: 0
    Accessible.role: Accessible.EditableText
    Accessible.name: tooltip || label || placeholderText
    Accessible.description: errorMessage

    Tk.ExpressionEvaluator {
        id: evaluator
        localeService: control.localeService
        symbols: control.symbols
        units: control.units
    }
    function formatCommitted() {
        if (!binding || binding.mixed) return ""
        return localeService ? localeService.formatNumber(Number(binding.value)) : String(binding.value)
    }
    function syncCommitted() {
        if (!input.activeFocus) {
            draft = formatCommitted()
        }
    }
    function previewDraft() {
        let result = evaluator.evaluate(draft)
        const canonicalUnit = binding && binding.descriptor ? binding.descriptor.unitId : ""
        const expectedDimension = canonicalUnit && units[canonicalUnit] ? units[canonicalUnit].dimension : ""
        if (result.ok && result.unit && expectedDimension && result.unit !== expectedDimension)
            result = {ok: false, error: qsTr("Unit is incompatible with property"), offset: 0, length: draft.length}
        errorMessage = result.ok ? "" : result.error
        errorOffset = result.ok ? -1 : result.offset
        if (result.ok) previewed(result.value, result.unit)
        return result
    }
    function commitDraft() {
        if (readOnly) return false
        const result = previewDraft()
        if (!result.ok || !binding.begin()) return false
        if (!binding.update(result.value)) { binding.cancel(); errorMessage = binding.validationError; return false }
        if (!binding.commit()) { errorMessage = binding.validationError; return false }
        expressionCommitted(draft, result.value, result.unit)
        return true
    }
    Component.onCompleted: syncCommitted()
    Connections {
        target: control.binding
        function onChanged() { control.syncCommitted() }
    }
    contentItem: Tk.TextField {
        id: input
        objectName: "expressionInput"
        text: control.draft
        mono: true
        readOnly: control.readOnly
        error: control.errorMessage.length > 0 || (control.binding && control.binding.validationError.length > 0)
        placeholderText: control.placeholderText
        tooltip: control.tooltip || control.label
        onTextEdited: { control.draft = text; control.errorMessage = ""; control.errorOffset = -1 }
        onAccepted: control.commitDraft()
        Keys.onEscapePressed: function(event) {
            control.errorMessage = ""
            control.errorOffset = -1
            control.draft = control.formatCommitted()
            event.accepted = true
        }
    }
}
