// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Schema and record values are host snapshots. Edits are saved only by a
// host request; design mode never writes to the database.
Tk.Box {
    id: control
    objectName: "formDesigner"
    property Tk.LocalDatabaseProvider provider: null
    property string tableName: ""
    property string mode: "design" // design, preview, edit
    property string recordId: ""
    property var record: ({})
    property var workingRecord: record
    property var design: ({ title: "Untitled form", tableName: "", fields: [] })
    property string tooltip: qsTr("Database form designer")
    property string lastRequestId: ""
    property string requestState: "idle"
    property string requestMessage: ""
    property int _requestSerial: 0
    property var schemaSnapshot: ({ ok: false, fields: [] })
    signal designChangeRequested(string fieldName, var value)
    signal designSaveRequested(string requestId, var design)
    signal recordSaveRequested(string requestId, string tableName, string recordId, var changes)

    Accessible.role: Accessible.Form
    Accessible.name: tooltip
    Accessible.description: requestMessage
    onRecordChanged: workingRecord = Object.assign({}, record)
    onProviderChanged: refreshSchema()
    onTableNameChanged: refreshSchema()

    function refreshSchema() { schemaSnapshot = provider && tableName ? provider.schema(tableName) : ({ ok: false, fields: [] }) }

    function fieldDesign(name) {
        const rows = design.fields || []
        for (let i = 0; i < rows.length; ++i) if (String(rows[i].fieldName) === name) return rows[i]
        return {}
    }
    function caption(name) { const spec = fieldDesign(name); return String(spec.label || name) }
    function quoteIdentifier(name) { return '"' + String(name).replace(/"/g, '""') + '"' }
    function setFieldValue(name, value) {
        const field = (schemaSnapshot.fields || []).find(entry => String(entry.name) === name)
        if (!field) return false
        const next = Object.assign({}, workingRecord)
        const type = String(field.type || "").toLowerCase()
        if (value === "" && field.notNull !== true) next[name] = null
        else if (type.includes("int") || type.includes("real") || type.includes("numeric") || type.includes("float") || type.includes("double")) {
            const number = Number(value); if (!isFinite(number)) return false; next[name] = number
        } else next[name] = value
        workingRecord = next
        return true
    }
    function validateRecord(values) {
        for (let i = 0; i < (schemaSnapshot.fields || []).length; ++i) {
            const field = schemaSnapshot.fields[i]
            if (field.notNull && !field.primaryKeyOrder && (values[field.name] === null || values[field.name] === undefined || String(values[field.name]).trim() === ""))
                return qsTr("%1 is required").arg(caption(String(field.name)))
        }
        return ""
    }
    function requestRecordSave() {
        const validation = validateRecord(workingRecord)
        if (validation.length) { requestState = "invalid"; requestMessage = validation; return false }
        const changes = ({})
        Object.keys(workingRecord).forEach(name => { if (workingRecord[name] !== record[name]) changes[name] = workingRecord[name] })
        if (!Object.keys(changes).length) { requestState = "idle"; requestMessage = qsTr("No changes"); return false }
        const id = "form-record-" + Date.now() + "-" + (++_requestSerial)
        lastRequestId = id; requestState = "pending"; requestMessage = ""
        recordSaveRequested(id, tableName, recordId, changes)
        return id
    }
    function requestDesignSave() {
        const id = "form-design-" + Date.now() + "-" + (++_requestSerial)
        lastRequestId = id; requestState = "pending"; requestMessage = ""
        designSaveRequested(id, Object.assign({}, design))
        return id
    }
    function acknowledge(id, state, message) {
        if (id !== lastRequestId || requestState !== "pending") return false
        requestState = state; requestMessage = message || ""
        return true
    }
    function updateLabel(fieldName, label) { designChangeRequested(fieldName, { label: label }) }

    Connections {
        target: control.provider
        function onChanged() { control.refreshSchema() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Heading { level: 2; text: String(control.design.title || qsTr("Form")); Tk.Flex.grow: 1 }
            Tk.Badge { text: control.mode }
            Tk.Button { text: qsTr("Design"); tooltip: qsTr("Edit form labels and layout"); onClicked: control.mode = "design" }
            Tk.Button { text: qsTr("Preview"); tooltip: qsTr("Preview without database writes"); onClicked: control.mode = "preview" }
            Tk.Button { text: qsTr("Edit record"); tooltip: qsTr("Edit the selected database record"); onClicked: control.mode = "edit" }
        }
        Tk.Notice { visible: control.requestState === "invalid" || control.requestState === "rejected"; variant: "danger"; title: qsTr("Form update"); text: control.requestMessage }
        Repeater {
            model: control.schemaSnapshot.fields || []
            delegate: Tk.Flex {
                required property var modelData
                property string fieldName: String(modelData.name || "")
                objectName: "formField_" + fieldName
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
                Tk.Caption { objectName: "formLabel_" + fieldName; text: control.caption(fieldName); width: Tk.Theme.size.labelWidth; wrapMode: Text.Wrap }
                Tk.Label { text: String(modelData.type || "").toUpperCase() + (modelData.notNull ? " · REQUIRED" : ""); muted: true; Tk.Flex.basis: Tk.Theme.size.fieldWidth / 2 }
                Tk.TextField {
                    objectName: "formValue_" + fieldName; visible: control.mode !== "design" && !String(modelData.type).toLowerCase().includes("bool")
                    enabled: control.mode === "edit"; readOnly: control.mode !== "edit"; text: String(control.workingRecord[fieldName] ?? "")
                    tooltip: control.caption(fieldName); Accessible.name: control.caption(fieldName)
                    onEditingFinished: control.setFieldValue(fieldName, text)
                    Tk.Flex.grow: 1
                }
                Tk.CheckBox {
                    objectName: "formBool_" + fieldName; visible: control.mode !== "design" && String(modelData.type).toLowerCase().includes("bool")
                    enabled: control.mode === "edit"; checked: !!control.workingRecord[fieldName]; text: control.caption(fieldName)
                    tooltip: control.caption(fieldName); onToggled: control.setFieldValue(fieldName, checked); Tk.Flex.grow: 1
                }
                Tk.TextField {
                    objectName: "formDesignLabel_" + fieldName; visible: control.mode === "design"; text: control.caption(fieldName)
                    tooltip: qsTr("Field label"); Accessible.name: qsTr("Label for %1").arg(fieldName); Tk.Flex.grow: 1
                    onEditingFinished: control.updateLabel(fieldName, text)
                }
                Tk.Badge { visible: control.mode === "design"; text: qsTr("binds to %1").arg(fieldName) }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
            Tk.Label { text: control.requestMessage || control.requestState; muted: true; Tk.Flex.grow: 1 }
            Tk.Button { objectName: "formSaveDesign"; visible: control.mode === "design"; text: qsTr("Save form design"); tooltip: qsTr("Request host to persist form design"); onClicked: control.requestDesignSave() }
            Tk.Button { objectName: "formSaveRecord"; visible: control.mode === "edit"; text: qsTr("Save record"); variant: "accent"; tooltip: qsTr("Request a validated record update"); onClicked: control.requestRecordSave() }
        }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
