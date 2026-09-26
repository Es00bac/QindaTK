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
    Component { id: schemaComponent; Tk.DatabaseSchemaView { provider: root.provider } }

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

    // OFF-33: edits validate before any record write, and design or preview
    // mode never emits one, whatever calls requestRecordSave.
    function test_form_validates_and_writes_only_in_edit_mode() {
        const form = createTemporaryObject(formComponent, root)
        verify(form)
        const saved = createTemporaryObject(spyComponent, root, { target: form, signalName: "recordSaveRequested" })
        verify(!form.setFieldValue("active", "yes"))              // INTEGER column
        compare(form.requestState, "invalid")
        verify(form.requestMessage.indexOf("number") >= 0)
        verify(!form.setFieldValue("active", "1.5"))
        compare(form.workingRecord.active, 1)
        verify(form.setFieldValue("active", ""))                  // blank is null, never 0
        compare(form.workingRecord.active, null)
        compare(form.requestState, "idle")
        verify(!form.requestRecordSave())
        compare(form.requestState, "invalid")
        compare(form.requestMessage, "active is required")
        verify(form.setFieldValue("active", "0"))
        verify(form.setFieldValue("name", "   "))
        verify(!form.requestRecordSave())
        compare(form.requestMessage, "name is required")
        compare(saved.count, 0)
        verify(form.setFieldValue("name", "Ada"))

        for (const mode of ["design", "preview"]) {
            form.mode = mode
            verify(!form.requestRecordSave())
            compare(form.requestState, "refused")
            verify(!form.setFieldValue("name", "Changed in " + mode))
        }
        compare(saved.count, 0)
        compare(form.workingRecord.name, "Ada")
        compare(provider.query("SELECT name, active FROM people WHERE id = 1").rows[0].name, "Mira")

        form.mode = "edit"
        verify(form.requestRecordSave())
        compare(saved.count, 1)
        compare(saved.signalArguments[0][3].name, "Ada")
        compare(saved.signalArguments[0][3].active, 0)
    }

    // Data entry lines its values up in one column and shows no type
    // labels (design information); design mode shows them. The schema
    // view names the database file, with the path as the tooltip.
    function test_form_columns_align_and_types_show_in_design() {
        const form = createTemporaryObject(formComponent, root, { width: 600 })
        verify(form)
        waitForRendering(form)
        const labelA = findChild(form, "formLabel_id"), labelB = findChild(form, "formLabel_active")
        compare(labelA.width, labelB.width)
        compare(findChild(form, "formValue_name").x, findChild(form, "formValue_active").x)
        verify(!findChild(form, "formType_name").visible)
        compare(findChild(form, "formStatus").text, "")
        form.mode = "design"
        waitForRendering(form)
        verify(findChild(form, "formType_name").visible)
        verify(findChild(form, "formType_name").text.indexOf("REQUIRED") > 0)
        const view = createTemporaryObject(schemaComponent, root, { width: 600 })
        verify(view)
        compare(findChild(view, "schemaDatabase").text, ":memory:")
    }

    // OFF-34: a relationship change previews as a plan listing the foreign
    // key; a failed commit leaves the view on the schema the database has.
    function test_schema_view_relationship_plan_and_failed_commit() {
        verify(provider.executeMigration("CREATE TABLE orders(id INTEGER PRIMARY KEY, person_id INTEGER)").ok)
        verify(provider.execute("INSERT INTO orders VALUES(?, ?)", [7, 1]).ok)
        const view = createTemporaryObject(schemaComponent, root, { selectedTable: "orders" })
        verify(view)
        compare(view.schemaSnapshot.relationships.length, 0)
        const before = JSON.stringify(view.schemaSnapshot)
        view.migrationStatements = [
            "CREATE TABLE orders_new(id INTEGER PRIMARY KEY, person_id INTEGER REFERENCES people(id))",
            "INSERT INTO orders_new(id, person_id) SELECT id, person_id FROM orders",
            "DROP TABLE orders",
            "ALTER TABLE orders_new RENAME TO orders"]
        verify(view.requestPreview())
        compare(view.requestState, "preview")
        compare(view.planRelationships.length, 1)
        compare(view.planRelationships[0].change, "added")
        compare(view.planRelationships[0].target, "people")
        waitForRendering(view)
        const planRow = findChild(view, "schemaPlanRelation_added_0")
        verify(planRow)
        compare(planRow.text, "adds orders.person_id → people.id")
        compare(JSON.stringify(view.schemaSnapshot), before)       // preview changed nothing

        const commits = createTemporaryObject(spyComponent, root, { target: view, signalName: "migrationCommitRequested" })
        const id = view.requestCommit()
        compare(commits.count, 1)
        // The host runs the plan after another change made it stale.
        verify(provider.executeMigration("CREATE TABLE orders_new(x INTEGER)").ok)
        const result = provider.executeMigrationPlan(commits.signalArguments[0][1].statements)
        verify(!result.ok)
        verify(view.acknowledge(id, "failed", result.error))
        compare(view.requestState, "failed")
        compare(JSON.stringify(view.schemaSnapshot), before)
        compare(view.schemaSnapshot.relationships.length, 0)
        verify(findChild(view, "schemaMigrationFailed").visible)

        verify(provider.executeMigration("DROP TABLE orders_new").ok)
        verify(view.requestPreview())
        const second = view.requestCommit()
        verify(provider.executeMigrationPlan(view.migrationPlan.statements).ok)
        verify(view.acknowledge(second, "applied", ""))
        compare(view.schemaSnapshot.relationships.length, 1)
        compare(view.schemaSnapshot.relationships[0].table, "people")
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
        compare(report.pageFooterText, "Page {page} of {pages}")      // new designs number pages
        verify(report.applyText("pageHeaderText", "People · {page}"))
        compare(doc.report.pageHeaderText, "People · {page}")
        compare(doc.undoText, "Edit page header")
        const output = createTemporaryObject(outputComponent, root)
        const built = output.build(root.provider, exported.signalArguments[0][1])
        verify(built.ok, built.error)
        compare(output.document.pageSetup.footerText, "Page {page} of {pages}")
        verify(output.build(root.provider, doc.report).ok)
        compare(output.document.pageSetup.headerText, "People · {page}")
        verify(doc.undo())
        compare(report.pageHeaderText, "")
        compare(output.rowCount, 1)
        compare(output.groupCount, 1)
        verify(doc.undo())
        compare(report.filterField, "")
        verify(!report.applyGroup("missing", "", "sum", false))
        verify(report.previewError.length > 0)
        compare(report.groupField, "active")
    }
}
