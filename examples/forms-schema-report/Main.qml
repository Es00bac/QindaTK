// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A real SQLite in-memory database backs this local workflow fixture.
// Design values live in fixture memory; record edits use bound SQL values.
Tk.AppWindow {
    id: window
    title: qsTr("Forms, schema and reports · SQLite fixture")
    width: 1280
    height: 960
    property var currentRecord: ({})
    property var formDesign: ({
        title: "Contact details",
        tableName: "contacts",
        fields: [
            { fieldName: "display_name", label: "Display name" },
            { fieldName: "active", label: "Active" },
            { fieldName: "department_id", label: "Department" }
        ]
    })

    Tk.LocalDatabaseProvider {
        id: database
        Component.onCompleted: {
            openMemory()
            executeMigration("CREATE TABLE departments(id INTEGER PRIMARY KEY, name TEXT NOT NULL)")
            executeMigration("CREATE TABLE contacts(id INTEGER PRIMARY KEY, display_name TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1, department_id INTEGER REFERENCES departments(id))")
            execute("INSERT INTO departments(name) VALUES(?)", ["Research"])
            execute("INSERT INTO departments(name) VALUES(?)", ["Studio"])
            execute("INSERT INTO contacts(display_name, active, department_id) VALUES(?, ?, ?)", ["Mira Chen", true, 1])
            execute("INSERT INTO contacts(display_name, active, department_id) VALUES(?, ?, ?)", ["Lee Park", false, 2])
            reloadRecord()
        }
    }

    function reloadRecord() {
        const result = database.query("SELECT id, display_name, active, department_id FROM contacts WHERE id = ?", [1])
        if (result.ok && result.rows.length) currentRecord = result.rows[0]
    }
    function quoted(name) { return '"' + String(name).replace(/"/g, '""') + '"' }

    Component.onCompleted: Qt.callLater(report.runPreview)

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.md; padding: Tk.Theme.space.md
        Tk.Overline { title: qsTr("LOCAL SQLITE · PARAMETERIZED READS · HOST-OWNED DESIGNS"); wide: true }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.md
            Tk.FormDesigner {
                id: form; Tk.Flex.flex: 1; provider: database; tableName: "contacts"
                mode: "edit"; recordId: String(window.currentRecord.id || ""); record: window.currentRecord
                design: window.formDesign
                onRecordSaveRequested: function(requestId, tableName, recordId, changes) {
                    const fields = Object.keys(changes).filter(name => ["display_name", "active", "department_id"].includes(name))
                    if (!fields.length) { acknowledge(requestId, "rejected", qsTr("No recognized editable fields")); return }
                    const sql = "UPDATE " + quoted(tableName) + " SET " + fields.map(name => quoted(name) + " = ?").join(", ") + " WHERE id = ?"
                    const bindings = fields.map(name => changes[name]); bindings.push(Number(recordId))
                    const result = database.execute(sql, bindings)
                    if (result.ok) { window.reloadRecord(); acknowledge(requestId, "accepted", qsTr("SQLite updated %1 row(s)").arg(result.rowsAffected)) }
                    else acknowledge(requestId, "rejected", result.error)
                }
                onDesignChangeRequested: function(fieldName, change) {
                    const fields = formDesign.fields.slice()
                    const item = fields.find(field => field.fieldName === fieldName)
                    if (item) item.label = change.label
                    formDesign = Object.assign({}, formDesign, { fields: fields })
                }
                onDesignSaveRequested: function(requestId, snapshot) { window.formDesign = snapshot; acknowledge(requestId, "accepted", qsTr("Design held in example memory")) }
            }
            Tk.DatabaseSchemaView {
                id: schema; Tk.Flex.flex: 1; provider: database; selectedTable: "contacts"
                migrationSql: "CREATE INDEX idx_contacts_name ON contacts(display_name)"
                onMigrationCommitRequested: function(requestId, plan) {
                    const result = database.executeMigration(plan.sql)
                    acknowledge(requestId, result.ok ? "accepted" : "rejected", result.ok ? qsTr("Schema updated explicitly") : result.error)
                }
            }
        }
        Tk.ReportDesigner {
            id: report; width: parent.width; provider: database; tableName: "contacts"
            headerText: qsTr("Contacts by department"); footerText: qsTr("Internal directory · local fixture")
            groupField: "department_id"; pageSize: 20
        }
        Tk.Notice {
            variant: "info"; title: qsTr("Fixture boundary")
            text: qsTr("Database data is in memory for this run. Form values update through bound SQL parameters; form design is held in memory; schema changes need Preview, then an explicit Request apply.")
        }
    }
}
