// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Schema and record values are host snapshots. Edits are saved only by a
// host request; design mode never writes to the database.
// AGENT-CONTRACT (docs/forms-reports.md#designers): with `designDocument`
// set, the form layout is the document's form and every design edit
// (label, order, shown) is one LocalDataDesignDocument command, so undo,
// save and recovery cover it; without it the control only emits
// designChangeRequested and the host owns the design.
Tk.Box {
    id: control
    objectName: "formDesigner"
    property Tk.LocalDatabaseProvider provider: null
    property Tk.LocalDataDesignDocument designDocument: null
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
    // Schema fields in design order; hidden fields only in design mode.
    readonly property var displayFields: control.orderedFields()
    signal designChangeRequested(string fieldName, var value)
    signal designSaveRequested(string requestId, var design)
    signal recordSaveRequested(string requestId, string tableName, string recordId, var changes)

    Accessible.role: Accessible.Form
    Accessible.name: tooltip
    Accessible.description: requestMessage
    onRecordChanged: workingRecord = Object.assign({}, record)
    onProviderChanged: refreshSchema()
    onTableNameChanged: refreshSchema()
    onDesignDocumentChanged: syncDesign()
    Component.onCompleted: syncDesign()

    function refreshSchema() { schemaSnapshot = provider && tableName ? provider.schema(tableName) : ({ ok: false, fields: [] }) }
    function syncDesign() {
        if (!designDocument) return
        design = designDocument.form
        if (designDocument.tableName.length && designDocument.tableName !== tableName) tableName = designDocument.tableName
    }

    function fieldDesign(name) {
        const rows = design.fields || []
        for (let i = 0; i < rows.length; ++i) if (String(rows[i].fieldName) === name) return rows[i]
        return {}
    }
    function orderedFields() {
        const schema = schemaSnapshot.fields || []
        const specs = design.fields || []
        const out = []
        const used = ({})
        for (let i = 0; i < specs.length; ++i) {
            const name = String(specs[i].fieldName)
            const field = schema.find(entry => String(entry.name) === name)
            if (!field || used[name]) continue
            used[name] = true
            if (specs[i].hidden === true && mode !== "design") continue
            out.push(field)
        }
        // A host-held design may list only the fields it relabels.
        if (!designDocument)
            for (let j = 0; j < schema.length; ++j) if (!used[String(schema[j].name)]) out.push(schema[j])
        return out
    }
    function caption(name) { const spec = fieldDesign(name); return String(spec.label || name) }
    function isHidden(name) { return fieldDesign(name).hidden === true }
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
    // Design edits: document commands when a design document is bound.
    function reportEdit(ok) {
        if (!ok && designDocument) { requestState = "invalid"; requestMessage = designDocument.lastError }
        return ok
    }
    function updateLabel(fieldName, label) {
        if (label === caption(fieldName)) return true
        if (designDocument) return reportEdit(designDocument.setFormFieldLabel(fieldName, label))
        designChangeRequested(fieldName, { label: label })
        return true
    }
    function moveField(fieldName, delta) {
        const names = (design.fields || []).map(entry => String(entry.fieldName))
        const index = names.indexOf(fieldName)
        if (index < 0 || index + delta < 0 || index + delta >= names.length) return false
        if (designDocument) return reportEdit(designDocument.moveFormField(fieldName, index + delta))
        designChangeRequested(fieldName, { index: index + delta })
        return true
    }
    function setShown(fieldName, shown) {
        if (shown === !isHidden(fieldName)) return true
        if (designDocument) return reportEdit(designDocument.setFormFieldHidden(fieldName, !shown))
        designChangeRequested(fieldName, { hidden: !shown })
        return true
    }
    function updateTitle(title) {
        if (title === String(design.title || "")) return true
        if (designDocument) return reportEdit(designDocument.setFormTitle(title))
        designChangeRequested("", { title: title })
        return true
    }

    Connections {
        target: control.provider
        function onChanged() { control.refreshSchema() }
    }
    Connections {
        target: control.designDocument
        function onChanged() { control.syncDesign() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Heading { visible: control.mode !== "design" || !control.designDocument; level: 2; text: String(control.design.title || qsTr("Form")); Tk.Flex.grow: 1 }
            Tk.TextField {
                objectName: "formTitle"; visible: control.mode === "design" && !!control.designDocument
                text: String(control.design.title || ""); tooltip: qsTr("Form title"); Accessible.name: tooltip
                onEditingFinished: control.updateTitle(text); Tk.Flex.grow: 1
            }
            Tk.Badge { text: control.mode }
            Tk.IconButton {
                objectName: "formUndo"; visible: !!control.designDocument; iconName: "undo-2"
                enabled: !!control.designDocument && control.designDocument.canUndo
                tooltip: control.designDocument && control.designDocument.undoText ? qsTr("Undo %1").arg(control.designDocument.undoText) : qsTr("Undo")
                onClicked: control.designDocument.undo()
            }
            Tk.IconButton {
                objectName: "formRedo"; visible: !!control.designDocument; iconName: "redo-2"
                enabled: !!control.designDocument && control.designDocument.canRedo
                tooltip: control.designDocument && control.designDocument.redoText ? qsTr("Redo %1").arg(control.designDocument.redoText) : qsTr("Redo")
                onClicked: control.designDocument.redo()
            }
            Tk.Button { text: qsTr("Design"); tooltip: qsTr("Edit form labels and layout"); onClicked: control.mode = "design" }
            Tk.Button { text: qsTr("Preview"); tooltip: qsTr("Preview without database writes"); onClicked: control.mode = "preview" }
            Tk.Button { text: qsTr("Edit record"); tooltip: qsTr("Edit the selected database record"); onClicked: control.mode = "edit" }
        }
        Tk.Notice { visible: control.requestState === "invalid" || control.requestState === "rejected"; variant: "danger"; title: qsTr("Form update"); text: control.requestMessage }
        Repeater {
            model: control.displayFields
            delegate: Tk.Flex {
                id: fieldRow
                required property var modelData
                required property int index
                property string fieldName: String(modelData.name || "")
                objectName: "formField_" + fieldName
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
                opacity: control.isHidden(fieldName) ? Tk.Theme.opacity.disabled : 1
                Tk.Caption { objectName: "formLabel_" + fieldRow.fieldName; text: control.caption(fieldRow.fieldName); width: Tk.Theme.size.labelWidth; wrapMode: Text.Wrap }
                Tk.Label { text: String(fieldRow.modelData.type || "").toUpperCase() + (fieldRow.modelData.notNull ? " · REQUIRED" : ""); muted: true; Tk.Flex.basis: Tk.Theme.size.fieldWidth / 2 }
                Tk.TextField {
                    objectName: "formValue_" + fieldRow.fieldName; visible: control.mode !== "design" && !String(fieldRow.modelData.type).toLowerCase().includes("bool")
                    enabled: control.mode === "edit"; readOnly: control.mode !== "edit"; text: String(control.workingRecord[fieldRow.fieldName] ?? "")
                    tooltip: control.caption(fieldRow.fieldName); Accessible.name: control.caption(fieldRow.fieldName)
                    onEditingFinished: control.setFieldValue(fieldRow.fieldName, text)
                    Tk.Flex.grow: 1
                }
                Tk.CheckBox {
                    objectName: "formBool_" + fieldRow.fieldName; visible: control.mode !== "design" && String(fieldRow.modelData.type).toLowerCase().includes("bool")
                    enabled: control.mode === "edit"; checked: !!control.workingRecord[fieldRow.fieldName]; text: control.caption(fieldRow.fieldName)
                    tooltip: control.caption(fieldRow.fieldName); onToggled: control.setFieldValue(fieldRow.fieldName, checked); Tk.Flex.grow: 1
                }
                Tk.TextField {
                    objectName: "formDesignLabel_" + fieldRow.fieldName; visible: control.mode === "design"; text: control.caption(fieldRow.fieldName)
                    tooltip: qsTr("Field label"); Accessible.name: qsTr("Label for %1").arg(fieldRow.fieldName); Tk.Flex.grow: 1
                    onEditingFinished: control.updateLabel(fieldRow.fieldName, text)
                }
                Tk.Badge { visible: control.mode === "design"; text: qsTr("binds to %1").arg(fieldRow.fieldName) }
                Tk.CheckBox {
                    objectName: "formShown_" + fieldRow.fieldName; visible: control.mode === "design"
                    text: qsTr("Shown"); checked: !control.isHidden(fieldRow.fieldName)
                    tooltip: qsTr("Show %1 on the form").arg(fieldRow.fieldName)
                    onToggled: control.setShown(fieldRow.fieldName, checked)
                }
                Tk.IconButton {
                    objectName: "formMoveUp_" + fieldRow.fieldName; visible: control.mode === "design"; iconName: "chevron-up"; small: true
                    enabled: fieldRow.index > 0; tooltip: qsTr("Move %1 up").arg(fieldRow.fieldName)
                    onClicked: control.moveField(fieldRow.fieldName, -1)
                }
                Tk.IconButton {
                    objectName: "formMoveDown_" + fieldRow.fieldName; visible: control.mode === "design"; iconName: "chevron-down"; small: true
                    enabled: fieldRow.index < control.displayFields.length - 1; tooltip: qsTr("Move %1 down").arg(fieldRow.fieldName)
                    onClicked: control.moveField(fieldRow.fieldName, 1)
                }
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
