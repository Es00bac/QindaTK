// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "OfficeExtensions"
    visible: true
    when: windowShown
    width: 900; height: 700
    Component { id: numberComponent; Tk.NumberField {
        property bool metric: false
        from: metric ? 0.2 : 0.1; to: metric ? 25 : 10
        decimals: 2; value: 0.58
    } }
    Component { id: previewComponent; Tk.PrintPreview { width: 520 } }
    Component { id: scrollPreviewComponent; Tk.Scroll { width: 414; height: 360; Tk.PrintPreview {} } }
    Component { id: toastComponent; Tk.Toast { title: "Saved"; message: "A longer message which needs intrinsic measurement." } }
    Component { id: docComponent; Tk.LocalDataDesignDocument {} }
    Component { id: formComponent; Tk.FormDesigner { width: 700 } }
    Component { id: borderComponent; Tk.BorderEditor { width: 380; onBordersEdited: next => borders = next } }
    Component { id: taskComponent; Tk.TaskBoard { width: 500; columns: [
        { status: "todo", label: "To do", tasks: [{ taskId: "one", title: "Invoice", categories: ["Finance"],
            extraFields: [{label: "Customer", value: "Example"}] }] } ]; categoryColors: ({Finance: "#996633"})
    } }
    Component { id: dockComponent; Tk.DockHost {
        width: 900; height: 300
        canvas: Item {}
        Tk.DockPanel { panelId: "left"; zone: "left"; extent: 300; title: "Left"; Item {} }
        Tk.DockPanel { panelId: "right"; zone: "right"; extent: 300; title: "Right"; Item {} }
    } }
    Component { id: gridComponent; Tk.EditableGrid {
        width: 500; height: 200
        model: Tk.EditableGridModel { Component.onCompleted: setDimensions(2, 2) }
        valueList: (rowId, columnId) => ({ values: ["One", "Two"], limitToList: true })
    } }
    Component { id: layerComponent; Tk.LayerTree {
        width: 350
        model: ListModel {
            property string lastError: ""
            property string activatedKey: ""
            signal structureChanged()
            signal revealRequested(string key)
            function groupKeys() { return [] }
            function ancestorKeys(key) { return [] }
            function nodeInfo(key) { return get(0) }
            function activate(key) { activatedKey = key; return true }
            function select(key, toggle) { activatedKey = key; return true }
            function setShown(key, shown) { setProperty(0, "shown", shown); return true }
            function setLocked(key, locked) { setProperty(0, "locked", locked); return true }
            ListElement { key: "layer:one"; kind: "layer"; itemId: "one"; label: "Independent layer";
                detail: ""; iconName: "layers"; shown: true; locked: false; effectiveVisible: true;
                effectiveLocked: false; selected: false; active: true; childCount: 0; parentKey: "" }
        }
    } }
    function test_layer_model_without_canvas_document() {
        const tree = createTemporaryObject(layerComponent, root)
        verify(tree)
        waitForRendering(tree)
        compare(tree.collection.treeCount, 1)
        verify(tree.activateLayer("one"))
        compare(tree.model.activatedKey, "layer:one")
        verify(tree.setVisibility("one", false))
        compare(tree.model.get(0).shown, false)
        compare(tree.document, null)
    }
    function init() { failOnWarning(/Binding loop detected/) }
    function test_decimal_bound_range_does_not_reenter_binding() {
        const field = createTemporaryObject(numberComponent, root)
        verify(field)
        tryCompare(field.inputItem, "text", "0.58")
        field.metric = true
        field.value = 30
        tryCompare(field.inputItem, "text", "25.00")
        compare(field.value, 30)
        field.metric = false
        tryCompare(field.inputItem, "text", "10.00")
    }
    function test_toast_measures_and_clamps() {
        const toast = createTemporaryObject(toastComponent, root)
        verify(toast)
        verify(toast.implicitWidth > toast.minimumWidth)
        verify(toast.implicitWidth <= toast.availableWidth)
        toast.maximumWidth = 250
        compare(toast.implicitWidth, 250)
        toast.show()
        waitForRendering(toast)
        verify(toast.implicitHeight > 0)
    }
    function test_form_switching_and_loaded_design() {
        const form = createTemporaryObject(formComponent, root)
        const first = createTemporaryObject(docComponent, root)
        const second = createTemporaryObject(docComponent, root)
        verify(first && second && form)
        form.designDocument = first
        verify(first.setFormTitle("First"))
        verify(first.undo())
        tryCompare(findChild(form, "formRedo"), "enabled", true)
        form.designDocument = second
        tryCompare(findChild(form, "formRedo"), "enabled", false)
        verify(second.setDesign(first.design))
        form.designDocument = first
        tryCompare(findChild(form, "formRedo"), "enabled", true)
    }
    function test_border_edges_keep_independent_styles() {
        const border = createTemporaryObject(borderComponent, root)
        verify(border)
        border.lineColor = "#123456"; border.lineWidth = 2.5; border.lineStyle = "dashed"
        border.toggleEdge("top")
        compare(border.borders.top.width, 2.5)
        compare(border.borders.top.style, "dashed")
        border.lineStyle = "double"
        border.toggleEdge("insideVertical")
        compare(border.borders.top.style, "dashed")
        compare(border.borders.insideVertical.style, "double")
        const edge = findChild(border, "borderEdge_top")
        edge.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryVerify(() => !border.borders.top)
        verify(border.borders.insideVertical)
    }
    function test_print_settings_use_host_width_without_feedback() {
        const preview = createTemporaryObject(previewComponent, root)
        verify(preview)
        const grid = findChild(preview, "printSettingsGrid")
        preview.width = 900
        tryCompare(grid, "twoUp", true)
        preview.width = 320
        tryCompare(grid, "twoUp", false)
    }
    function test_print_settings_inside_scroll_do_not_reenter_layout() {
        const scroll = createTemporaryObject(scrollPreviewComponent, root)
        verify(scroll)
        waitForRendering(scroll)
        scroll.width = 700
        waitForRendering(scroll)
        scroll.width = 414
        waitForRendering(scroll)
        tryCompare(findChild(scroll, "printSettingsGrid"), "twoUp", false)
    }
    function test_task_card_metadata() {
        const board = createTemporaryObject(taskComponent, root)
        verify(board)
        waitForRendering(board)
        verify(findChild(board, "taskCategory_Finance") !== null)
        compare(findChild(board, "taskCategorySwatch_Finance").width, Tk.Theme.size.iconSm)
        compare(findChild(board, "taskExtraField_Customer").text, "Customer: Example")
    }
    function test_two_docks_shrink_without_mutating_saved_extent() {
        const dock = createTemporaryObject(dockComponent, root)
        verify(dock)
        waitForRendering(dock)
        compare(dock.laneExtent("left", 0), 300)
        dock.width = 520
        waitForRendering(dock)
        const left = findChild(dock, "dockZone_left")
        const right = findChild(dock, "dockZone_right")
        const rightEdge = right.mapToItem(dock, right.width, 0)
        verify(left.width > 0 && right.width > 0)
        verify(rightEdge.x <= dock.width + 1)
        verify(dock.canvasItem.width > 0)
        compare(dock.laneExtent("left", 0), 300)
        dock.width = 900
        waitForRendering(dock)
        compare(dock.visibleSideExtent("left", 0), 300)
    }
    function test_value_list_limit_and_keyboard() {
        const grid = createTemporaryObject(gridComponent, root)
        verify(grid)
        waitForRendering(grid)
        grid.setCurrent(0, 0, false)
        verify(grid.beginCellEdit())
        verify(!grid.commitCellEdit("Outside"))
        compare(grid.cellEditing, true)
        const picker = findChild(grid, "gridValueListEditor")
        verify(picker.visible)
        picker.forceActiveFocus()
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Return)
        tryCompare(grid, "cellEditing", false)
        verify(["One", "Two"].indexOf(grid.model.cellValue(grid.activeRowId, grid.activeColumnId)) >= 0)
        grid.valueList = () => ({ values: ["One", "Two"], limitToList: false })
        verify(grid.beginCellEdit())
        verify(grid.commitCellEdit("Outside"))
        compare(grid.model.cellValue(grid.activeRowId, grid.activeColumnId), "Outside")
    }
}
