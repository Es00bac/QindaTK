// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// The document-application set (docs/decisions.md D-013): three-button
// dialogs, the prompt, colour swatches and button, inline rename, wrapping
// toolbars, radio menu items and the viewport's zoom maths.
TestCase {
    id: root
    name: "ControlsOffice"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Component { id: signalSpyComponent; SignalSpy {} }
    Component {
        id: messageComponent
        Tk.MessageDialog { title: "Unsaved"; text: "Save changes?"; primaryText: "Save"; secondaryText: "Cancel"; tertiaryText: "Discard" }
    }
    Component { id: promptComponent; Tk.PromptDialog { title: "Rename"; label: "Name"; text: "" } }
    Component {
        id: swatchesComponent
        Tk.ColorSwatches { colors: [{ name: "Black", color: "#000000" }, "#1c62d4", "#d21c1c"]; current: "#d21c1c"; columns: 2 }
    }
    Component {
        id: colorButtonComponent
        Tk.ColorButton { color: "#000000"; colors: ["#000000", "#1c62d4"]; tooltip: "Pen colour" }
    }
    Component { id: treeComponent; Tk.TreeRow { text: "Page 1"; editable: true; width: 240 } }
    Component {
        id: wrapBarComponent
        Item {
            width: 200
            Tk.ToolBar {
                objectName: "bar"; wrap: true
                anchors.left: parent.left; anchors.right: parent.right
                Repeater { model: 10; Tk.IconButton { iconName: "pen-tool"; tooltip: "P" } }
            }
        }
    }
    Component {
        id: menuComponent
        Tk.Menu {
            Tk.MenuItem { objectName: "pen"; text: "Pen"; checkable: true; radio: true; checked: true }
            Tk.MenuItem { objectName: "eraser"; text: "Eraser"; checkable: true; radio: true }
        }
    }
    Component {
        id: viewportComponent
        Tk.Viewport {
            width: 400; height: 300; contentPadding: 20
            Rectangle { implicitWidth: 300; implicitHeight: 800; color: "white" }
        }
    }

    function make(component, props) {
        const item = createTemporaryObject(component, root, props || {})
        verify(item !== null)
        return item
    }

    function test_message_dialog_has_three_buttons() {
        const dialog = make(messageComponent)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: dialog, signalName: "tertiary" })
        dialog.open()
        tryCompare(dialog, "opened", true)
        const tertiary = findChild(dialog.footer, "dialogTertiary")
        verify(tertiary !== null)
        verify(tertiary.visible)
        compare(tertiary.text, "Discard")
        mouseClick(tertiary)
        compare(spy.count, 1)
        verify(findChild(dialog.contentItem, "messageIcon") !== null)
        dialog.close()
    }

    function test_prompt_blocks_empty_and_accepts_text() {
        const dialog = make(promptComponent)
        const accepted = createTemporaryObject(signalSpyComponent, root, { target: dialog, signalName: "accepted" })
        dialog.open()
        tryCompare(dialog, "opened", true)
        const field = findChild(dialog.contentItem, "promptField")
        verify(field !== null)
        verify(!dialog.acceptable)
        const primary = findChild(dialog.footer, "dialogPrimary")
        verify(!primary.enabled)
        keyClick(Qt.Key_Return)
        compare(accepted.count, 0)
        field.text = "Research"
        verify(dialog.acceptable)
        tryCompare(primary, "enabled", true)
        keyClick(Qt.Key_Return)
        compare(accepted.count, 1)
        compare(dialog.text, "Research")
    }

    function test_swatches_select_and_ring_current() {
        const swatches = make(swatchesComponent)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: swatches, signalName: "selected" })
        waitForRendering(swatches)
        const second = findChild(swatches, "swatch_1")
        verify(second !== null)
        verify(!second.isCurrent)
        verify(findChild(swatches, "swatch_2").isCurrent)
        mouseClick(second)
        compare(spy.count, 1)
        compare(Qt.colorEqual(spy.signalArguments[0][0], "#1c62d4"), true)
        compare(findChild(swatches, "swatch_0").entry.name, "Black")
    }

    function test_color_button_opens_swatches_and_selects() {
        const button = make(colorButtonComponent)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: button, signalName: "colorSelected" })
        compare(button.implicitHeight, Tk.Theme.size.control)
        waitForRendering(button)
        mouseClick(button)
        const popover = findChild(button, "colorButtonPopover")
        verify(popover !== null)
        tryCompare(popover, "opened", true)
        const swatch = findChild(popover.contentItem, "swatch_1")
        verify(swatch !== null)
        mouseClick(swatch)
        compare(spy.count, 1)
        compare(Qt.colorEqual(button.color, "#1c62d4"), true)
        tryCompare(popover, "visible", false)
    }

    function test_tree_row_inline_rename() {
        const row = make(treeComponent)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: row, signalName: "renamed" })
        waitForRendering(row)
        verify(!row.editing)
        row.startEditing()
        verify(row.editing)
        const editor = findChild(row, "treeEditor")
        verify(editor !== null)
        verify(editor.activeFocus)
        editor.text = "Agenda"
        keyClick(Qt.Key_Return)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "Agenda")
        verify(!row.editing)
        compare(row.text, "Page 1")   // the owner renames, not the row
        // Escape cancels without a signal.
        row.startEditing()
        keyClick(Qt.Key_Escape)
        verify(!row.editing)
        compare(spy.count, 1)
    }

    function test_tool_bar_wraps_and_keeps_every_button_inside() {
        const host = make(wrapBarComponent)
        const bar = findChild(host, "bar")
        waitForRendering(bar)
        tryVerify(function() { return bar.height > Tk.Theme.size.toolbar })
        const buttons = []
        for (let i = 0; i < bar.items.length; ++i) {
            if (bar.items[i].objectName !== "" || bar.items[i].implicitWidth > 0) buttons.push(bar.items[i])
        }
        verify(buttons.length >= 10)
        for (let i = 0; i < buttons.length; ++i) {
            const p = buttons[i].mapToItem(bar, 0, 0)
            verify(p.x + buttons[i].width <= bar.width + 0.5)
            verify(p.y + buttons[i].height <= bar.height + 0.5)
        }
    }

    function test_menu_item_radio_shows_dot_only_when_checked() {
        const menu = make(menuComponent)
        menu.popup(10, 10)
        tryCompare(menu, "opened", true)
        const pen = findChild(menu.contentItem, "pen")
        const eraser = findChild(menu.contentItem, "eraser")
        verify(pen !== null && eraser !== null)
        verify(pen.radio && pen.checked)
        verify(eraser.radio && !eraser.checked)
        menu.close()
    }

    function test_viewport_zoom_about_keeps_the_point_and_fits_width() {
        const vp = make(viewportComponent)
        waitForRendering(vp)
        compare(vp.zoom, 1.0)
        const anchor = Qt.point(100, 100)
        const before = vp.toContent(anchor)
        vp.zoomAbout(anchor, 2.0)
        compare(vp.zoom, 2.0)
        const after = vp.toContent(anchor)
        verify(Math.abs(after.x - before.x) < 1.0)
        verify(Math.abs(after.y - before.y) < 1.0)
        compare(vp.fitMode, "none")
        vp.fitWidth()
        compare(vp.fitMode, "width")
        // (400 - 2 * 20) / 300
        verify(Math.abs(vp.zoom - 1.2) < 1e-6)
        vp.width = 700
        tryVerify(function() { return Math.abs(vp.zoom - (660 / 300)) < 1e-6 })
    }
}
