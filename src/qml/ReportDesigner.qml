// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Preview runs one allow-listed, parameterized read through the local provider.
Tk.Box {
    id: control
    objectName: "reportDesigner"
    property Tk.LocalDatabaseProvider provider: null
    property string tableName: ""
    property string filterField: ""
    property string filterValue: ""
    property string groupField: ""
    property string sumField: ""
    property int pageSize: 20
    property string headerText: qsTr("Data report")
    property string footerText: ""
    property var previewRows: []
    property var previewColumns: []
    property bool previewTruncated: false
    property string previewError: ""
    property string tooltip: qsTr("Report designer and query preview")
    property string reportState: "idle"
    property string lastRequestId: ""
    property int _requestSerial: 0
    property var schemaSnapshot: ({ ok: false, fields: [] })
    signal previewRequested(string requestId, var querySpec)
    signal exportRequested(string requestId, var reportDesign)

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: previewError || reportState
    function quoteIdentifier(name) { return '"' + String(name).replace(/"/g, '""') + '"' }
    function fieldExists(name) { return (schemaSnapshot.fields || []).some(field => String(field.name) === name) }
    function numericFields() { return (schemaSnapshot.fields || []).filter(field => /INT|REAL|NUM|DEC|FLOAT|DOUBLE/i.test(String(field.type || ""))).map(field => String(field.name)) }
    function refreshSchema() { schemaSnapshot = provider && tableName ? provider.schema(tableName) : ({ ok: false, fields: [] }) }
    onProviderChanged: refreshSchema()
    onTableNameChanged: refreshSchema()
    Connections { target: control.provider; function onChanged() { control.refreshSchema() } }
    function runPreview() {
        if (!provider || !provider.connected || provider.tables.indexOf(tableName) < 0) { previewError = qsTr("Choose a connected table"); reportState = "error"; return false }
        const fields = (schemaSnapshot.fields || []).map(field => String(field.name))
        if (groupField && !fieldExists(groupField)) { previewError = qsTr("Group field is not in the selected schema"); reportState = "error"; return false }
        if (filterField && !fieldExists(filterField)) { previewError = qsTr("Filter field is not in the selected schema"); reportState = "error"; return false }
        if (sumField && (!groupField || numericFields().indexOf(sumField) < 0)) { previewError = qsTr("Choose a numeric field and a group for the total"); reportState = "error"; return false }
        const id = "report-preview-" + Date.now() + "-" + (++_requestSerial)
        const columns = groupField ? [groupField, "rowCount"].concat(sumField ? ["sumValue"] : []) : fields
        let sql = groupField
            ? "SELECT " + quoteIdentifier(groupField) + ", COUNT(*) AS rowCount" + (sumField ? ", SUM(" + quoteIdentifier(sumField) + ") AS sumValue" : "") + " FROM " + quoteIdentifier(tableName)
            : "SELECT " + fields.map(quoteIdentifier).join(", ") + " FROM " + quoteIdentifier(tableName)
        const bindings = []
        if (filterField) { sql += " WHERE " + quoteIdentifier(filterField) + " = ?"; bindings.push(filterValue) }
        if (groupField) sql += " GROUP BY " + quoteIdentifier(groupField) + " ORDER BY " + quoteIdentifier(groupField)
        sql += " LIMIT ?"; bindings.push(pageSize + 1)
        const spec = { table: tableName, filterField: filterField, groupField: groupField, sumField: sumField, pageSize: pageSize, sql: sql, bindings: bindings }
        lastRequestId = id; reportState = "pending"; previewError = ""
        previewRequested(id, spec)
        const result = provider.query(sql, bindings)
        if (!result.ok) { previewError = result.error; reportState = "error"; previewRows = []; return false }
        previewColumns = result.columns
        previewTruncated = result.rows.length > pageSize || result.truncated
        previewRows = result.rows.slice(0, pageSize)
        reportState = "ready"
        return true
    }
    function requestExport() {
        const id = "report-export-" + Date.now() + "-" + (++_requestSerial)
        lastRequestId = id; reportState = "export-requested"
        exportRequested(id, ({ headerText: headerText, footerText: footerText, tableName: tableName, filterField: filterField, filterValue: filterValue, groupField: groupField, sumField: sumField, pageSize: pageSize }))
        return id
    }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Heading { level: 2; text: control.headerText; Tk.Flex.grow: 1 }
            Tk.Button { objectName: "reportRunPreview"; text: qsTr("Run preview"); variant: "accent"; tooltip: qsTr("Run a parameterized read query"); enabled: !!control.provider && control.provider.connected; onClicked: control.runPreview() }
            Tk.Button { objectName: "reportRequestExport"; text: qsTr("Request export"); tooltip: qsTr("Request host paginated export"); enabled: control.previewRows.length > 0; onClicked: control.requestExport() }
        }
        Tk.Notice { visible: control.previewError.length > 0; variant: "danger"; title: qsTr("Report query failed"); text: control.previewError }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; wrap: Tk.Flex.Wrap
            Tk.ComboBox { objectName: "reportTable"; model: control.provider ? control.provider.tables : []; currentIndex: model.indexOf(control.tableName); tooltip: qsTr("Source table"); Accessible.name: tooltip; onActivated: control.tableName = model[currentIndex] }
            Tk.ComboBox { objectName: "reportFilterField"; model: [qsTr("No filter")].concat((control.schemaSnapshot.fields || []).map(field => String(field.name))); currentIndex: Math.max(0, model.indexOf(control.filterField || qsTr("No filter"))); tooltip: qsTr("Filter field"); Accessible.name: tooltip; onActivated: control.filterField = currentIndex === 0 ? "" : model[currentIndex] }
            Tk.TextField { objectName: "reportFilterValue"; placeholderText: qsTr("Exact match value"); text: control.filterValue; tooltip: qsTr("Parameterized filter value"); Accessible.name: tooltip; onTextChanged: control.filterValue = text }
            Tk.ComboBox { objectName: "reportGroupField"; model: [qsTr("No grouping")].concat((control.schemaSnapshot.fields || []).map(field => String(field.name))); currentIndex: Math.max(0, model.indexOf(control.groupField || qsTr("No grouping"))); tooltip: qsTr("Group rows and count"); Accessible.name: tooltip; onActivated: control.groupField = currentIndex === 0 ? "" : model[currentIndex] }
            Tk.ComboBox { objectName: "reportSumField"; model: [qsTr("No total")].concat(control.numericFields()); currentIndex: Math.max(0, model.indexOf(control.sumField || qsTr("No total"))); tooltip: qsTr("Sum a numeric field within each group"); Accessible.name: tooltip; enabled: !!control.groupField; onActivated: control.sumField = currentIndex === 0 ? "" : model[currentIndex] }
            Tk.NumberField { objectName: "reportPageSize"; from: 1; to: 500; value: control.pageSize; tooltip: qsTr("Preview row limit"); onValueModified: control.pageSize = value }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
            Tk.TextField { objectName: "reportHeader"; text: control.headerText; tooltip: qsTr("Report header"); Accessible.name: tooltip; onTextChanged: control.headerText = text; Tk.Flex.grow: 1 }
            Tk.TextField { objectName: "reportFooter"; text: control.footerText; placeholderText: qsTr("Footer"); tooltip: qsTr("Report footer"); Accessible.name: tooltip; onTextChanged: control.footerText = text; Tk.Flex.grow: 1 }
        }
        Tk.Overline { title: control.previewTruncated ? qsTr("PREVIEW · MORE ROWS OMITTED") : qsTr("PREVIEW"); wide: true }
        Repeater {
            model: control.previewRows
            delegate: Tk.Flex {
                id: resultRow
                required property var modelData
                direction: Tk.Flex.Row; gap: Tk.Theme.space.md
                Repeater {
                    model: control.previewColumns
                    delegate: Tk.Label { required property string modelData; text: String(resultRow.modelData[modelData] ?? "∅"); mono: true; elide: Text.ElideRight; Tk.Flex.grow: 1 }
                }
            }
        }
        Tk.Caption { text: control.reportState === "ready" ? qsTr("%1 preview rows · SQLite read-only query").arg(control.previewRows.length) : control.reportState }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
