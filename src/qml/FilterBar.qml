// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Composes typed filter leaves; owner receives a version-independent
// predicate tree and decides when to submit it.
Item {
    id: bar
    property var fields: []
    property var clauses: []
    property string conjunction: "and"
    property string tooltip: qsTr("Filter rows")
    readonly property bool valid: validationError.length === 0
    readonly property string validationError: {
        for (let i = 0; i < clauses.length; ++i) {
            const c = clauses[i]
            if (!fieldById(c.field)) return qsTr("Choose a field for every filter.")
            if (c.op !== "isNull" && c.op !== "isNotNull" && String(c.value).trim().length === 0)
                return qsTr("Enter a value for every filter.")
            if (c.type === "integer" && c.op !== "isNull" && c.op !== "isNotNull"
                    && !/^[+-]?\d+$/.test(String(c.value).trim()))
                return qsTr("Enter a whole integer value.")
            if (c.type === "number" && c.op !== "isNull" && c.op !== "isNotNull"
                    && !isFinite(Number(c.value)))
                return qsTr("Enter a finite numeric value.")
        }
        return ""
    }
    signal predicateChanged(var predicate)
    signal clausesEdited(var clauses)
    signal conjunctionEdited(string conjunction)
    signal clauseRemoved(int index)

    function fieldById(id) {
        for (let i = 0; i < fields.length; ++i)
            if (fields[i].id === id) return fields[i]
        return null
    }
    function fieldIndex(id) {
        for (let i = 0; i < fields.length; ++i)
            if (fields[i].id === id) return i
        return -1
    }
    function operatorIndex(clause) {
        const t = clauseRowFieldType(clause)
        const ops = t === "text" ? ["eq", "contains", "isNull", "isNotNull"]
                   : t === "boolean" ? ["eq", "isNull", "isNotNull"]
                   : ["eq", "ne", "lt", "le", "gt", "ge", "isNull", "isNotNull"]
        return Math.max(0, ops.indexOf(clause.op))
    }
    function clauseRowFieldType(clause) {
        const field = fieldById(clause.field)
        return field ? field.type : "text"
    }
    function addClause() {
        if (!enabled || fields.length === 0) return
        const next = clauses.slice()
        const field = fields[0]
        next.push({ "field": field.id, "type": field.type, "op": "eq", "value": "" })
        clauses = next
        publish()
    }
    function updateClause(index, key, value) {
        if (!enabled || index < 0 || index >= clauses.length) return
        const next = clauses.slice()
        const entry = Object.assign({}, next[index])
        entry[key] = value
        if (key === "field") {
            const field = fieldById(value)
            entry.type = field ? field.type : "text"
            entry.op = "eq"
            entry.value = ""
        }
        next[index] = entry
        clauses = next
        publish()
    }
    function removeClause(index) {
        if (!enabled || index < 0 || index >= clauses.length) return
        const next = clauses.slice()
        next.splice(index, 1)
        clauses = next
        clauseRemoved(index)
        publish()
    }
    function typedValue(c) {
        if (c.type === "integer") return { "type": "integer", "value": String(c.value).trim() }
        if (c.type === "number") return { "type": "number", "value": Number(c.value) }
        if (c.type === "boolean") return { "type": "boolean", "value": c.value === "true" }
        return { "type": "text", "value": String(c.value) }
    }
    function toPredicate() {
        const children = []
        for (let i = 0; i < clauses.length; ++i) {
            const c = clauses[i]
            if (c.op === "isNull" || c.op === "isNotNull")
                children.push({ "kind": c.op, "field": c.field })
            else
                children.push({ "kind": "compare", "field": c.field, "op": c.op,
                                "literal": typedValue(c) })
        }
        if (children.length === 0) return { "kind": "constant", "value": true }
        if (children.length === 1) return children[0]
        return { "kind": conjunction === "or" ? "or" : "and", "children": children }
    }
    function publish() {
        clausesEdited(clauses)
        predicateChanged(toPredicate())
    }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: content.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: valid ? qsTr("%1 active filter conditions").arg(clauses.length)
                                 : validationError
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled
    Tk.ToolTip { text: bar.tooltip; visible: text.length > 0 && hover.hovered }
    HoverHandler { id: hover }

    Tk.Flex {
        id: content
        width: bar.width > 0 ? bar.width : implicitWidth
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Label { text: qsTr("Filters"); accent: true }
            Tk.ComboBox {
                objectName: "filterConjunction"
                model: [qsTr("Match all"), qsTr("Match any")]
                currentIndex: bar.conjunction === "or" ? 1 : 0
                tooltip: qsTr("Combine filter conditions with AND or OR")
                onActivated: function(index) {
                    bar.conjunction = index === 1 ? "or" : "and"
                    bar.conjunctionEdited(bar.conjunction)
                    bar.publish()
                }
            }
            Tk.Spacer { visible: true }
            Tk.Button {
                objectName: "filterAdd"
                text: qsTr("Add filter")
                tooltip: qsTr("Add a typed filter condition")
                enabled: bar.enabled && bar.fields.length > 0
                onClicked: bar.addClause()
            }
        }
        Repeater {
            model: bar.clauses.length
            delegate: Item {
                id: clauseRow
                required property int index
                readonly property var clause: bar.clauses[index]
                readonly property var field: bar.fieldById(clause.field)
                readonly property bool noValue: clause.op === "isNull" || clause.op === "isNotNull"
                objectName: "filterClause_" + index
                implicitWidth: row.implicitWidth
                implicitHeight: Math.max(Tk.Theme.size.control,
                                         Tk.Theme.font.body + Tk.Theme.space.sm * 2)
                Tk.Flex.shrink: 0
                Tk.Flex {
                    id: row
                    anchors.fill: parent
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    Tk.ComboBox {
                        objectName: "filterField_" + clauseRow.index
                        model: bar.fields
                        textRole: "label"
                        currentIndex: bar.fieldIndex(clauseRow.clause.field)
                        tooltip: qsTr("Filter field")
                        onActivated: function(index) {
                            const f = bar.fields[index]
                            if (f) bar.updateClause(clauseRow.index, "field", f.id)
                        }
                    }
                    Tk.ComboBox {
                        objectName: "filterOperator_" + clauseRow.index
                        model: clauseRow.field && clauseRow.field.type === "text"
                               ? [qsTr("equals"), qsTr("contains"), qsTr("is null"), qsTr("is not null")]
                               : clauseRow.field && clauseRow.field.type === "boolean"
                                 ? [qsTr("equals"), qsTr("is null"), qsTr("is not null")]
                                 : [qsTr("equals"), qsTr("not equal"), qsTr("less than"),
                                    qsTr("at most"), qsTr("greater than"), qsTr("at least"),
                                    qsTr("is null"), qsTr("is not null")]
                        currentIndex: bar.operatorIndex(clauseRow.clause)
                        tooltip: qsTr("Comparison")
                        onActivated: function(index) {
                            const ops = clauseRow.field && clauseRow.field.type === "text"
                                    ? ["eq", "contains", "isNull", "isNotNull"]
                                    : clauseRow.field && clauseRow.field.type === "boolean"
                                      ? ["eq", "isNull", "isNotNull"]
                                      : ["eq", "ne", "lt", "le", "gt", "ge", "isNull", "isNotNull"]
                            bar.updateClause(clauseRow.index, "op", ops[index])
                        }
                    }
                    Tk.TextField {
                        objectName: "filterValue_" + clauseRow.index
                        visible: !clauseRow.noValue && clauseRow.clause.type !== "boolean"
                        enabled: visible
                        placeholderText: clauseRow.clause.type === "integer" ? qsTr("Integer")
                                        : clauseRow.clause.type === "number" ? qsTr("Number") : qsTr("Value")
                        text: String(clauseRow.clause.value)
                        implicitWidth: Tk.Theme.size.fieldWidth
                        tooltip: qsTr("Typed filter value")
                        onTextEdited: bar.updateClause(clauseRow.index, "value", text)
                    }
                    Tk.ComboBox {
                        objectName: "filterBoolean_" + clauseRow.index
                        visible: !clauseRow.noValue && clauseRow.clause.type === "boolean"
                        model: [qsTr("False"), qsTr("True")]
                        currentIndex: clauseRow.clause.value === "true" ? 1 : 0
                        tooltip: qsTr("Boolean filter value")
                        onActivated: function(index) {
                            bar.updateClause(clauseRow.index, "value", index === 1 ? "true" : "false")
                        }
                    }
                    Tk.Button {
                        objectName: "filterRemove_" + clauseRow.index
                        iconName: "x"
                        text: qsTr("Remove")
                        small: true
                        tooltip: qsTr("Remove filter condition")
                        onClicked: bar.removeClause(clauseRow.index)
                    }
                }
            }
        }
        Tk.Label {
            objectName: "filterValidation"
            visible: !bar.valid
            text: bar.validationError
            color: Tk.Theme.color.danger
        }
        Tk.Label {
            visible: bar.clauses.length === 0 && bar.fields.length === 0
            text: qsTr("No fields available")
            muted: true
        }
    }
}
