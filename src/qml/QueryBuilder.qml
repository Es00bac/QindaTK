// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Typed visual editor for QueryEngine's version-1 JSON contract. This control
// never builds executable text; the controller validates and runs the AST.
Item {
    id: builder
    property var controller: null
    property var schema: controller ? controller.schema : ({ "id": "", "revision": "", "fields": [] })
    property string sourceRevision: controller && controller.source ? controller.source.revision : ""
    property var clauses: []
    property string conjunction: "and"
    property var rowFields: []
    property var columnFields: []
    property var measures: []
    property var querySpec: null
    property string tooltip: qsTr("Build a typed query and pivot layout")
    property string loadDiagnostic: ""
    property int _sequence: 0
    property int _measureSequence: 0
    readonly property var fields: {
        const output = []
        const input = schema && schema.fields ? schema.fields : []
        for (let i = 0; i < input.length; ++i)
            output.push({ "id": input[i].id, "type": input[i].type,
                          "label": input[i].label || input[i].id })
        return output
    }
    readonly property string diagnostic: controller ? controller.diagnostic : ""
    readonly property string status: controller ? controller.status : "idle"

    signal queryRequested(var query)
    signal queryBuilt(var query)
    signal previewRequested(string requestId)

    function has(list, id) { return list.indexOf(id) >= 0 }
    function setAxis(index, axisIndex) {
        if (index < 0 || index >= fields.length) return
        const id = fields[index].id
        const rows = rowFields.slice()
        const columns = columnFields.slice()
        const r = rows.indexOf(id)
        const c = columns.indexOf(id)
        if (r >= 0) rows.splice(r, 1)
        if (c >= 0) columns.splice(c, 1)
        if (axisIndex === 1) rows.push(id)
        else if (axisIndex === 2) columns.push(id)
        rowFields = rows
        columnFields = columns
        publish()
    }
    function axisIndex(id) { return has(rowFields, id) ? 1 : has(columnFields, id) ? 2 : 0 }
    function addMeasure() {
        if (!enabled || fields.length === 0) return
        const next = measures.slice()
        let candidate = fields[0]
        for (let i = 0; i < fields.length; ++i)
            if (fields[i].type === "number" || fields[i].type === "integer") { candidate = fields[i]; break }
        let id = ""
        do {
            ++_measureSequence
            id = "measure_" + _measureSequence
        } while (measureIdExists(id))
        next.push({ "id": id, "field": candidate.id,
                    "function": candidate.type === "number" || candidate.type === "integer" ? "sum" : "countValues" })
        measures = next
        publish()
    }
    function updateMeasure(index, key, value) {
        if (!enabled || index < 0 || index >= measures.length) return
        const next = measures.slice()
        next[index] = Object.assign({}, next[index], ({ [key]: value }))
        measures = next
        publish()
    }
    function removeMeasure(index) {
        if (!enabled || index < 0 || index >= measures.length) return
        const next = measures.slice()
        next.splice(index, 1)
        measures = next
        publish()
    }
    function measureIdExists(id) {
        for (let i = 0; i < measures.length; ++i)
            if (measures[i].id === id) return true
        return false
    }
    function compileSpec() {
        if (!schema || !schema.id || !schema.revision || !sourceRevision || measures.length === 0)
            return null
        ++_sequence
        const predicate = filterEditor.toPredicate()
        const serializedMeasures = []
        for (let i = 0; i < measures.length; ++i)
            serializedMeasures.push({ "id": measures[i].id, "field": measures[i].field,
                                     "function": measures[i].function })
        return { "version": 1, "id": "qml-query-" + Date.now() + "-" + _sequence,
                 "schemaId": schema.id, "schemaRevision": schema.revision,
                 "sourceRevision": sourceRevision, "filter": predicate,
                 "parameters": ({}), "rows": rowFields.slice(), "columns": columnFields.slice(),
                 "measures": serializedMeasures }
    }
    function publish() {
        const spec = compileSpec()
        if (spec === null) return
        querySpec = spec
        queryBuilt(spec)
    }
    function requestPreview() {
        if (!enabled || controller === null || !filterEditor.valid) return false
        const spec = compileSpec()
        if (spec === null) return false
        querySpec = spec
        controller.query = spec
        queryRequested(spec)
        const accepted = controller.refresh()
        if (accepted) previewRequested(spec.id)
        return accepted
    }
    function loadSpec(spec) {
        if (!spec || spec.version !== 1) {
            loadDiagnostic = qsTr("Unsupported saved query version.")
            return false
        }
        const nextRows = spec.rows ? spec.rows.slice() : []
        const nextColumns = spec.columns ? spec.columns.slice() : []
        const nextMeasures = spec.measures ? spec.measures.slice() : []
        const nextConjunction = spec.filter && spec.filter.kind === "or" ? "or" : "and"
        if (spec.filter && spec.filter.kind === "constant" && spec.filter.value === true) {
            rowFields = nextRows
            columnFields = nextColumns
            measures = nextMeasures
            conjunction = nextConjunction
            clauses = []
            querySpec = spec
            loadDiagnostic = ""
            return true
        }
        let leaves = spec.filter && spec.filter.children ? spec.filter.children : [spec.filter]
        const parsed = []
        for (let i = 0; i < leaves.length; ++i) {
            const leaf = leaves[i]
            if (!leaf || (leaf.kind !== "compare" && leaf.kind !== "isNull" && leaf.kind !== "isNotNull"))
                return rejectLoad(qsTr("This saved query uses a predicate group the editor cannot represent."))
            const field = fieldById(leaf.field)
            if (!field) return rejectLoad(qsTr("Saved query field '%1' is missing from the current schema.").arg(leaf.field))
            const v = leaf.literal || (leaf.parameter && spec.parameters
                                      ? spec.parameters[leaf.parameter] : null) || {}
            let op = leaf.kind === "compare" ? leaf.op : leaf.kind
            let value = ""
            if (leaf.kind === "compare") {
                if (v.type === "null" && op === "eq") op = "isNull"
                else if (v.type === "null" && op === "ne") op = "isNotNull"
                else if (v.type !== field.type
                         && !((field.type === "integer" || field.type === "number")
                              && (v.type === "integer" || v.type === "number")))
                    return rejectLoad(qsTr("A saved filter value has a type that does not match its field."))
                else value = v.type === "integer" ? String(v.value)
                             : v.type === "boolean" ? String(v.value) : String(v.value)
            }
            parsed.push({ "field": leaf.field, "type": field.type, "op": op, "value": value })
        }
        rowFields = nextRows
        columnFields = nextColumns
        measures = nextMeasures
        conjunction = nextConjunction
        clauses = parsed
        querySpec = spec
        loadDiagnostic = ""
        return true
    }
    function rejectLoad(message) {
        loadDiagnostic = message
        return false
    }
    function fieldById(id) {
        for (let i = 0; i < fields.length; ++i) if (fields[i].id === id) return fields[i]
        return null
    }
    function fieldIndex(id) {
        for (let i = 0; i < fields.length; ++i) if (fields[i].id === id) return i
        return -1
    }
    function measureFunctions(type) {
        return type === "integer" || type === "number"
               ? ["countRows", "countValues", "sum", "average", "minimum", "maximum"]
               : ["countRows", "countValues", "minimum", "maximum"]
    }
    function measureFunctionIndex(measure) {
        return Math.max(0, measureFunctions(fieldById(measure.field) ? fieldById(measure.field).type : "text")
                            .indexOf(measure.function))
    }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: content.implicitHeight
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: diagnostic.length > 0 ? diagnostic : qsTr("Visual query editor")
    Tk.ToolTip { text: builder.tooltip; visible: text.length > 0 && hover.hovered }
    HoverHandler { id: hover }

    Tk.Flex {
        id: content
        width: builder.width > 0 ? builder.width : implicitWidth
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.FilterBar {
            id: filterEditor
            objectName: "queryFilterBar"
            width: content.width
            fields: builder.fields
            clauses: builder.clauses
            conjunction: builder.conjunction
            onClausesEdited: function(next) { builder.clauses = next }
            onConjunctionEdited: function(next) { builder.conjunction = next }
            onPredicateChanged: builder.publish()
        }
        Tk.Box {
            objectName: "queryAxisPanel"
            width: content.width
            color: Tk.Theme.color.surface
            borderWidth: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            padding: Tk.Theme.space.sm
            implicitHeight: axisColumn.implicitHeight + Tk.Theme.space.sm * 2
            Tk.Flex {
                id: axisColumn
                anchors.fill: parent
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Label { text: qsTr("Group fields"); accent: true }
                Repeater {
                    model: builder.fields.length
                    delegate: Tk.Flex {
                        required property int index
                        objectName: "queryAxisField_" + builder.fields[index].id
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        Tk.Label { text: builder.fields[parent.index].label; Tk.Flex.grow: 1 }
                        Tk.ComboBox {
                            objectName: "queryAxis_" + builder.fields[parent.index].id
                            model: [qsTr("Not grouped"), qsTr("Rows"), qsTr("Columns")]
                            currentIndex: builder.axisIndex(builder.fields[parent.index].id)
                            tooltip: qsTr("Place this field on a pivot axis")
                            onActivated: function(axis) { builder.setAxis(parent.index, axis) }
                        }
                    }
                }
                Tk.Label { text: qsTr("Measures"); accent: true }
                Repeater {
                    model: builder.measures.length
                    delegate: Tk.Flex {
                        required property int index
                        readonly property var measure: builder.measures[index]
                        objectName: "queryMeasure_" + index
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.xs
                        Tk.ComboBox {
                            objectName: "queryMeasureField_" + parent.index
                            model: builder.fields
                            textRole: "label"
                            currentIndex: builder.fieldIndex(parent.measure.field)
                            tooltip: qsTr("Measure source field")
                            onActivated: function(i) {
                                const f = builder.fields[i]
                                if (f) builder.updateMeasure(parent.index, "field", f.id)
                            }
                        }
                        Tk.ComboBox {
                            objectName: "queryAggregate_" + parent.index
                            model: builder.measureFunctions(builder.fieldById(parent.measure.field)
                                                            ? builder.fieldById(parent.measure.field).type : "text")
                            currentIndex: builder.measureFunctionIndex(parent.measure)
                            tooltip: qsTr("Aggregate function")
                            onActivated: function(i) {
                                const choices = builder.measureFunctions(builder.fieldById(parent.measure.field)
                                                                         ? builder.fieldById(parent.measure.field).type : "text")
                                builder.updateMeasure(parent.index, "function", choices[i])
                            }
                        }
                        Tk.Button {
                            objectName: "queryMeasureRemove_" + parent.index
                            text: qsTr("Remove")
                            small: true
                            tooltip: qsTr("Remove aggregate")
                            onClicked: builder.removeMeasure(parent.index)
                        }
                    }
                }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    Tk.Button {
                        objectName: "queryMeasureAdd"
                        text: qsTr("Add measure")
                        tooltip: qsTr("Add an aggregate value")
                        enabled: builder.fields.length > 0
                        onClicked: builder.addMeasure()
                    }
                    Tk.Spacer { visible: true }
                    Tk.Button {
                        objectName: "queryPreview"
                        text: builder.status === "running" ? qsTr("Refreshing…") : qsTr("Refresh preview")
                        iconName: builder.status === "running" ? "loader-circle" : "play"
                        variant: "accent"
                        busy: builder.status === "running"
                        tooltip: qsTr("Run this bounded query and update the pivot preview")
                        onClicked: builder.requestPreview()
                    }
                    Tk.Button {
                        objectName: "queryCancel"
                        text: qsTr("Cancel")
                        visible: builder.status === "running"
                        tooltip: qsTr("Cancel the active query")
                        onClicked: if (builder.controller) builder.controller.cancel()
                    }
                }
                Tk.Label {
                    objectName: "queryDiagnostic"
                    visible: builder.diagnostic.length > 0
                    text: builder.diagnostic
                    color: Tk.Theme.color.danger
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
