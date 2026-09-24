// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#datavalidationeditor, OFF-18): edits the
// data-validation rules of one sheet of a Tk.Workbook: ranges, a typed
// constraint (whole number, decimal, list items or a source range, text
// length, custom formula), the blank policy, the input message and the
// invalid-value style (stop refuses; warning/information ask). apply()
// commits one rule as one undoable step; a new rule takes its cells from
// older ones (Workbook.setValidation). `pastePolicy` is the host's paste
// policy (bind it to SheetView.pastePolicy): "allOrNothing" or "skipInvalid".
T.Control {
    id: editor

    property Tk.Workbook workbook: null
    property string sheetId: ""
    property string tooltip: qsTr("Data validation")
    property string pastePolicy: "allOrNothing"
    // Draft rule (the form).
    property string ruleId: ""
    property string ranges: ""
    property string type: "wholeNumber"
    property string operator: "between"
    property string value1: ""
    property string value2: ""
    property string items: ""
    property bool allowBlank: true
    property string inputTitle: ""
    property string inputMessage: ""
    property string errorStyle: "stop"
    property string errorTitle: ""
    property string errorMessage: ""
    property int epoch: 0
    readonly property var rules: epoch >= 0 && workbook && sheetId ? workbook.validations(sheetId) : []
    readonly property bool bounded: type === "wholeNumber" || type === "decimal" || type === "textLength"
    readonly property bool twoValues: bounded && (operator === "between" || operator === "notBetween")
    readonly property var itemList: items.split(/[\n,]/).map(function(item) { return item.trim() })
                                         .filter(function(item) { return item.length > 0 })
    readonly property var draft: buildDraft()
    readonly property var check: epoch >= 0 && workbook && sheetId ? workbook.checkValidation(sheetId, draft)
                                                                   : ({ ok: false, message: "" })
    readonly property var typeModel: [
        { text: qsTr("Any value"), value: "any" }, { text: qsTr("Whole number"), value: "wholeNumber" },
        { text: qsTr("Decimal"), value: "decimal" }, { text: qsTr("List"), value: "list" },
        { text: qsTr("Text length"), value: "textLength" }, { text: qsTr("Custom formula"), value: "custom" }]
    readonly property var operatorModel: [
        { text: qsTr("between"), value: "between" }, { text: qsTr("not between"), value: "notBetween" },
        { text: qsTr("equal to"), value: "equal" }, { text: qsTr("not equal to"), value: "notEqual" },
        { text: qsTr("greater than"), value: "greater" }, { text: qsTr("less than"), value: "less" },
        { text: qsTr("at least"), value: "greaterOrEqual" }, { text: qsTr("at most"), value: "lessOrEqual" }]
    readonly property var styleModel: [{ text: qsTr("Stop"), value: "stop" }, { text: qsTr("Warning"), value: "warning" },
                                       { text: qsTr("Information"), value: "information" }]
    readonly property var policyModel: [{ text: qsTr("All or nothing"), value: "allOrNothing" },
                                        { text: qsTr("Skip invalid cells"), value: "skipInvalid" }]

    signal applied(string ruleId)

    implicitWidth: Tk.Theme.size.dialogWidth
    implicitHeight: body.implicitHeight + topPadding + bottomPadding
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip

    function indexOfValue(model, value) {
        for (let i = 0; i < model.length; ++i) if (model[i].value === value) return i
        return 0
    }
    function summary(rule) {
        let what = typeModel[indexOfValue(typeModel, rule.type)].text
        if (rule.type === "list") what += rule.items.length > 0 ? ": " + rule.items.join(", ") : ": " + rule.value1
        else if (rule.type === "custom") what += ": " + rule.value1
        else if (rule.type !== "any")
            what += " " + operatorModel[indexOfValue(operatorModel, rule.operator)].text + " " + rule.value1
                    + (rule.operator === "between" || rule.operator === "notBetween" ? qsTr(" and %1").arg(rule.value2) : "")
        return qsTr("%1 — %2").arg(what).arg(rule.ranges.join(" "))
    }
    function buildDraft() {
        const rule = { ranges: ranges.split(/[\s,;]+/).filter(function(part) { return part.length > 0 }), type: type,
                       allowBlank: allowBlank, inputTitle: inputTitle, inputMessage: inputMessage,
                       errorStyle: errorStyle, errorTitle: errorTitle, errorMessage: errorMessage }
        if (ruleId.length > 0) rule.id = ruleId
        if (bounded) { rule.operator = operator; rule.value1 = value1; if (twoValues) rule.value2 = value2 }
        if (type === "custom") rule.value1 = value1
        if (type === "list") {
            if (itemList.length > 0) rule.items = itemList
            else rule.value1 = value1
        }
        return rule
    }
    function newRule(target) {
        ruleId = ""
        ranges = target || ""
        type = "wholeNumber"; operator = "between"; value1 = ""; value2 = ""; items = ""
        allowBlank = true; inputTitle = ""; inputMessage = ""
        errorStyle = "stop"; errorTitle = ""; errorMessage = ""
    }
    function load(id) {
        const rule = rules.find(function(candidate) { return candidate.id === id })
        if (!rule) return false
        ruleId = rule.id
        ranges = rule.ranges.join(" ")
        type = rule.type; operator = rule.operator; value1 = rule.value1; value2 = rule.value2
        items = rule.items.join("\n")
        allowBlank = rule.allowBlank
        inputTitle = rule.inputTitle; inputMessage = rule.inputMessage
        errorStyle = rule.errorStyle; errorTitle = rule.errorTitle; errorMessage = rule.errorMessage
        return true
    }
    function apply() {
        if (!workbook || !check.ok) return ""
        const id = workbook.setValidation(sheetId, draft)
        epoch += 1
        if (id.length > 0) { ruleId = id; applied(id) }
        return id
    }
    function remove(id) {
        const ok = !!workbook && workbook.removeValidation(sheetId, id)
        epoch += 1
        if (ok && id === ruleId) newRule(ranges)
        return ok
    }

    Connections {
        target: editor.workbook
        function onRevisionChanged() { editor.epoch += 1 }
        function onSheetsChanged() { editor.epoch += 1 }
    }

    contentItem: Tk.Flex {
        id: body
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.SectionHeader { title: editor.tooltip; count: String(editor.rules.length) }
        Tk.SheetRuleList {
            objectName: "dvRuleList"
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            rules: editor.rules
            currentId: editor.ruleId
            summary: editor.summary
            tooltip: qsTr("Validation rules")
            onSelected: function(ruleId) { editor.load(ruleId) }
            onRemoveRequested: function(ruleId) { editor.remove(ruleId) }
        }
        Tk.Button { objectName: "dvNew"; text: qsTr("New rule"); small: true; onClicked: editor.newRule(editor.ranges) }
        Tk.Grid {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            columns: "auto 1fr"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.xs
            Tk.Label { text: qsTr("Applies to"); color: Tk.Theme.color.textMuted }
            Tk.TextField {
                objectName: "dvRanges"; small: true; text: editor.ranges; placeholderText: qsTr("A1:A20")
                tooltip: qsTr("Ranges the rule checks, separated by spaces")
                onTextEdited: editor.ranges = text
            }
            Tk.Label { text: qsTr("Allow"); color: Tk.Theme.color.textMuted }
            Tk.ComboBox {
                objectName: "dvType"; small: true; model: editor.typeModel; textRole: "text"; valueRole: "value"
                currentIndex: editor.indexOfValue(editor.typeModel, editor.type)
                tooltip: qsTr("Kind of value allowed")
                onActivated: editor.type = currentValue
            }
            Tk.Label { visible: editor.bounded; text: qsTr("Which is"); color: Tk.Theme.color.textMuted }
            Tk.ComboBox {
                objectName: "dvOperator"; visible: editor.bounded; small: true; model: editor.operatorModel
                textRole: "text"; valueRole: "value"
                currentIndex: editor.indexOfValue(editor.operatorModel, editor.operator)
                tooltip: qsTr("Comparison")
                onActivated: editor.operator = currentValue
            }
            Tk.Label {
                visible: editor.bounded || editor.type === "custom" || (editor.type === "list" && editor.itemList.length === 0)
                text: editor.type === "custom" ? qsTr("Formula") : editor.type === "list" ? qsTr("Source") : qsTr("Value")
                color: Tk.Theme.color.textMuted
            }
            Tk.TextField {
                objectName: "dvValue1"; small: true; mono: true; text: editor.value1
                visible: editor.bounded || editor.type === "custom" || (editor.type === "list" && editor.itemList.length === 0)
                placeholderText: editor.type === "custom" ? "=A1>0" : editor.type === "list" ? "=$D$1:$D$9" : qsTr("Value or =formula")
                tooltip: qsTr("Value or formula, relative to the first cell of the first range")
                onTextEdited: editor.value1 = text
            }
            Tk.Label { visible: editor.twoValues; text: qsTr("and"); color: Tk.Theme.color.textMuted }
            Tk.TextField {
                objectName: "dvValue2"; visible: editor.twoValues; small: true; mono: true; text: editor.value2
                tooltip: qsTr("Second value of the range")
                onTextEdited: editor.value2 = text
            }
            Tk.Label { visible: editor.type === "list"; text: qsTr("Items"); color: Tk.Theme.color.textMuted }
            Tk.TextArea {
                objectName: "dvItems"; visible: editor.type === "list"; text: editor.items
                placeholderText: qsTr("One per line, or comma separated")
                tooltip: qsTr("Allowed values")
                onTextChanged: if (activeFocus) editor.items = text
            }
            Tk.Label { text: ""; visible: editor.type !== "any" }
            Tk.CheckBox {
                objectName: "dvAllowBlank"; visible: editor.type !== "any"; small: true; text: qsTr("Allow blank cells")
                checked: editor.allowBlank
                onToggled: editor.allowBlank = checked
            }
            Tk.Label { text: qsTr("Input title"); color: Tk.Theme.color.textMuted }
            Tk.TextField {
                objectName: "dvInputTitle"; small: true; text: editor.inputTitle
                tooltip: qsTr("Title shown when a checked cell is selected")
                onTextEdited: editor.inputTitle = text
            }
            Tk.Label { text: qsTr("Input message"); color: Tk.Theme.color.textMuted }
            Tk.TextField {
                objectName: "dvInputMessage"; small: true; text: editor.inputMessage
                tooltip: qsTr("Message shown when a checked cell is selected")
                onTextEdited: editor.inputMessage = text
            }
            Tk.Label { visible: editor.type !== "any"; text: qsTr("On invalid"); color: Tk.Theme.color.textMuted }
            Tk.Segmented {
                objectName: "dvErrorStyle"; visible: editor.type !== "any"; small: true; model: editor.styleModel
                currentIndex: editor.indexOfValue(editor.styleModel, editor.errorStyle)
                tooltip: qsTr("Stop refuses the value; warning and information ask first")
                onActivated: function(index) { editor.errorStyle = editor.styleModel[index].value }
            }
            Tk.Label { visible: editor.type !== "any"; text: qsTr("Error title"); color: Tk.Theme.color.textMuted }
            Tk.TextField {
                objectName: "dvErrorTitle"; visible: editor.type !== "any"; small: true; text: editor.errorTitle
                tooltip: qsTr("Title of the invalid-value message")
                onTextEdited: editor.errorTitle = text
            }
            Tk.Label { visible: editor.type !== "any"; text: qsTr("Error message"); color: Tk.Theme.color.textMuted }
            Tk.TextField {
                objectName: "dvErrorMessage"; visible: editor.type !== "any"; small: true; text: editor.errorMessage
                tooltip: qsTr("Message for an invalid value (empty: a message built from the rule)")
                onTextEdited: editor.errorMessage = text
            }
            Tk.Label { text: qsTr("When pasting"); color: Tk.Theme.color.textMuted }
            Tk.Segmented {
                objectName: "dvPastePolicy"; small: true; model: editor.policyModel
                currentIndex: editor.indexOfValue(editor.policyModel, editor.pastePolicy)
                tooltip: qsTr("A paste with invalid cells applies nothing, or only the valid cells")
                onActivated: function(index) { editor.pastePolicy = editor.policyModel[index].value }
            }
        }
        Tk.Caption {
            objectName: "dvError"
            visible: !editor.check.ok && String(editor.check.message || "").length > 0
            text: String(editor.check.message || "")
            color: Tk.Theme.color.danger
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            justify: Tk.Flex.End
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            Tk.Button {
                objectName: "dvApply"
                text: editor.ruleId.length > 0 ? qsTr("Update rule") : qsTr("Add rule")
                variant: "accent"
                available: editor.check.ok === true
                onClicked: editor.apply()
            }
        }
    }
}
