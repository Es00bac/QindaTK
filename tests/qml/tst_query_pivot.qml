// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "QueryPivot"
    when: windowShown
    visible: true
    width: 920
    height: 680

    readonly property var fields: [
        { "id": "region", "label": "Region", "type": "text", "nullable": false },
        { "id": "amount", "label": "Amount", "type": "number", "nullable": true },
        { "id": "units", "label": "Units", "type": "integer", "nullable": true },
        { "id": "active", "label": "Active", "type": "boolean", "nullable": false }
    ]
    Component {
        id: builderComponent
        Tk.QueryBuilder {
            width: 820
            schema: ({ "id": "sales", "revision": "schema-4", "fields": root.fields })
            sourceRevision: "source-10"
        }
    }
    Component {
        id: connectedBuilderComponent
        Tk.QueryBuilder {
            width: 820
            controller: realController
            schema: realController.schema
        }
    }
    Component { id: filterComponent; Tk.FilterBar { width: 820; fields: root.fields } }
    Component { id: pivotComponent; Tk.PivotView { width: 680; height: 340; maxRows: 1; maxColumns: 1 } }
    Component { id: spyComponent; SignalSpy { } }
    Tk.EditableGridModel { id: sourceGrid }
    Tk.QueryController { id: realController; source: sourceGrid }
    SignalSpy { id: queryRejectedSpy; target: realController; signalName: "rejected" }

    function test_filter_bar_builds_typed_predicate_and_retains_invalid_draft() {
        const filter = createTemporaryObject(filterComponent, root)
        verify(filter !== null)
        filter.addClause()
        compare(filter.clauses.length, 1)
        filter.updateClause(0, "field", "units")
        filter.updateClause(0, "value", "9007199254740993")
        verify(filter.valid)
        let predicate = filter.toPredicate()
        compare(predicate.kind, "compare")
        compare(predicate.field, "units")
        compare(predicate.op, "eq")
        compare(predicate.literal.type, "integer")
        compare(predicate.literal.value, "9007199254740993")

        filter.updateClause(0, "value", "12x")
        verify(!filter.valid)
        compare(filter.clauses[0].value, "12x")
        verify(filter.validationError.length > 0)
        filter.updateClause(0, "op", "isNull")
        verify(filter.valid)
        compare(filter.toPredicate().kind, "isNull")
    }

    function test_query_builder_emits_versioned_spec_and_axes() {
        const builder = createTemporaryObject(builderComponent, root)
        verify(builder !== null)
        const editor = findChild(builder, "queryFilterBar")
        verify(editor !== null)
        editor.addClause()
        editor.updateClause(0, "field", "amount")
        editor.updateClause(0, "value", "10.5")
        builder.setAxis(0, 1)
        builder.setAxis(1, 2)
        builder.addMeasure()
        builder.publish()
        const spec = builder.querySpec
        verify(spec !== null)
        compare(spec.version, 1)
        compare(spec.schemaId, "sales")
        compare(spec.schemaRevision, "schema-4")
        compare(spec.sourceRevision, "source-10")
        compare(spec.filter.literal.type, "number")
        compare(spec.filter.literal.value, 10.5)
        compare(spec.rows[0], "region")
        compare(spec.columns[0], "amount")
        compare(spec.measures.length, 1)
        verify(spec.id.length > 0)
        compare(builder.loadSpec(spec), true)
        compare(builder.rowFields[0], "region")
        compare(builder.columnFields[0], "amount")
        const zeroSpec = JSON.parse(JSON.stringify(spec))
        zeroSpec.filter.literal.value = 0
        compare(builder.loadSpec(zeroSpec), true)
        compare(builder.clauses[0].value, "0")
        compare(builder.clauses[0].op, "eq")
        const previousClauses = builder.clauses
        const nestedSpec = JSON.parse(JSON.stringify(spec))
        nestedSpec.filter = ({ "kind": "and", "children": [
            { "kind": "or", "children": [spec.filter] }
        ] })
        compare(builder.loadSpec(nestedSpec), false)
        verify(builder.loadDiagnostic.length > 0)
        compare(builder.clauses.length, previousClauses.length)

        builder.measures = [ { "id": "measure_1", "field": "amount", "function": "sum" },
                             { "id": "measure_2", "field": "units", "function": "sum" } ]
        builder._measureSequence = 0
        builder.removeMeasure(0)
        builder.addMeasure()
        compare(builder.measures.length, 2)
        verify(builder.measures[0].id !== builder.measures[1].id)
    }

    function test_pivot_display_is_bounded_and_drills_through_read_only_result() {
        const rows = ["row:one", "row:two"]
        const columns = ["column:a", "column:b"]
        const controller = {
            status: "complete",
            diagnostic: "",
            rowLabels: ({ "row:one": "North", "row:two": "South" }),
            columnLabels: ({ "column:a": "Revenue", "column:b": "Count" }),
            resultModel: {
                rowCount: 2,
                columnCount: 2,
                rowIdAt: function(index) { return rows[index] },
                columnIdAt: function(index) { return columns[index] }
            },
            cellValue: function(rowId, columnId) { return rowId === "row:one" ? 42 : 7 },
            sourceRows: function(rowId, columnId) { return [rowId + "/source"] }
        }
        const pivot = createTemporaryObject(pivotComponent, root, { controller: controller })
        verify(pivot !== null)
        compare(pivot.totalRows, 2)
        compare(pivot.totalColumns, 2)
        compare(pivot.visibleRows, 1)
        compare(pivot.visibleColumns, 1)
        verify(pivot.truncated)
        waitForRendering(pivot)
        const cell = findChild(pivot, "pivotCell_0_0")
        verify(cell !== null)
        compare(cell.text, "42")
        const spy = createTemporaryObject(spyComponent, root,
                                          { target: pivot, signalName: "drillThrough" })
        mouseClick(cell)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "row:one")
        compare(spy.signalArguments[0][1], "column:a")
        compare(spy.signalArguments[0][2][0], "row:one/source")

        mouseClick(findChild(pivot, "pivotRowsNext"))
        mouseClick(findChild(pivot, "pivotColumnsNext"))
        compare(pivot.rowPage, 1)
        compare(pivot.columnPage, 1)
        compare(findChild(pivot, "pivotCell_1_1").text, "7")
        pivot.activateCell(0, 0)
        compare(spy.count, 2)
        compare(spy.signalArguments[1][0], "row:two")
        compare(spy.signalArguments[1][1], "column:b")
    }

    function test_builder_refreshes_real_query_controller_and_pivot_is_read_only() {
        verify(sourceGrid.setRowIds(["n1", "s1"]))
        verify(sourceGrid.setColumnIds(["region", "amount"]))
        verify(sourceGrid.beginEdit())
        verify(sourceGrid.stageCell("n1", "region", "North"))
        verify(sourceGrid.stageCell("n1", "amount", 42))
        verify(sourceGrid.stageCell("s1", "region", "South"))
        verify(sourceGrid.stageCell("s1", "amount", 7))
        verify(sourceGrid.commitEdit())
        realController.schema = ({ "id": "sales", "revision": "schema-1", "fields": [
            { "id": "region", "type": "text", "nullable": false },
            { "id": "amount", "type": "number", "nullable": false }
        ] })
        const builder = createTemporaryObject(connectedBuilderComponent, root)
        verify(builder !== null)
        builder.rowFields = ["region"]
        builder.measures = [ { "id": "revenue", "field": "amount", "function": "sum" } ]
        const filterEditor = findChild(builder, "queryFilterBar")
        verify(filterEditor !== null)
        filterEditor.addClause()
        filterEditor.updateClause(0, "field", "amount")
        filterEditor.updateClause(0, "op", "gt")
        filterEditor.updateClause(0, "value", "5")
        verify(builder.requestPreview())
        tryCompare(realController, "status", "complete", 5000)
        compare(realController.requestId, builder.querySpec.id)
        verify(realController.resultModel.readOnly)
        const pivot = createTemporaryObject(pivotComponent, root, { controller: realController })
        verify(pivot !== null)
        tryCompare(pivot, "totalRows", 3, 5000)
        const firstId = realController.resultModel.rowIdAt(0)
        const firstLabel = realController.rowLabels[firstId]
        const readonlyResult = realController.resultModel
        waitForRendering(pivot)
        const firstCell = findChild(pivot, "pivotCell_0_0")
        verify(firstCell !== null)
        compare(firstCell.text, "42")
        verify(readonlyResult.readOnly)
        verify(!readonlyResult.beginEdit())
        compare(readonlyResult.cellValue(firstId, readonlyResult.columnIdAt(0)), 42)
        const drill = createTemporaryObject(spyComponent, root,
                                            { target: pivot, signalName: "drillThrough" })
        mouseClick(firstCell)
        compare(drill.count, 1)
        compare(drill.signalArguments[0][0], firstId)
        compare(drill.signalArguments[0][2].join(","), "n1")
        mouseClick(findChild(pivot, "pivotRowsNext"))
        tryCompare(pivot, "rowPage", 1)
        const secondPageCell = findChild(pivot, "pivotCell_1_0")
        verify(secondPageCell !== null)
        compare(secondPageCell.text, "7")
        mouseClick(secondPageCell)
        compare(drill.count, 2)
        compare(drill.signalArguments[1][0], realController.resultModel.rowIdAt(1))
        compare(drill.signalArguments[1][2].join(","), "s1")

        queryRejectedSpy.clear()
        verify(builder.requestPreview())
        verify(sourceGrid.beginEdit("Change data while refresh is pending"))
        verify(sourceGrid.stageCell("n1", "amount", 45))
        verify(sourceGrid.commitEdit())
        tryCompare(realController, "status", "rejected", 5000)
        compare(queryRejectedSpy.count, 1)
        compare(queryRejectedSpy.signalArguments[0][0], builder.querySpec.id)
        verify(queryRejectedSpy.signalArguments[0][1].length > 0)
        verify(!realController.resultModel)
        compare(firstLabel.length > 0, true)
    }

    function test_empty_pivot_reports_query_action() {
        const pivot = createTemporaryObject(pivotComponent, root,
                                            { maxRows: 10000, maxColumns: 10000 })
        verify(pivot !== null)
        compare(pivot.totalRows, 0)
        compare(pivot.visibleColumns, 0)
        compare(pivot.truncated, false)
        compare(pivot.rowPageSize, 50)
        compare(pivot.columnPageSize, 24)
    }
}
