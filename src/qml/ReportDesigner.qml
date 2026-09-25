// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Preview runs one allow-listed, parameterized read through the local provider.
// AGENT-CONTRACT (docs/forms-reports.md#designers): with `designDocument`
// set, the header, footer, group, group total, page break, detail and
// filter controls edit the document's report through LocalDataDesignDocument
// commands (one undo step each) and follow its undo/redo; export requests
// then carry designDocument.report, the definition LocalReportDocumentModel
// builds. Without it the properties below are the host's snapshot.
Tk.Box {
    id: control
    objectName: "reportDesigner"
    property Tk.LocalDatabaseProvider provider: null
    property Tk.LocalDataDesignDocument designDocument: null
    property string tableName: ""
    property string filterField: ""
    property string filterValue: ""
    property string groupField: ""
    property string sumField: ""
    property string totalFunction: "sum" // WP-029 aggregate applied to sumField per group
    property bool pageBreakPerGroup: false
    property bool showDetail: true
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
    readonly property var totalFunctions: ["sum", "average", "minimum", "maximum", "countValues"]
    signal previewRequested(string requestId, var querySpec)
    signal exportRequested(string requestId, var reportDesign)

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: previewError || reportState
    function quoteIdentifier(name) { return '"' + String(name).replace(/"/g, '""') + '"' }
    function fieldExists(name) { return (schemaSnapshot.fields || []).some(field => String(field.name) === name) }
    function fieldType(name) { const field = (schemaSnapshot.fields || []).find(entry => String(entry.name) === name); return field ? String(field.type || "") : "" }
    function numericFields() { return (schemaSnapshot.fields || []).filter(field => /INT|REAL|NUM|DEC|FLOAT|DOUBLE/i.test(String(field.type || ""))).map(field => String(field.name)) }
    function refreshSchema() { schemaSnapshot = provider && tableName ? provider.schema(tableName) : ({ ok: false, fields: [] }) }
    onProviderChanged: refreshSchema()
    onTableNameChanged: refreshSchema()
    onDesignDocumentChanged: syncFromDesign()
    Component.onCompleted: syncFromDesign()
    Connections { target: control.provider; function onChanged() { control.refreshSchema() } }
    Connections { target: control.designDocument; function onChanged() { control.syncFromDesign() } }

    // The document's report, shown through the simple one-group controls.
    function syncFromDesign() {
        if (!designDocument) return
        const report = designDocument.report
        if (designDocument.tableName.length) tableName = designDocument.tableName
        headerText = String(report.headerText || "")
        footerText = String(report.footerText || "")
        showDetail = report.detail !== false
        const group = (report.groups || [])[0] || ({})
        groupField = String(group.field || report.groupField || "")
        pageBreakPerGroup = group.pageBreakBefore === true
        const total = (group.totals || []).find(entry => entry.function !== "countRows") || ({})
        sumField = String(total.field || report.sumField || "")
        totalFunction = String(total.function || "sum")
        const filter = (report.query || ({})).filter || ({})
        const parameters = (report.query || ({})).parameters || ({})
        filterField = filter.kind === "compare" && filter.op === "eq" ? String(filter.field || "") : String(report.filterField || "")
        const parameter = parameters[filter.parameter] || ({})
        filterValue = parameter.value !== undefined ? String(parameter.value) : String(report.filterValue || "")
    }
    function edited(ok) {
        if (!ok && designDocument) { previewError = designDocument.lastError; reportState = "error" }
        return ok
    }
    function groupSpec(field, totalField, fn, pageBreak) {
        return field ? [{ field: field, pageBreakBefore: pageBreak, totals: totalField ? [{ field: totalField, function: fn }] : [] }] : []
    }
    function applyText(key, text) {
        if (designDocument) return edited(designDocument.setReportText(key, text))
        if (key === "headerText") headerText = text; else footerText = text
        return true
    }
    function applyGroup(field, totalField, fn, pageBreak) {
        if (designDocument) return edited(designDocument.setReportGroups(groupSpec(field, totalField, fn, pageBreak)))
        groupField = field; sumField = field ? totalField : ""; totalFunction = fn; pageBreakPerGroup = pageBreak
        return true
    }
    function applyDetail(detail) {
        if (designDocument) return edited(designDocument.setReportDetail(detail))
        showDetail = detail
        return true
    }
    // A typed WP-029 parameter for the filter field's SQLite type.
    function typedParameter(field, value) {
        const type = fieldType(field).toUpperCase()
        const text = String(value).trim()
        if (/INT/.test(type) && !/BOOL/.test(type)) return text.length && /^-?\d+$/.test(text) ? { type: "integer", value: text } : { type: "null" }
        if (/BOOL/.test(type)) return text.length ? { type: "boolean", value: /^(true|yes|1)$/i.test(text) } : { type: "null" }
        if (/CHAR|CLOB|TEXT|DATE|TIME/.test(type) || type.length === 0) return { type: "text", value: String(value) }
        return text.length && isFinite(Number(text)) ? { type: "number", value: Number(text) } : { type: "null" }
    }
    function applyFilter(field, value) {
        if (designDocument) {
            if (!field) return edited(designDocument.setReportFilter({}, {}))
            return edited(designDocument.setReportFilter({ kind: "compare", field: field, op: "eq", parameter: "filterValue" },
                                                         { filterValue: typedParameter(field, value) }))
        }
        filterField = field; filterValue = value
        return true
    }
    function runPreview() {
        if (!provider || !provider.connected || provider.tables.indexOf(tableName) < 0) { previewError = qsTr("Choose a connected table"); reportState = "error"; return false }
        const fields = (schemaSnapshot.fields || []).map(field => String(field.name))
        if (groupField && !fieldExists(groupField)) { previewError = qsTr("Group field is not in the selected schema"); reportState = "error"; return false }
        if (filterField && !fieldExists(filterField)) { previewError = qsTr("Filter field is not in the selected schema"); reportState = "error"; return false }
        if (sumField && (!groupField || numericFields().indexOf(sumField) < 0)) { previewError = qsTr("Choose a numeric field and a group for the total"); reportState = "error"; return false }
        const id = "report-preview-" + Date.now() + "-" + (++_requestSerial)
        const aggregate = ({ sum: "SUM", average: "AVG", minimum: "MIN", maximum: "MAX", countValues: "COUNT" })[totalFunction] || "SUM"
        let sql = groupField
            ? "SELECT " + quoteIdentifier(groupField) + ", COUNT(*) AS rowCount" + (sumField ? ", " + aggregate + "(" + quoteIdentifier(sumField) + ") AS totalValue" : "") + " FROM " + quoteIdentifier(tableName)
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
        exportRequested(id, designDocument ? designDocument.report
                                           : ({ headerText: headerText, footerText: footerText, tableName: tableName, filterField: filterField, filterValue: filterValue,
                                                groups: groupSpec(groupField, sumField, totalFunction, pageBreakPerGroup), detail: showDetail, pageSize: pageSize }))
        return id
    }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Heading { level: 2; text: control.headerText; Tk.Flex.grow: 1 }
            Tk.IconButton {
                objectName: "reportUndo"; visible: !!control.designDocument; iconName: "undo-2"
                enabled: !!control.designDocument && control.designDocument.canUndo
                tooltip: control.designDocument && control.designDocument.undoText ? qsTr("Undo %1").arg(control.designDocument.undoText) : qsTr("Undo")
                onClicked: control.designDocument.undo()
            }
            Tk.IconButton {
                objectName: "reportRedo"; visible: !!control.designDocument; iconName: "redo-2"
                enabled: !!control.designDocument && control.designDocument.canRedo
                tooltip: control.designDocument && control.designDocument.redoText ? qsTr("Redo %1").arg(control.designDocument.redoText) : qsTr("Redo")
                onClicked: control.designDocument.redo()
            }
            Tk.Button { objectName: "reportRunPreview"; text: qsTr("Run preview"); variant: "accent"; tooltip: qsTr("Run a parameterized read query"); enabled: !!control.provider && control.provider.connected; onClicked: control.runPreview() }
            Tk.Button { objectName: "reportRequestExport"; text: qsTr("Request export"); tooltip: qsTr("Request host paginated export"); enabled: control.previewRows.length > 0; onClicked: control.requestExport() }
        }
        Tk.Notice { visible: control.previewError.length > 0; variant: "danger"; title: qsTr("Report query failed"); text: control.previewError }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; wrap: Tk.Flex.Wrap
            Tk.ComboBox { objectName: "reportTable"; enabled: !control.designDocument; model: control.provider ? control.provider.tables : []; currentIndex: model.indexOf(control.tableName); tooltip: qsTr("Source table"); Accessible.name: tooltip; onActivated: control.tableName = model[currentIndex] }
            Tk.ComboBox { objectName: "reportFilterField"; model: [qsTr("No filter")].concat((control.schemaSnapshot.fields || []).map(field => String(field.name))); currentIndex: Math.max(0, model.indexOf(control.filterField || qsTr("No filter"))); tooltip: qsTr("Filter field"); Accessible.name: tooltip; onActivated: control.applyFilter(currentIndex === 0 ? "" : model[currentIndex], control.filterValue) }
            Tk.TextField { objectName: "reportFilterValue"; placeholderText: qsTr("Exact match value"); text: control.filterValue; tooltip: qsTr("Parameterized filter value"); Accessible.name: tooltip; onEditingFinished: { if (text !== control.filterValue) control.applyFilter(control.filterField, text) } }
            Tk.ComboBox { objectName: "reportGroupField"; model: [qsTr("No grouping")].concat((control.schemaSnapshot.fields || []).map(field => String(field.name))); currentIndex: Math.max(0, model.indexOf(control.groupField || qsTr("No grouping"))); tooltip: qsTr("Group rows and count"); Accessible.name: tooltip; onActivated: control.applyGroup(currentIndex === 0 ? "" : model[currentIndex], control.sumField, control.totalFunction, control.pageBreakPerGroup) }
            Tk.ComboBox { objectName: "reportSumField"; model: [qsTr("No total")].concat(control.numericFields()); currentIndex: Math.max(0, model.indexOf(control.sumField || qsTr("No total"))); tooltip: qsTr("Total a numeric field within each group"); Accessible.name: tooltip; enabled: !!control.groupField; onActivated: control.applyGroup(control.groupField, currentIndex === 0 ? "" : model[currentIndex], control.totalFunction, control.pageBreakPerGroup) }
            Tk.ComboBox { objectName: "reportTotalFunction"; model: control.totalFunctions; currentIndex: Math.max(0, model.indexOf(control.totalFunction)); tooltip: qsTr("Group total function"); Accessible.name: tooltip; enabled: !!control.sumField; onActivated: control.applyGroup(control.groupField, control.sumField, model[currentIndex], control.pageBreakPerGroup) }
            Tk.CheckBox { objectName: "reportPageBreak"; text: qsTr("New page per group"); checked: control.pageBreakPerGroup; enabled: !!control.groupField; tooltip: qsTr("Start every group after the first on a new page"); onToggled: control.applyGroup(control.groupField, control.sumField, control.totalFunction, checked) }
            Tk.CheckBox { objectName: "reportDetail"; text: qsTr("Detail rows"); checked: control.showDetail; tooltip: qsTr("Print every row, or only group headers and totals"); onToggled: control.applyDetail(checked) }
            Tk.NumberField { objectName: "reportPageSize"; from: 1; to: 500; value: control.pageSize; tooltip: qsTr("Preview row limit"); onValueModified: control.pageSize = value }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
            Tk.TextField { objectName: "reportHeader"; text: control.headerText; tooltip: qsTr("Report header"); Accessible.name: tooltip; onEditingFinished: { if (text !== control.headerText) control.applyText("headerText", text) } Tk.Flex.grow: 1 }
            Tk.TextField { objectName: "reportFooter"; text: control.footerText; placeholderText: qsTr("Footer"); tooltip: qsTr("Report footer"); Accessible.name: tooltip; onEditingFinished: { if (text !== control.footerText) control.applyText("footerText", text) } Tk.Flex.grow: 1 }
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
