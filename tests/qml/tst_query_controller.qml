// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "QueryController"
    when: windowShown
    visible: true
    width: 320
    height: 180

    Tk.EditableGridModel { id: sourceGrid }
    Tk.EditableGridModel { id: lookupGrid }
    Tk.QueryController { id: controller; source: sourceGrid }
    SignalSpy { id: completedSpy; target: controller; signalName: "completed" }
    SignalSpy { id: rejectedSpy; target: controller; signalName: "rejected" }

    function configureQuery(id) {
        controller.schema = ({ id: "sales", revision: "schema-1", fields: [
            { id: "region", type: "text", nullable: false },
            { id: "amount", type: "number", nullable: true }
        ] })
        controller.query = ({ version: 1, id: id, schemaId: "sales",
            schemaRevision: "schema-1", sourceRevision: sourceGrid.revision,
            filter: { kind: "constant", value: true }, parameters: {},
            rows: ["region"], columns: [],
            measures: [ { id: "sum", field: "amount", function: "sum" } ] })
    }

    function initTestCase() {
        verify(sourceGrid.setRowIds(["r1", "r2"]))
        verify(sourceGrid.setColumnIds(["region", "amount"]))
        verify(sourceGrid.beginEdit())
        verify(sourceGrid.stageCell("r1", "region", "East"))
        verify(sourceGrid.stageCell("r1", "amount", 10))
        verify(sourceGrid.stageCell("r2", "region", "West"))
        verify(sourceGrid.stageCell("r2", "amount", 5))
        verify(sourceGrid.commitEdit())
    }

    function test_asyncResultIsReadOnlyAndDrillThroughWorks() {
        configureQuery("qml-1")
        completedSpy.clear()
        verify(controller.refresh())
        tryCompare(controller, "status", "complete", 5000)
        compare(completedSpy.count, 1)
        compare(controller.requestId, "qml-1")
        verify(controller.resultModel)
        verify(controller.resultModel.readOnly)
        compare(controller.resultModel.rowCount, 3)
        compare(controller.resultModel.columnCount, 1)
        const rowId = controller.resultModel.rowIdAt(0)
        const columnId = controller.resultModel.columnIdAt(0)
        compare(controller.rowLabels[rowId], "East")
        compare(controller.columnLabels[columnId], "All / sum")
        compare(controller.cellValue(rowId, columnId), 10)
        compare(controller.sourceRows(rowId, columnId).join(","), "r1")
        verify(!controller.resultModel.beginEdit())
        compare(controller.cellValue(rowId, columnId), 10)
    }

    function test_cancelAndStaleSourceCannotPublish() {
        configureQuery("qml-cancel")
        verify(controller.refresh())
        controller.cancel()
        compare(controller.status, "cancelled")
        wait(100)
        verify(!controller.resultModel)

        configureQuery("qml-stale")
        rejectedSpy.clear()
        verify(controller.refresh())
        verify(sourceGrid.beginEdit())
        verify(sourceGrid.stageCell("r2", "amount", 6))
        verify(sourceGrid.commitEdit())
        compare(controller.status, "rejected")
        compare(rejectedSpy.count, 1)
        wait(100)
        verify(!controller.resultModel)
    }

    function test_invalidSchemaRevisionRejectsBeforeExecution() {
        configureQuery("qml-invalid")
        rejectedSpy.clear()
        let stale = controller.query
        stale.schemaRevision = "old"
        controller.query = stale
        verify(!controller.refresh())
        compare(controller.status, "rejected")
        verify(controller.diagnostic.length > 0)
        compare(controller.requestId, "qml-invalid")
        compare(rejectedSpy.signalArguments[0][0], "qml-invalid")
        controller.schema = ({ id: "broken" })
        verify(!controller.refresh())
        compare(controller.requestId, "qml-invalid")
        compare(rejectedSpy.signalArguments[1][0], "qml-invalid")
    }

    function test_save_reopen_rebases_compatible_schema_and_source() {
        configureQuery("persisted")
        verify(controller.refresh())
        tryCompare(controller, "status", "complete", 5000)
        const saved = controller.saveQuery()
        verify(saved.length > 0)
        compare(JSON.parse(saved).documentVersion, 1)
        controller.schema = ({ id: "sales", revision: "schema-2", fields: [
            { id: "region", type: "text", nullable: false },
            { id: "amount", type: "number", nullable: true }
        ] })
        verify(sourceGrid.setData(sourceGrid.index(0, 1), 11))
        verify(controller.reopenQuery(saved))
        tryCompare(controller, "status", "complete", 5000)
        compare(controller.query.schemaRevision, "schema-2")
        compare(controller.query.sourceRevision, sourceGrid.revision)
        verify(controller.requestId.indexOf("persisted:reopen:") === 0)
        const id = controller.resultModel.rowIdAt(0)
        compare(controller.cellValue(id, controller.resultModel.columnIdAt(0)), 11)
        controller.schema = ({ id: "sales", revision: "schema-3", fields: [
            { id: "region", type: "text", nullable: false },
            { id: "amount", type: "text", nullable: true }
        ] })
        verify(!controller.reopenQuery(saved))
        compare(controller.status, "rejected")
        verify(controller.diagnostic.indexOf("amount") >= 0)
        verify(!controller.resultModel)
    }

    function test_joined_detail_keeps_source_references() {
        verify(lookupGrid.setRowIds(["p1", "p2"]))
        verify(lookupGrid.setColumnIds(["region", "category"]))
        verify(lookupGrid.beginEdit())
        verify(lookupGrid.stageCell("p1", "region", "East"))
        verify(lookupGrid.stageCell("p1", "category", "Tools"))
        verify(lookupGrid.stageCell("p2", "region", "East"))
        verify(lookupGrid.stageCell("p2", "category", "Supplies"))
        verify(lookupGrid.commitEdit())
        controller.joinSource = lookupGrid
        controller.joinSchema = ({ id: "catalog", revision: "catalog-1", fields: [
            { id: "region", type: "text", nullable: false },
            { id: "category", type: "text", nullable: false }
        ] })
        controller.schema = ({ id: "sales", revision: "schema-1", fields: [
            { id: "region", type: "text", nullable: false },
            { id: "amount", type: "number", nullable: true }
        ] })
        controller.query = ({ version: 1, id: "joined-qml", schemaId: "sales",
            schemaRevision: "schema-1", sourceRevision: sourceGrid.revision,
            filter: { kind: "constant", value: true }, parameters: {},
            rows: [], columns: [], measures: [],
            projection: ["region", "catalog.category"],
            sort: [ { field: "catalog.category", ascending: true, nullsLast: true } ],
            join: { sourceId: "catalog", schemaRevision: "catalog-1",
                sourceRevision: lookupGrid.revision, leftField: "region",
                rightField: "region", leftOuter: true } })
        verify(controller.refresh())
        tryCompare(controller, "status", "complete", 5000)
        compare(controller.resultModel.rowCount, 3)
        const rowId = controller.resultModel.rowIdAt(0)
        const columnId = controller.resultModel.columnIdAt(1)
        compare(controller.cellValue(rowId, columnId), "Supplies")
        const refs = controller.sourceReferences(rowId, columnId)
        compare(refs.length, 2)
        compare(refs[0].sourceId, "sales")
        compare(refs[1].sourceId, "catalog")
        compare(refs[1].rowId, "p2")
        const unmatched = controller.resultModel.rowIdAt(2)
        const unmatchedRefs = controller.sourceReferences(unmatched, columnId)
        compare(unmatchedRefs.length, 1)
        compare(unmatchedRefs[0].sourceId, "sales")
        compare(unmatchedRefs[0].rowId, "r2")
        const saved = controller.saveQuery()
        verify(controller.reopenQuery(saved))
        tryCompare(controller, "status", "complete", 5000)
        compare(controller.resultModel.rowIdAt(0), rowId)
        verify(controller.refresh())
        verify(lookupGrid.setData(lookupGrid.index(0, 1), "Changed"))
        compare(controller.status, "rejected")
        verify(!controller.resultModel)
    }
}
