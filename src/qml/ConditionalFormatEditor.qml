// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#conditionalformateditor, OFF-17): edits the
// conditional formats of one sheet of a Tk.Workbook. The rule list is in
// priority order (the first rule applies first); the form edits one draft
// rule map, checked by Workbook.checkConditionalFormat and previewed by
// Workbook.previewConditionalFormat over the first cells of its first range
// through the sheet's own cell renderer; apply() commits it as one undoable
// step. The Workbook evaluates rules; themes style this editor, the rule's
// document colors style cells.
T.Control {
    id: editor

    property Tk.Workbook workbook: null
    property string sheetId: ""
    property string tooltip: qsTr("Conditional formats")
    // Document colors offered for text, fill and scales (stored as #rrggbb).
    property var documentColors: [
        { name: qsTr("Danger tint"), color: opaque(Tk.Theme.color.dangerSubtle) },
        { name: qsTr("Danger"), color: opaque(Tk.Theme.color.danger) },
        { name: qsTr("Warning"), color: opaque(Tk.Theme.color.warning) },
        { name: qsTr("Success"), color: opaque(Tk.Theme.color.success) },
        { name: qsTr("Info"), color: opaque(Tk.Theme.color.info) },
        { name: qsTr("Accent tint"), color: opaque(Tk.Theme.color.accentSubtle) },
        { name: qsTr("Accent"), color: opaque(Tk.Theme.color.accent) },
        { name: qsTr("Panel"), color: opaque(Tk.Theme.color.panel) }]
    // Draft rule (the form).
    property string ruleId: ""
    property string ranges: ""
    property string type: "cellValue"
    property string operator: "greater"
    property string value1: ""
    property string value2: ""
    property string text: ""
    property var style: ({})
    property string minColor: ""
    property string midColor: ""
    property string maxColor: ""
    property bool stopIfTrue: false
    property int epoch: 0
    readonly property var rules: epoch >= 0 && workbook && sheetId ? workbook.conditionalFormats(sheetId) : []
    readonly property bool compares: type === "cellValue"
    readonly property bool twoValues: compares && (operator === "between" || operator === "notBetween")
    readonly property bool styled: ["colorScale", "dataBar", "iconSet"].indexOf(type) < 0
    readonly property var draft: buildDraft()
    readonly property var check: epoch >= 0 && workbook && sheetId ? workbook.checkConditionalFormat(sheetId, draft)
                                                                   : ({ ok: false, message: "" })
    readonly property var window: previewWindow()
    readonly property var preview: check.ok && window ? workbook.previewConditionalFormat(sheetId, draft,
        window.firstRow, window.firstColumn, window.rows, window.columns) : []
    readonly property var typeModel: [
        { text: qsTr("Cell value"), value: "cellValue" }, { text: qsTr("Formula is true"), value: "expression" },
        { text: qsTr("Contains text"), value: "containsText" }, { text: qsTr("Blank"), value: "blank" },
        { text: qsTr("Not blank"), value: "notBlank" }, { text: qsTr("Error"), value: "errors" },
        { text: qsTr("No error"), value: "noErrors" }, { text: qsTr("Color scale"), value: "colorScale" },
        { text: qsTr("Data bar"), value: "dataBar" }, { text: qsTr("Icon set"), value: "iconSet" }]
    readonly property var operatorModel: [
        { text: qsTr("between"), value: "between" }, { text: qsTr("not between"), value: "notBetween" },
        { text: qsTr("equal to"), value: "equal" }, { text: qsTr("not equal to"), value: "notEqual" },
        { text: qsTr("greater than"), value: "greater" }, { text: qsTr("less than"), value: "less" },
        { text: qsTr("at least"), value: "greaterOrEqual" }, { text: qsTr("at most"), value: "lessOrEqual" }]

    signal applied(string ruleId)

    implicitWidth: Tk.Theme.size.dialogWidth
    implicitHeight: body.implicitHeight + topPadding + bottomPadding
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip

    function opaque(color) {
        const p = Tk.Theme.color.panel, a = color.a
        return Qt.rgba(color.r * a + p.r * (1 - a), color.g * a + p.g * (1 - a), color.b * a + p.b * (1 - a), 1).toString()
    }
    function indexOfValue(model, value) {
        for (let i = 0; i < model.length; ++i) if (model[i].value === value) return i
        return 0
    }
    function nameOf(model, value) { return model[indexOfValue(model, value)].text }
    function summary(rule) {
        let condition = nameOf(typeModel, rule.type)
        if (rule.type === "cellValue")
            condition = qsTr("Value %1 %2").arg(nameOf(operatorModel, rule.operator)).arg(rule.value1)
                        + (rule.operator === "between" || rule.operator === "notBetween" ? qsTr(" and %1").arg(rule.value2) : "")
        else if (rule.type === "expression") condition = rule.value1
        else if (rule.type === "containsText") condition = qsTr("Contains “%1”").arg(rule.text)
        return qsTr("%1 — %2").arg(condition).arg(rule.ranges.join(" "))
    }
    function buildDraft() {
        // Scales, bars and icons never "match", so stopIfTrue means nothing for them.
        const rule = { ranges: ranges.split(/[\s,;]+/).filter(function(part) { return part.length > 0 }), type: type,
                       stopIfTrue: styled && stopIfTrue }
        if (ruleId.length > 0) rule.id = ruleId
        if (compares) { rule.operator = operator; rule.value1 = value1; if (twoValues) rule.value2 = value2 }
        if (type === "expression") rule.value1 = value1
        if (type === "containsText") rule.text = text
        if (styled) rule.style = style
        if (type === "colorScale") {
            if (minColor) rule.minColor = minColor
            if (midColor) rule.midColor = midColor
            if (maxColor) rule.maxColor = maxColor
        }
        if (type === "dataBar" && maxColor) rule.maxColor = maxColor
        return rule
    }
    // The first cells of the draft's first range (at most 6 rows by 4 columns).
    function previewWindow() {
        if (!workbook || epoch < 0) return null
        const first = ranges.split(/[\s,;]+/).filter(function(part) { return part.length > 0 })[0]
        const area = first ? workbook.parseAddress(first) : ({ ok: false })
        if (!area.ok) return null
        return { firstRow: area.firstRow, firstColumn: area.firstColumn,
                 rows: Math.min(6, area.lastRow - area.firstRow + 1), columns: Math.min(4, area.lastColumn - area.firstColumn + 1) }
    }
    function setStyle(key, value) {
        const next = Object.assign({}, style)
        if (value === undefined || value === "" || value === false) delete next[key]
        else next[key] = value
        style = next
    }
    // Starts a new rule over `target` (e.g. the selection address).
    function newRule(target) {
        ruleId = ""
        ranges = target || ""
        type = "cellValue"; operator = "greater"; value1 = ""; value2 = ""; text = ""
        style = ({ fillColor: documentColors[0].color })
        minColor = ""; midColor = ""; maxColor = ""
        stopIfTrue = false
    }
    function load(id) {
        const rule = rules.find(function(candidate) { return candidate.id === id })
        if (!rule) return false
        ruleId = rule.id
        ranges = rule.ranges.join(" ")
        type = rule.type; operator = rule.operator; value1 = rule.value1; value2 = rule.value2; text = rule.text
        style = rule.style || ({})
        minColor = rule.minColor; midColor = rule.midColor; maxColor = rule.maxColor
        stopIfTrue = rule.stopIfTrue
        return true
    }
    // Commits the draft as one undoable step; returns the rule id or "".
    function apply() {
        if (!workbook || !check.ok) return ""
        const id = workbook.setConditionalFormat(sheetId, draft)
        epoch += 1
        if (id.length > 0) { ruleId = id; applied(id) }
        return id
    }
    function remove(id) {
        const ok = !!workbook && workbook.removeConditionalFormat(sheetId, id)
        epoch += 1
        if (ok && id === ruleId) newRule(ranges)
        return ok
    }
    function move(id, priority) {
        const ok = !!workbook && workbook.moveConditionalFormat(sheetId, id, priority)
        epoch += 1
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
        Tk.SectionHeader {
            title: editor.tooltip
            count: String(editor.rules.length)
        }
        Tk.SheetRuleList {
            objectName: "cfRuleList"
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            rules: editor.rules
            currentId: editor.ruleId
            movable: true
            summary: editor.summary
            tooltip: qsTr("Conditional formats in the order they apply")
            onSelected: function(ruleId) { editor.load(ruleId) }
            onMoveRequested: function(ruleId, priority) { editor.move(ruleId, priority) }
            onRemoveRequested: function(ruleId) { editor.remove(ruleId) }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Button { objectName: "cfNew"; text: qsTr("New rule"); small: true; onClicked: editor.newRule(editor.ranges) }
            Tk.Caption {
                visible: editor.ruleId.length > 0
                text: qsTr("Editing rule %1").arg(editor.ruleId)
                color: Tk.Theme.color.textMuted
                Tk.Flex.alignSelf: Tk.Flex.Center
            }
        }
        Tk.ConditionalRuleForm { editor: editor; Tk.Flex.alignSelf: Tk.Flex.Stretch }
        Tk.Caption {
            objectName: "cfError"
            visible: !editor.check.ok && String(editor.check.message || "").length > 0
            text: String(editor.check.message || "")
            color: Tk.Theme.color.danger
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        Tk.Caption { text: qsTr("Preview"); visible: editor.preview.length > 0; color: Tk.Theme.color.textMuted }
        Tk.RulePreview {
            objectName: "cfPreview"
            visible: editor.preview.length > 0
            cells: editor.preview
            tooltip: qsTr("Preview of the rule over its first cells")
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            justify: Tk.Flex.End
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            Tk.Button {
                objectName: "cfApply"
                text: editor.ruleId.length > 0 ? qsTr("Update rule") : qsTr("Add rule")
                variant: "accent"
                available: editor.check.ok === true
                onClicked: editor.apply()
            }
        }
    }
}
