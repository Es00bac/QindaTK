// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// D-311: FormDesigner's positioned controls — every type added from the
// toolbox, selected, moved and resized on the canvas (snapped, one undo step
// per gesture), edited in the property sheet, and live in preview and edit
// mode (bound values, calculated controls, command buttons, subforms, tab
// pages); without a design document the host receives the whole next list.
TestCase {
    id: root
    name: "FormDesignerControls"
    visible: true
    when: windowShown
    width: 900
    height: 640
    property Tk.LocalDatabaseProvider provider: Tk.LocalDatabaseProvider {}

    Component { id: spyComponent; SignalSpy {} }
    Component { id: documentComponent; Tk.LocalDataDesignDocument {} }
    Component {
        id: formComponent
        Tk.FormDesigner {
            width: 860
            provider: root.provider
            tableName: "orders"
            recordId: "1"
            record: ({ id: 1, region: "North", quantity: 3, price: 2.5, paid: 0 })
            gridSize: 8
        }
    }
    Component {
        id: childFormComponent
        Rectangle {
            property string formName: ""
            property string linkMasterFields: ""
            property var masterRecord: ({})
            objectName: "hostSubform"
            color: "transparent"
        }
    }

    function init() {
        verify(provider.openMemory())
        verify(provider.executeMigration("CREATE TABLE orders(id INTEGER PRIMARY KEY, region TEXT NOT NULL, quantity INTEGER, price REAL, paid INTEGER)").ok)
        verify(provider.execute("INSERT INTO orders VALUES(?, ?, ?, ?, ?)", [1, "North", 3, 2.5, 0]).ok)
    }
    function cleanup() { provider.close() }

    function makeForm(withDocument) {
        const form = createTemporaryObject(formComponent, root)
        verify(form)
        if (withDocument) {
            const doc = createTemporaryObject(documentComponent, root)
            verify(doc.bindTable(provider, "orders"))
            form.designDocument = doc
        }
        form.controlsEnabled = true
        waitForRendering(form)
        return form
    }
    function named(item, name) { return findChild(item, name) }

    function test_toolbox_adds_every_type_and_the_sheet_edits_it() {
        const form = makeForm(true)
        const doc = form.designDocument
        verify(named(form, "formToolbox").visible)
        const types = ["label", "textBox", "comboBox", "listBox", "checkBox", "subform",
                       "commandButton", "tabControl", "image", "line", "rectangle"]
        for (let i = 0; i < types.length; ++i) {
            const add = named(form, "formAdd_" + types[i])
            verify(add !== null, types[i])
            mouseClick(add)
        }
        compare(form.controls.length, types.length)
        compare(doc.form.controls.length, types.length)
        // New controls stack downwards on the grid; the last one is selected.
        compare(form.controls[1].y % 8, 0)
        verify(form.controls[1].y > form.controls[0].y)
        compare(form.selectedControlId, form.controls[10].id)
        waitForRendering(form)
        verify(named(form, "formPropertySheet").visible)

        // Bind the combo box and give it items through the sheet.
        const combo = form.controls[2]
        form.selectControl(combo.id)
        waitForRendering(form)
        const field = named(form, "formControlField")
        verify(field.visible)
        field.currentIndex = field.model.indexOf("region")
        field.activated(field.currentIndex)
        compare(form.controlById(combo.id).fieldName, "region")
        const items = named(form, "formControlItems")
        items.text = "North; South ; East"
        items.editingFinished()
        const stored = form.controlById(combo.id).items
        compare(stored.length, 3)
        compare(stored[0], "North")
        compare(stored[1], "South")
        compare(stored[2], "East")
        compare(doc.undoText, "Change Combo1")
        // A calculated text box keeps an opaque expression.
        form.selectControl(form.controls[1].id)
        const expression = named(form, "formControlExpression")
        expression.text = "=[quantity]*[price]"
        expression.editingFinished()
        compare(form.controlById(form.controls[1].id).expression, "=[quantity]*[price]")
        // A command button's action id, a subform's form name, tab pages.
        form.selectControl(form.controls[6].id)
        named(form, "formControlAction").text = "printOrderAction"
        named(form, "formControlAction").editingFinished()
        compare(form.controls[6].actionId, "printOrderAction")
        form.selectControl(form.controls[5].id)
        named(form, "formControlFormName").text = "Order lines"
        named(form, "formControlFormName").editingFinished()
        compare(form.controls[5].formName, "Order lines")
        form.selectControl(form.controls[7].id)
        named(form, "formControlPages").text = "General; Lines; Notes"
        named(form, "formControlPages").editingFinished()
        compare(form.controls[7].pages.length, 3)
        // The check box goes on the tab control's second page.
        form.selectControl(form.controls[4].id)
        const place = named(form, "formControlPlace")
        verify(place.visible)
        place.currentIndex = 2
        place.activated(2)
        compare(form.controls[4].parentId, form.controls[7].id)
        compare(form.controls[4].page, 1)
        // Geometry fields and delete; undo restores.
        named(form, "formControl_width").valueModified(200)
        compare(form.controls[4].width, 200)
        mouseClick(named(form, "formControlDelete"))
        compare(form.controls.length, 10)
        compare(form.selectedControlId, "")
        verify(doc.undo())
        compare(form.controls.length, 11)
    }

    function test_canvas_selects_moves_and_resizes_on_the_grid() {
        const form = makeForm(true)
        const doc = form.designDocument
        const id = form.addControl("textBox", { x: 16, y: 16, width: 160, height: 24, fieldName: "region" })
        verify(id.length > 0)
        form.selectControl("")
        waitForRendering(form)
        const canvas = named(form, "formCanvas")
        const shape = named(canvas, "formControl_" + id)
        verify(shape !== null)
        compare(shape.x, 16)
        mouseClick(shape, 20, 12)
        compare(form.selectedControlId, id)
        verify(named(canvas, "formSelection").visible)
        // Drag: 37 px right and 21 down snap to 40 and 24 — one undo step.
        // Drags are given in canvas coordinates: the dragged item follows the pointer.
        mouseDrag(canvas, 16 + 20, 16 + 12, 37, 21)
        tryVerify(function() { return form.controlById(id).x === 56 })
        compare(form.controlById(id).y, 40)
        compare(doc.undoText, "Move or size Text1")
        // Resize from the east handle by 30 px: width 160 -> 192 (snapped right edge).
        waitForRendering(form)
        const east = named(canvas, "formHandle_e")
        verify(east !== null && east.visible)
        const grip = east.mapToItem(canvas, east.width / 2 + 2, east.height / 2)
        compare(grip.x, 56 + 160 + 2)
        mouseDrag(canvas, grip.x, grip.y, 30, 0)
        tryVerify(function() { return form.controlById(id).width === 192 })
        compare(form.controlById(id).x, 56)
        // Keyboard: arrows nudge by the grid, Shift+arrows size, Delete removes.
        canvas.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(form.controlById(id).x, 64)
        keyClick(Qt.Key_Down, Qt.ShiftModifier)
        compare(form.controlById(id).height, 32)
        keyClick(Qt.Key_Escape)
        compare(form.selectedControlId, "")
        keyClick(Qt.Key_Tab)
        compare(form.selectedControlId, id)
        keyClick(Qt.Key_Delete)
        compare(form.controls.length, 0)
        verify(doc.undo())
        compare(form.controls.length, 1)
        // Clicking empty canvas clears the selection.
        form.selectControl(id)
        mouseClick(canvas, canvas.width - 4, canvas.height - 4)
        compare(form.selectedControlId, "")
    }

    function test_preview_and_edit_run_the_controls() {
        const form = makeForm(true)
        const total = form.addControl("textBox", { x: 8, y: 8, expression: "=[quantity]*[price]" })
        const region = form.addControl("comboBox", { x: 8, y: 40, fieldName: "region", items: ["North", "South"] })
        const button = form.addControl("commandButton", { x: 8, y: 72, caption: "Print", actionId: "printOrderAction" })
        const tabs = form.addControl("tabControl", { x: 8, y: 104, width: 320, height: 120, pages: ["One", "Two"] })
        const paid = form.addControl("checkBox", { x: 16, y: 136, fieldName: "paid", caption: "Paid", parentId: tabs, page: 1 })
        const sub = form.addControl("subform", { x: 340, y: 8, formName: "Order lines", linkMasterFields: "id" })
        form.expressionEvaluator = function(expression, record) { return Number(record.quantity) * Number(record.price) }
        form.subformComponent = childFormComponent
        form.mode = "preview"
        waitForRendering(form)
        verify(!named(form, "formField_region").visible)
        compare(named(form, "formLive_" + total).text, "7.5")
        compare(named(form, "formLive_" + region).currentIndex, 0)
        compare(named(form, "formLive_" + region).enabled, false)
        const hosted = named(form, "hostSubform")
        verify(hosted !== null)
        compare(hosted.formName, "Order lines")
        compare(hosted.linkMasterFields, "id")
        // Tab pages show their own controls.
        verify(!named(form, "formControl_" + paid).visible)
        const strip = named(named(form, "formLive_" + tabs), "formLiveTabs")
        strip.tabActivated(1)
        verify(named(form, "formControl_" + paid).visible)
        const pressed = createTemporaryObject(spyComponent, root, { target: form, signalName: "controlCommandRequested" })
        mouseClick(named(form, "formLive_" + button))
        compare(pressed.count, 1)
        compare(pressed.signalArguments[0][0], button)
        compare(pressed.signalArguments[0][1], "printOrderAction")
        // Edit mode writes through setFieldValue.
        form.mode = "edit"
        waitForRendering(form)
        const combo = named(form, "formLive_" + region)
        verify(combo.enabled)
        combo.activated(1)
        compare(form.workingRecord.region, "South")
        verify(form.workingRecord.region !== form.record.region)
        const paidBox = named(form, "formLive_" + paid)
        paidBox.checked = true
        paidBox.toggled()
        compare(form.workingRecord.paid, 1)
        compare(named(form, "formLive_" + total).readOnly, true)
        verify(sub.length > 0)
    }

    function test_host_owned_design_receives_whole_control_lists() {
        const form = makeForm(false)
        const changed = createTemporaryObject(spyComponent, root, { target: form, signalName: "designChangeRequested" })
        const id = form.addControl("label", { caption: "Order" })
        verify(id.length > 0)
        compare(changed.count, 1)
        compare(changed.signalArguments[0][0], "")
        const next = changed.signalArguments[0][1].controls
        compare(next.length, 1)
        compare(next[0].name, "Label1")
        compare(next[0].caption, "Order")
        form.design = Object.assign({}, form.design, { controls: next })
        verify(form.updateControl(id, { x: 48 }))
        compare(changed.signalArguments[1][1].controls[0].x, 48)
        verify(!form.updateControl(id, { width: -5 }))
        compare(form.requestState, "invalid")
        compare(changed.count, 2)
    }
}
