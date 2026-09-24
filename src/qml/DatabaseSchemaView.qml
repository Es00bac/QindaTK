// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Introspection is live; migration actions remain proposals until the host
// explicitly executes them through LocalDatabaseProvider.executeMigration.
Tk.Box {
    id: control
    objectName: "databaseSchemaView"
    property Tk.LocalDatabaseProvider provider: null
    property string selectedTable: ""
    property string migrationSql: ""
    property var migrationPlan: ({ ok: false })
    property string requestState: "idle"
    property string requestMessage: ""
    property string tooltip: qsTr("Database schema")
    property string lastRequestId: ""
    property int _requestSerial: 0
    property var schemaSnapshot: ({ ok: false, fields: [], relationships: [] })
    signal migrationPreviewRequested(string requestId, var plan)
    signal migrationCommitRequested(string requestId, var plan)

    Accessible.role: Accessible.Table
    Accessible.name: tooltip
    Accessible.description: provider ? provider.filePath : qsTr("No database connected")
    function refreshSchema() { schemaSnapshot = provider && selectedTable ? provider.schema(selectedTable) : ({ ok: false, fields: [], relationships: [] }) }
    function chooseTable(index) { if (!provider || index < 0 || index >= provider.tables.length) return false; selectedTable = provider.tables[index]; refreshSchema(); return true }
    function requestPreview() {
        if (!provider || !migrationSql.trim()) return false
        const plan = provider.previewMigration(migrationSql)
        migrationPlan = plan
        const id = "migration-preview-" + Date.now() + "-" + (++_requestSerial)
        lastRequestId = id; requestState = plan.ok ? "preview" : "rejected"; requestMessage = plan.warning || plan.error || ""
        migrationPreviewRequested(id, plan)
        return id
    }
    function requestCommit() {
        if (!migrationPlan.ok) return false
        const id = "migration-commit-" + Date.now() + "-" + (++_requestSerial)
        lastRequestId = id; requestState = "pending"; requestMessage = qsTr("Waiting for host approval")
        migrationCommitRequested(id, Object.assign({}, migrationPlan))
        return id
    }
    function acknowledge(id, state, message) { if (id !== lastRequestId || requestState !== "pending") return false; requestState = state; requestMessage = message || ""; return true }

    onProviderChanged: refreshSchema()
    onSelectedTableChanged: refreshSchema()
    Connections {
        target: control.provider
        function onChanged() { control.refreshSchema() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Heading { level: 2; text: qsTr("Database schema"); Tk.Flex.grow: 1 }
            Tk.Badge { text: control.provider && control.provider.connected ? control.provider.filePath : qsTr("Disconnected"); variant: control.provider && control.provider.connected ? "success" : "warning" }
            Tk.ComboBox {
                objectName: "schemaTablePicker"; model: control.provider ? control.provider.tables : []
                currentIndex: control.provider ? model.indexOf(control.selectedTable) : -1
                tooltip: qsTr("Select database table"); Accessible.name: qsTr("Database table")
                onActivated: control.chooseTable(currentIndex)
            }
        }
        Tk.Notice { visible: !!control.provider && control.provider.errorText.length > 0; variant: "danger"; title: qsTr("Database error"); text: control.provider ? control.provider.errorText : "" }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("COLUMN"); wide: true; Tk.Flex.basis: Tk.Theme.size.fieldWidth }
            Tk.Overline { title: qsTr("TYPE"); wide: true; Tk.Flex.basis: Tk.Theme.size.fieldWidth / 2 }
            Tk.Overline { title: qsTr("NULL / KEY / DEFAULT"); wide: true; Tk.Flex.grow: 1 }
        }
        Repeater {
            model: control.schemaSnapshot.fields || []
            delegate: Tk.Flex {
                required property var modelData
                objectName: "schemaField_" + String(modelData.name || "")
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
                Tk.Label { text: String(modelData.name || ""); mono: true; Tk.Flex.basis: Tk.Theme.size.fieldWidth }
                Tk.Label { text: String(modelData.type || "BLOB"); muted: true; Tk.Flex.basis: Tk.Theme.size.fieldWidth / 2 }
                Tk.Label {
                    text: (modelData.notNull ? qsTr("required") : qsTr("nullable"))
                        + (modelData.primaryKeyOrder ? " · PK" : "")
                        + (modelData.defaultValue !== undefined && modelData.defaultValue !== null ? " · " + String(modelData.defaultValue) : "")
                    muted: true; Tk.Flex.grow: 1
                }
            }
        }
        Repeater {
            model: control.schemaSnapshot.relationships || []
            delegate: Tk.Label {
                required property var modelData
                objectName: "schemaRelation_" + String(modelData.id) + "_" + String(modelData.sequence)
                text: qsTr("%1.%2 → %3.%4").arg(control.selectedTable).arg(modelData.from).arg(modelData.table).arg(modelData.to)
                muted: true; Accessible.role: Accessible.StaticText
            }
        }
        Tk.Overline { title: qsTr("MIGRATION PREVIEW"); wide: true }
        Tk.TextArea { objectName: "schemaMigrationSql"; width: parent.width; rows: 2; text: control.migrationSql; placeholderText: qsTr("CREATE INDEX ..."); tooltip: qsTr("SQL migration draft"); Accessible.name: tooltip; onTextChanged: control.migrationSql = text }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
            Tk.Label { text: control.requestMessage || control.requestState; muted: true; wrapMode: Text.Wrap; Tk.Flex.grow: 1 }
            Tk.Button { objectName: "schemaPreviewMigration"; text: qsTr("Preview only"); tooltip: qsTr("Validate a schema statement without executing it"); onClicked: control.requestPreview() }
            Tk.Button { objectName: "schemaRequestMigration"; text: qsTr("Request apply"); tooltip: qsTr("Ask host to execute the approved migration"); enabled: control.migrationPlan.ok; onClicked: control.requestCommit() }
        }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
