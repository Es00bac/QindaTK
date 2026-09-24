// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// SQLite records are in-memory; form/report design and rendered output use
// the local atomic design codec and the shared paginated document renderer.
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
    property string workflowStatus: qsTr("Design not saved · local SQLite sample")

    Tk.LocalDataDesignStore { id: designStore }
    Tk.LocalReportDocumentModel { id: reportOutput }

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
            for (let i = 2; i < 102; ++i)
                execute("INSERT INTO contacts(display_name, active, department_id) VALUES(?, ?, ?)",
                        ["Contributor " + String(i).padStart(3, "0"), i % 2 === 0, i % 2 + 1])
            reloadRecord()
        }
    }

    function reloadRecord() {
        const result = database.query("SELECT id, display_name, active, department_id FROM contacts WHERE id = ?", [1])
        if (result.ok && result.rows.length) currentRecord = result.rows[0]
    }
    function quoted(name) { return '"' + String(name).replace(/"/g, '""') + '"' }
    function reportSnapshot() {
        return { tableName: report.tableName, headerText: report.headerText,
                 footerText: report.footerText, filterField: report.filterField,
                 filterValue: report.filterValue, groupField: report.groupField,
                 sumField: report.sumField }
    }
    function designSnapshot() {
        return {
            format: "qindatk.local-data-design", version: 1, designId: "contacts-workbench",
            binding: { provider: "sqlite", schema: "main", table: "contacts",
                       fields: ["id", "display_name", "active", "department_id"] },
            form: formDesign,
            report: reportSnapshot()
        }
    }
    function saveDesign() {
        const result = designStore.saveToFile(designStore.defaultPath, designSnapshot(), database)
        workflowStatus = result.ok ? qsTr("Design saved · %1").arg(result.path) : result.error
    }
    function reopenDesign() {
        const result = designStore.loadFromFile(designStore.defaultPath, database)
        if (!result.ok) { workflowStatus = result.error; return }
        const data = result.design
        formDesign = data.form
        report.tableName = data.report.tableName
        report.headerText = data.report.headerText
        report.footerText = data.report.footerText
        report.filterField = data.report.filterField
        report.filterValue = data.report.filterValue
        report.groupField = data.report.groupField
        report.sumField = data.report.sumField || ""
        workflowStatus = qsTr("Design reopened and schema binding verified · %1").arg(data.designId)
        report.runPreview()
    }

    Component.onCompleted: Qt.callLater(function() {
        report.runPreview()
        reportOutput.build(database, reportSnapshot())
    })

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.md; padding: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("LOCAL SQLITE · STABLE BINDINGS · PAGINATED OUTPUT"); wide: true; Tk.Flex.grow: 1 }
            Tk.Button { text: qsTr("Save design"); tooltip: qsTr("Atomically save form and report design"); onClicked: window.saveDesign() }
            Tk.Button { text: qsTr("Reopen design"); tooltip: qsTr("Load saved design and validate its SQLite fields"); onClicked: window.reopenDesign() }
        }
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
                migrationStatements: ["CREATE INDEX idx_contacts_name ON contacts(display_name)",
                                      "CREATE INDEX idx_contacts_department ON contacts(department_id)"]
                onMigrationCommitRequested: function(requestId, plan) {
                    const result = plan.statements ? database.executeMigrationPlan(plan.statements) : database.executeMigration(plan.sql)
                    acknowledge(requestId, result.ok ? "accepted" : "rejected", result.ok ? qsTr("Schema updated explicitly") : result.error)
                }
            }
        }
        Tk.ReportDesigner {
            id: report; width: parent.width; provider: database; tableName: "contacts"
            headerText: qsTr("Contact directory"); footerText: qsTr("Internal directory · local fixture")
            pageSize: 20
            onExportRequested: function(requestId, reportDesign) {
                const built = reportOutput.build(database, reportDesign)
                if (!built.ok) { workflowStatus = built.error; return }
                const exported = reportOutput.exportPdf(reportOutput.defaultPdfPath)
                workflowStatus = exported.ok
                    ? qsTr("PDF exported · %1 rows · %2").arg(exported.rowCount).arg(exported.path)
                    : exported.error
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
            Tk.PageNavigator { view: reportDocument; Tk.Flex.shrink: 0 }
            Item {
                height: Tk.Theme.size.waveformHeight * 9
                Tk.Flex.grow: 1; Tk.Flex.basis: 0; Tk.Flex.minHeight: 0
                Tk.RichDocumentView {
                    id: reportDocument
                    anchors.fill: parent
                    document: reportOutput.document
                    zoom: 0.55
                    tooltip: qsTr("Paginated report document")
                    readOnly: true
                }
            }
        }
        Tk.Notice {
            variant: "info"; title: qsTr("Fixture boundary")
            text: qsTr("SQLite sample records are in memory. Design JSON is versioned and atomically saved to a local file with table/field names; reopen rechecks the live schema. Request export builds a paginated RichTextDocument and writes PDF using its document renderer.")
        }
        Tk.Caption { text: window.workflowStatus; wrapMode: Text.Wrap }
    }
}
