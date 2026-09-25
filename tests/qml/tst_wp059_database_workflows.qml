// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "Wp059DatabaseWorkflows"
    visible: true
    when: windowShown
    width: 720
    height: 500
    property Tk.LocalDatabaseProvider provider: Tk.LocalDatabaseProvider {}

    Component { id: formComponent; Tk.FormDesigner { provider: root.provider; tableName: "people"; recordId: "1"; mode: "edit"; record: ({ id: 1, name: "Mira", active: 1 }) } }
    Component { id: spyComponent; SignalSpy {} }
    Component { id: documentComponent; Tk.LocalDataDesignDocument {} }
    Component { id: reportComponent; Tk.ReportDesigner { provider: root.provider } }
    Component { id: outputComponent; Tk.LocalReportDocumentModel {} }

    function init() {
        verify(provider.openMemory())
        verify(provider.executeMigration("CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT NOT NULL, active INTEGER NOT NULL)").ok)
        verify(provider.execute("INSERT INTO people VALUES(?, ?, ?)", [1, "Mira", 1]).ok)
    }

    function cleanup() { provider.close() }

    function test_parameter_binding_schema_and_form_request() {
        const schema = provider.schema("people")
        verify(schema.ok)
        compare(schema.fields.length, 3)
        verify(schema.fields[1].notNull)
        const rows = provider.query("SELECT name FROM people WHERE name = ?", ["' OR 1=1 --"])
        verify(rows.ok)
        compare(rows.rows.length, 0)
        verify(!provider.query("SELECT 1; DELETE FROM people").ok)

        const form = createTemporaryObject(formComponent, root)
        verify(form)
        compare(form.schemaSnapshot.fields.length, 3)
        const saved = createTemporaryObject(spyComponent, root, { target: form, signalName: "recordSaveRequested" })
        verify(form.setFieldValue("name", "O'Reilly"))
        const requestId = form.requestRecordSave()
        verify(requestId)
        compare(saved.count, 1)
        const values = saved.signalArguments[0]
        compare(values[2], "1")
        compare(values[3].name, "O'Reilly")
        verify(provider.execute("UPDATE people SET name=? WHERE id=?", [values[3].name, Number(values[2])]).ok)
        compare(provider.query("SELECT name FROM people WHERE id = ?", [1]).rows[0].name, "O'Reilly")

        const migrationPlan = provider.previewMigration("CREATE INDEX people_name ON people(name)")
        verify(migrationPlan.ok)
        verify(!provider.tables.includes("people_name"))
        verify(provider.executeMigration(migrationPlan.sql).ok)
    }

    // WP-059: the designers edit a LocalDataDesignDocument through undoable
    // commands, and the exported definition runs through the report engine.
    function test_design_document_drives_designers() {
        const doc = createTemporaryObject(documentComponent, root)
        verify(doc.bindTable(root.provider, "people"))
        const form = createTemporaryObject(formComponent, root, { designDocument: doc, mode: "design" })
        verify(form)
        compare(form.tableName, "people")
        compare(form.displayFields.length, 3)
        verify(form.updateLabel("name", "Full name"))
        compare(form.caption("name"), "Full name")
        verify(form.moveField("active", -2))
        compare(String(form.displayFields[0].name), "active")
        verify(form.setShown("id", false))
        compare(form.displayFields.length, 3)
        form.mode = "edit"
        compare(form.displayFields.length, 2)
        verify(doc.undo())
        compare(form.displayFields.length, 3)
        verify(doc.undo())
        verify(doc.undo())
        compare(form.caption("name"), "name")
        compare(String(form.displayFields[0].name), "id")

        const report = createTemporaryObject(reportComponent, root, { designDocument: doc })
        verify(report)
        compare(report.tableName, "people")
        verify(report.applyGroup("active", "id", "sum", true))
        compare(doc.report.groups.length, 1)
        compare(report.groupField, "active")
        verify(report.pageBreakPerGroup)
        verify(report.applyFilter("name", "Mira"))
        compare(report.filterField, "name")
        compare(doc.report.query.parameters.filterValue.type, "text")
        const exported = createTemporaryObject(spyComponent, root, { target: report, signalName: "exportRequested" })
        report.requestExport()
        compare(exported.count, 1)
        const output = createTemporaryObject(outputComponent, root)
        const built = output.build(root.provider, exported.signalArguments[0][1])
        verify(built.ok, built.error)
        compare(output.rowCount, 1)
        compare(output.groupCount, 1)
        verify(doc.undo())
        compare(report.filterField, "")
        verify(!report.applyGroup("missing", "", "sum", false))
        verify(report.previewError.length > 0)
        compare(report.groupField, "active")
    }
}
