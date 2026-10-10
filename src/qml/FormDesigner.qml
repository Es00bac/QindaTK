// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "FormControls.js" as FormControls

// Schema and record values are host snapshots. Edits are saved only by a
// host request; design mode never writes to the database.
// AGENT-CONTRACT (docs/forms-reports.md#designers): with `designDocument`
// set, the form layout is the document's form and every design edit
// (label, order, shown) is one LocalDataDesignDocument command, so undo,
// save and recovery cover it; without it the control only emits
// designChangeRequested and the host owns the design.
// AGENT-CONTRACT (D-311, docs/forms-reports.md#form-controls): positioned
// controls (`design.controls`: labels, bound text/combo/list/check boxes,
// subforms by form name, command buttons with an action id, tab controls,
// images, lines, rectangles, calculated text boxes with an opaque
// `expression`) are laid out on FormControlCanvas and edited in
// FormControlSheet. Each finished gesture or property edit is one
// LocalDataDesignDocument command, or one designChangeRequested("",
// {controls}) carrying the whole next list when the host owns the design.
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
    readonly property var controls: design.controls || []
    // The toolbox, canvas and property sheet; on when the design has controls.
    property bool controlsEnabled: controls.length > 0
    property string selectedControlId: ""
    // Design snap for moving and sizing controls (0: free).
    property real gridSize: Tk.Theme.space.md
    // Preview/edit: created in a subform's frame (FormControlLive's contract).
    property Component subformComponent: null
    // function(expression, record) -> value of a calculated control.
    property var expressionEvaluator: null
    readonly property Tk.LocalDataDesignStore _controlRules: Tk.LocalDataDesignStore { }
    signal designChangeRequested(string fieldName, var value)
    signal designSaveRequested(string requestId, var design)
    signal recordSaveRequested(string requestId, string tableName, string recordId, var changes)
    // A command button was pressed in preview or edit mode.
    signal controlCommandRequested(string controlId, string actionId)

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
    // AGENT-GUARD (docs/forms-reports.md#designers): record values change
    // only in edit mode, and an empty entry is null (never 0), so a required
    // number left blank fails validateRecord instead of saving a zero.
    function setFieldValue(name, value) {
        if (mode !== "edit") { requestState = "refused"; requestMessage = qsTr("Switch to edit mode to change the record"); return false }
        const field = (schemaSnapshot.fields || []).find(entry => String(entry.name) === name)
        if (!field) return false
        const next = Object.assign({}, workingRecord)
        const type = String(field.type || "").toLowerCase()
        if (value === "" || value === null || value === undefined) next[name] = null
        else if (type.includes("int") || type.includes("real") || type.includes("numeric") || type.includes("float") || type.includes("double")) {
            const text = String(value).trim()
            const number = Number(text)
            if (!text.length || !isFinite(number) || (type.includes("int") && !Number.isInteger(number))) {
                requestState = "invalid"; requestMessage = qsTr("%1 needs a number").arg(caption(name)); return false
            }
            next[name] = number
        } else next[name] = value
        workingRecord = next
        if (requestState === "invalid" || requestState === "refused") { requestState = "idle"; requestMessage = "" }
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
    // Design and preview never issue a record write, whatever calls this.
    function requestRecordSave() {
        if (mode !== "edit") { requestState = "refused"; requestMessage = qsTr("Records are saved only in edit mode"); return false }
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

    // --- form controls (D-311)
    function fieldNames() {
        return designDocument ? designDocument.boundFields : (schemaSnapshot.fields || []).map(field => String(field.name))
    }
    function controlById(controlId) {
        for (let i = 0; i < controls.length; ++i) if (String(controls[i].id) === String(controlId)) return controls[i]
        return ({})
    }
    function selectControl(controlId) { selectedControlId = controlId && controlById(controlId).id ? String(controlId) : "" }
    function controlTitle(type) {
        return ({ label: qsTr("Label"), textBox: qsTr("Text box"), comboBox: qsTr("Combo box"), listBox: qsTr("List box"),
                  checkBox: qsTr("Check box"), subform: qsTr("Subform"), commandButton: qsTr("Command button"),
                  tabControl: qsTr("Tab control"), image: qsTr("Image"), line: qsTr("Line"), rectangle: qsTr("Rectangle") })[type] || type
    }
    // Host-owned design: the whole next list, checked like a document's.
    function requestControls(next) {
        const problem = _controlRules.checkControls(next)
        if (problem.length) { requestState = "invalid"; requestMessage = problem; return false }
        designChangeRequested("", { controls: next })
        return true
    }
    function addControl(type, properties) {
        const props = Object.assign({}, properties || {}, { type: type })
        const at = FormControls.nextPosition(controls, gridSize)
        if (props.x === undefined) props.x = at.x
        if (props.y === undefined) props.y = at.y
        let id = ""
        if (designDocument) {
            id = designDocument.addFormControl(props)
            if (!id.length) { reportEdit(false); return "" }
        } else {
            const created = Object.assign(_controlRules.controlDefaults(type), props)
            if (!created.id) created.id = "ctl-" + Math.random().toString(16).slice(2, 14)
            if (!created.name) created.name = FormControls.uniqueName(controls, type)
            if (!requestControls(controls.concat([created]))) return ""
            id = created.id
        }
        selectedControlId = id
        return id
    }
    function updateControl(controlId, changes) {
        if (!controlById(controlId).id) return false
        if (designDocument) return reportEdit(designDocument.setFormControl(controlId, changes))
        return requestControls(controls.map(entry => String(entry.id) === String(controlId) ? FormControls.merged(entry, changes) : entry))
    }
    function replaceControls(next) {
        return designDocument ? reportEdit(designDocument.setFormControls(next)) : requestControls(next)
    }
    function removeControl(controlId) {
        if (!controlById(controlId).id) return false
        const ok = designDocument ? reportEdit(designDocument.removeFormControl(controlId))
                                  : requestControls(controls.filter(entry => entry.id !== controlId && entry.parentId !== controlId))
        if (ok && selectedControlId === controlId) selectedControlId = ""
        return ok
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
        Tk.Notice { visible: control.requestState === "invalid" || control.requestState === "rejected" || control.requestState === "refused"; variant: "danger"; title: qsTr("Form update"); text: control.requestMessage }
        Tk.Flex {
            objectName: "formToolbox"
            visible: control.mode === "design" && control.controlsEnabled
            direction: Tk.Flex.Row; wrap: Tk.Flex.Wrap; align: Tk.Flex.Center; gap: Tk.Theme.space.xs
            Tk.Caption { text: qsTr("Add") }
            Repeater {
                model: FormControls.types()
                delegate: Tk.IconButton {
                    required property string modelData
                    objectName: "formAdd_" + modelData
                    iconName: FormControls.iconFor(modelData)
                    small: true
                    tooltip: qsTr("Add %1").arg(control.controlTitle(modelData))
                    onClicked: control.addControl(modelData)
                }
            }
        }
        Tk.Flex {
            objectName: "formLayout"
            visible: control.controlsEnabled && (control.mode === "design" || control.controls.length > 0)
            direction: Tk.Flex.Row; gap: Tk.Theme.space.md; align: Tk.Flex.Start
            Tk.FormControlCanvas { designer: control; Tk.Flex.grow: 1 }
            Tk.FormControlSheet {
                designer: control
                visible: control.mode === "design" && control.selectedControlId.length > 0
                Tk.Flex.basis: Tk.Theme.size.panelMinWidth
                Tk.Flex.shrink: 0
            }
        }
        Repeater {
            model: control.displayFields
            delegate: Tk.Flex {
                id: fieldRow
                required property var modelData
                required property int index
                property string fieldName: String(modelData.name || "")
                objectName: "formField_" + fieldName
                // With positioned controls, preview and edit show the canvas only.
                visible: control.mode === "design" || !control.controlsEnabled || control.controls.length === 0
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
                opacity: control.isHidden(fieldName) ? Tk.Theme.opacity.disabled : 1
                // AGENT-NOTE: a Label's implicit width is read-only, so the label
                // column is sized by its flex basis (one column for every row).
                Tk.Caption {
                    objectName: "formLabel_" + fieldRow.fieldName; text: control.caption(fieldRow.fieldName)
                    wrapMode: Text.Wrap; Tk.Flex.basis: Tk.Theme.size.labelWidth; Tk.Flex.shrink: 0
                }
                // The column's type is design information; data entry shows only
                // whether the field is required.
                Tk.Label {
                    objectName: "formType_" + fieldRow.fieldName
                    visible: control.mode === "design"
                    text: String(fieldRow.modelData.type || "").toUpperCase() + (fieldRow.modelData.notNull ? " · REQUIRED" : "")
                    muted: true; Tk.Flex.basis: Tk.Theme.size.fieldWidth / 2; Tk.Flex.shrink: 0
                }
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
            Tk.Label {
                objectName: "formStatus"
                text: control.requestMessage || (control.requestState === "idle" ? "" : control.requestState)
                muted: true; Tk.Flex.grow: 1
            }
            Tk.Button { objectName: "formSaveDesign"; visible: control.mode === "design"; text: qsTr("Save form design"); tooltip: qsTr("Request host to persist form design"); onClicked: control.requestDesignSave() }
            Tk.Button { objectName: "formSaveRecord"; visible: control.mode === "edit"; text: qsTr("Save record"); variant: "accent"; tooltip: qsTr("Request a validated record update"); onClicked: control.requestRecordSave() }
        }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
