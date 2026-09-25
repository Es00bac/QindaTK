// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// SQLite records are in memory. The form and report design is one
// LocalDataDesignDocument: every designer edit is an undo step, and Save /
// Reopen go through the atomic design codec, which rechecks the live schema.
// The report runs through the shared query engine into a paginated document
// that the page view, the print preview and PDF/printer output all share.
Tk.AppWindow {
    id: window
    title: qsTr("Forms, schema and reports · SQLite fixture")
    width: 1280
    height: 1100
    property var currentRecord: ({})
    property string workflowStatus: qsTr("Design not saved · local SQLite sample")
    property int currentPage: 0

    Tk.LocalDataDesignStore { id: designStore }
    Tk.LocalReportDocumentModel { id: reportOutput }
    Tk.LocalDataDesignDocument {
        id: designDoc
        onChanged: Qt.callLater(window.rebuildReport)
    }
    Tk.PrintAdapter {
        id: printer
        objectName: "reportPrintAdapter"
        document: reportOutput.document
        onJobFinished: function(jobId, status) {
            window.workflowStatus = status === "completed" ? qsTr("Printed · %1").arg(job.outputPath || job.printerId)
                                                           : qsTr("Print %1: %2").arg(status).arg(job.error)
        }
    }

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
            window.reloadRecord()
            window.createDesign()
        }
    }

    // The starting design is one undo step ("Create design").
    function createDesign() {
        const history = designDoc.undoService
        history.beginMacro(qsTr("Create design"))
        designDoc.bindTable(database, "contacts")
        designDoc.setFormTitle(qsTr("Contact details"))
        designDoc.setFormFieldLabel("display_name", qsTr("Display name"))
        designDoc.setFormFieldLabel("active", qsTr("Active"))
        designDoc.setFormFieldLabel("department_id", qsTr("Department"))
        designDoc.setReportText("headerText", qsTr("Contact directory"))
        designDoc.setReportText("footerText", qsTr("Internal directory · local fixture"))
        designDoc.setReportGroups([{ field: "department_id", label: qsTr("Department"), pageBreakBefore: true,
                                     totals: [{ field: "active", function: "sum" }] }])
        history.endMacro()
    }
    function reloadRecord() {
        const result = database.query("SELECT id, display_name, active, department_id FROM contacts WHERE id = ?", [1])
        if (result.ok && result.rows.length) currentRecord = result.rows[0]
    }
    function rebuildReport() {
        if (!designDoc.tableName.length) return
        const built = reportOutput.build(database, designDoc.report)
        if (!built.ok) workflowStatus = qsTr("Report kept from the last good build · %1").arg(built.error)
    }
    function saveDesign() {
        const result = designStore.saveToFile(designStore.defaultPath, designDoc.design, database)
        workflowStatus = result.ok ? qsTr("Design saved · %1").arg(result.path) : result.error
    }
    function reopenDesign() {
        const result = designStore.loadFromFile(designStore.defaultPath, database)
        if (!result.ok) { workflowStatus = result.error; return }
        if (!designDoc.setDesign(result.design)) { workflowStatus = designDoc.lastError; return }
        const changed = (result.issues || []).filter(issue => !issue.fatal).map(issue => issue.message)
        workflowStatus = changed.length ? changed.join(" · ")
                                        : qsTr("Design reopened and schema binding verified · %1").arg(result.design.designId)
    }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.md; padding: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("LOCAL SQLITE · STABLE BINDINGS · PAGINATED OUTPUT"); wide: true; Tk.Flex.grow: 1 }
            Tk.Button { objectName: "designUndo"; text: designDoc.undoText ? qsTr("Undo %1").arg(designDoc.undoText) : qsTr("Undo"); enabled: designDoc.canUndo; tooltip: qsTr("Undo the last design edit"); onClicked: designDoc.undo() }
            Tk.Button { objectName: "designRedo"; text: qsTr("Redo"); enabled: designDoc.canRedo; tooltip: qsTr("Redo the design edit"); onClicked: designDoc.redo() }
            Tk.Button { text: qsTr("Save design"); tooltip: qsTr("Atomically save form and report design"); onClicked: window.saveDesign() }
            Tk.Button { text: qsTr("Reopen design"); tooltip: qsTr("Load saved design and validate its SQLite fields"); onClicked: window.reopenDesign() }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.md
            Tk.FormDesigner {
                id: form; Tk.Flex.flex: 1; provider: database; designDocument: designDoc
                mode: "edit"; recordId: String(window.currentRecord.id || ""); record: window.currentRecord
                onRecordSaveRequested: function(requestId, tableName, recordId, changes) {
                    const fields = Object.keys(changes).filter(name => designDoc.boundFields.includes(name) && name !== "id")
                    if (!fields.length) { acknowledge(requestId, "rejected", qsTr("No recognized editable fields")); return }
                    const quoted = name => '"' + String(name).replace(/"/g, '""') + '"'
                    const sql = "UPDATE " + quoted(tableName) + " SET " + fields.map(name => quoted(name) + " = ?").join(", ") + " WHERE id = ?"
                    const bindings = fields.map(name => changes[name]); bindings.push(Number(recordId))
                    const result = database.execute(sql, bindings)
                    if (result.ok) { window.reloadRecord(); window.rebuildReport(); acknowledge(requestId, "accepted", qsTr("SQLite updated %1 row(s)").arg(result.rowsAffected)) }
                    else acknowledge(requestId, "rejected", result.error)
                }
                onDesignSaveRequested: function(requestId, snapshot) { window.saveDesign(); acknowledge(requestId, "accepted", window.workflowStatus) }
            }
            Tk.DatabaseSchemaView {
                id: schema; Tk.Flex.flex: 1; provider: database; selectedTable: "contacts"
                migrationSql: "CREATE INDEX idx_contacts_name ON contacts(display_name)"
                migrationStatements: ["CREATE INDEX idx_contacts_name ON contacts(display_name)",
                                      "CREATE INDEX idx_contacts_department ON contacts(department_id)"]
                onMigrationCommitRequested: function(requestId, plan) {
                    const result = plan.statements ? database.executeMigrationPlan(plan.statements) : database.executeMigration(plan.sql)
                    acknowledge(requestId, result.ok ? "accepted" : "rejected", result.ok ? qsTr("Schema updated explicitly") : result.error)
                    const check = designDoc.validate(database)
                    window.workflowStatus = check.ok ? qsTr("Design still matches the schema") : check.error
                    window.rebuildReport()
                }
            }
        }
        Tk.ReportDesigner {
            id: report; width: parent.width; provider: database; designDocument: designDoc
            pageSize: 12
            onExportRequested: function(requestId, reportDesign) {
                const built = reportOutput.build(database, reportDesign)
                if (!built.ok) { window.workflowStatus = built.error; return }
                const exported = reportOutput.exportPdf(reportOutput.defaultPdfPath)
                window.workflowStatus = exported.ok
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
            // The print panel (sheet, navigation, output settings) is taller
            // than the document strip, so it scrolls instead of overflowing.
            Item {
                height: Tk.Theme.size.waveformHeight * 9
                Tk.Flex.basis: Tk.Theme.size.panelMinWidth * 2.3; Tk.Flex.shrink: 0
                Tk.Scroll {
                    objectName: "reportPrintScroll"
                    anchors.fill: parent
                    accessibleName: qsTr("Print preview and output settings")
                    Tk.PrintPreview {
                        id: printPreview
                        objectName: "reportPrintPreview"
                        pageCount: printer.pageCount
                        currentPage: Math.min(window.currentPage, Math.max(0, printer.pageCount - 1))
                        pageRange: printer.settings.pageRange
                        settings: printer.settings
                        capabilities: printer.capabilities
                        destinations: printer.destinations
                        job: printer.job
                        onPageRequested: function(index) { window.currentPage = index }
                        onPageRangeChangeRequested: function(range) { printer.applySettings({ pageRange: range }) }
                        onSettingsChangeRequested: function(next) { if (!printer.applySettings(next)) window.workflowStatus = printer.lastError }
                        onPrintRequested: function(settings, range) {
                            if (printer.print({ pageRange: range }) === "") window.workflowStatus = printer.lastError
                        }
                        onCancelPrintRequested: function(id) { printer.cancel(id) }
                        Tk.PrintSheetView {
                            objectName: "reportPrintSheet"
                            anchors.fill: parent
                            adapter: printer
                            pageIndex: printPreview.currentPage
                            Accessible.role: Accessible.Graphic
                            Accessible.name: qsTr("Printed sheet for report page %1").arg(pageIndex + 1)
                        }
                    }
                }
            }
        }
        Tk.Notice {
            variant: "info"; title: qsTr("Fixture boundary")
            text: qsTr("SQLite sample records are in memory. Design edits are undoable document commands; the design JSON is versioned and atomically saved with table, field and type bindings, and reopen rechecks the live schema. The report is grouped by department with a page per group; preview, PDF and printers share one paginated document.")
        }
        Tk.Caption { text: reportOutput.errorText ? qsTr("%1 · last error: %2").arg(window.workflowStatus).arg(reportOutput.errorText) : window.workflowStatus; wrapMode: Text.Wrap }
    }
}
