// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Popup focus and host bounds are tested together because T.Popup controls
// share the window overlay and keyboard focus chain.
TestCase {
    id: root
    name: "PopupFocus"
    when: windowShown
    visible: true
    width: 280
    height: 180

    Tk.Button {
        id: origin
        objectName: "popupFocusOrigin"
        text: "Open"
        x: Tk.Theme.space.sm
        y: Tk.Theme.space.sm
    }

    Tk.Dialog {
        id: dialog
        title: "Confirm"
        primaryText: "Continue"
        secondaryText: "Cancel"
        Tk.Label { text: "A dialog body" }
    }

    Tk.Popover {
        id: popover
        anchorItem: origin
        Tk.Button { text: "Popover action" }
    }

    Item {
        id: movablePanel
        width: 120
        height: 40
        Tk.Button { id: movableAnchor; text: "Panel action" }
        Tk.Popover {
            id: movablePopover
            anchorItem: movableAnchor
            Tk.Label { text: "Panel popover" }
        }
        Tk.Menu {
            id: movableMenu
            Tk.MenuItem { text: "Panel menu action" }
        }
        Tk.Dialog { id: movableDialog; title: "Panel dialog"; hostItem: movableAnchor }
        Tk.CommandPalette {
            id: movablePalette
            hostItem: movableAnchor
            commands: [{ id: "panel.action", label: "Panel action" }]
        }
    }
    Window { id: secondWindow; width: 280; height: 180; visible: false }

    Tk.CommandPalette {
        id: palette
        commands: [
            { id: "one", label: "First command", section: "Commands" },
            { id: "two", label: "Second command", section: "Commands" }
        ]
    }

    Tk.Menu {
        id: menu
        Tk.Menu {
            id: nestedMenu
            title: "Parent"
            Tk.MenuItem { text: "Nested action" }
            Tk.MenuItem { text: "Another action" }
        }
        Tk.MenuItem { text: "Other" }
        Tk.MenuItem { text: "Third" }
        Tk.MenuItem { text: "Fourth" }
        Tk.MenuItem { text: "Fifth" }
        Tk.MenuItem { text: "Sixth" }
        Tk.MenuItem { text: "Seventh" }
        Tk.MenuItem { text: "Eighth" }
        Tk.MenuItem { text: "Ninth" }
    }

    Component { id: signalSpyComponent; SignalSpy {} }

    function focusOrigin() {
        origin.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return root.Window.window.activeFocusItem === origin })
    }

    function test_popover_follows_panel_into_native_window() {
        secondWindow.show()
        try {
            movablePanel.parent = secondWindow.contentItem
            movablePopover.open()
            tryVerify(function() { return movablePopover.opened })
            compare(movablePopover.parent.Window.window, secondWindow)
            movablePopover.close()
            movableMenu.popup()
            tryVerify(function() { return movableMenu.opened })
            compare(movableMenu.parent.Window.window, secondWindow)
            movableMenu.close()
            movableDialog.open()
            tryVerify(function() { return movableDialog.opened })
            compare(movableDialog.parent.Window.window, secondWindow)
            movableDialog.close()
            movablePalette.open()
            tryVerify(function() { return movablePalette.opened })
            compare(movablePalette.parent.Window.window, secondWindow)
            movablePalette.close()
        } finally {
            movablePopover.close()
            movableMenu.close()
            movableDialog.close()
            movablePalette.close()
            movablePanel.parent = root
            secondWindow.hide()
            root.Window.window.requestActivate()
            origin.forceActiveFocus()
        }
    }

    function test_dialog_focus_entry_escape_and_return() {
        focusOrigin()
        dialog.open()
        tryVerify(function() { return dialog.opened })
        const primary = findChild(dialog.footer, "dialogPrimary")
        verify(primary !== null)
        tryVerify(function() { return primary.activeFocus })
        keyClick(Qt.Key_Escape)
        tryVerify(function() { return !dialog.visible })
        tryVerify(function() { return root.Window.window.activeFocusItem === origin })
    }

    function test_palette_focus_entry_escape_and_return() {
        focusOrigin()
        palette.open()
        tryVerify(function() { return palette.opened })
        const input = findChild(palette.contentItem, "paletteInput")
        verify(input !== null)
        tryVerify(function() { return input.activeFocus })
        keyClick(Qt.Key_Escape)
        tryVerify(function() { return !palette.visible })
        tryVerify(function() { return root.Window.window.activeFocusItem === origin })
    }

    function test_popover_escape_keeps_anchor_focus() {
        focusOrigin()
        popover.open()
        tryVerify(function() { return popover.opened })
        tryVerify(function() { return root.Window.window.activeFocusItem !== origin })
        keyClick(Qt.Key_Escape)
        tryVerify(function() { return !popover.visible })
        compare(root.Window.window.activeFocusItem, origin)
    }

    function test_menu_escape_returns_focus_to_opener() {
        focusOrigin()
        menu.popup(12, 12)
        tryVerify(function() { return menu.opened })
        tryVerify(function() { return root.Window.window.activeFocusItem !== origin })
        keyClick(Qt.Key_Escape)
        tryVerify(function() { return !menu.visible })
        tryVerify(function() { return root.Window.window.activeFocusItem === origin })
    }

    function test_nested_menu_is_reachable_and_escape_returns_through_parent() {
        const host = root.Window.window
        const oldWidth = host.width
        const oldHeight = host.height
        try {
            host.width = 220
            host.height = 140
            focusOrigin()
            menu.popup(160, 88)
            tryVerify(function() { return menu.opened && menu.width > 0 && menu.height > 0 })
            verify(menu.x >= 0)
            verify(menu.y >= 0)
            verify(menu.x + menu.width <= host.contentItem.width + 0.5)
            verify(menu.y + menu.height <= host.contentItem.height + 0.5)

            keyClick(Qt.Key_Down)
            tryVerify(function() { return menu.currentIndex === 0 })
            keyClick(Qt.Key_Right)
            tryVerify(function() { return nestedMenu.opened })
            verify(nestedMenu.width > 0 && nestedMenu.height > 0)
            const nestedOrigin = nestedMenu.parent.mapToItem(
                        host.contentItem, nestedMenu.x, nestedMenu.y)
            verify(nestedOrigin.x >= 0 && nestedOrigin.y >= 0)
            verify(nestedOrigin.x + nestedMenu.width <= host.contentItem.width + 0.5)
            verify(nestedOrigin.y + nestedMenu.height <= host.contentItem.height + 0.5)
            keyClick(Qt.Key_Escape)
            tryVerify(function() { return !nestedMenu.visible && menu.opened })
            tryVerify(function() { return menu.currentIndex === 0 })
            tryVerify(function() {
                return host.activeFocusItem && host.activeFocusItem.text === "Parent"
            })

            for (let index = 0; index < menu.count - 1; ++index)
                keyClick(Qt.Key_Down)
            tryVerify(function() { return menu.currentIndex === menu.count - 1 })
            verify(menu.contentItem.contentY > 0)

            keyClick(Qt.Key_Escape)
            tryVerify(function() { return !menu.visible })
            tryVerify(function() { return host.activeFocusItem === origin })
        } finally {
            nestedMenu.close()
            menu.close()
            host.width = oldWidth
            host.height = oldHeight
        }
    }

    function test_small_host_keeps_modal_popup_reachable() {
        const host = root.Window.window
        const oldWidth = host.width
        const oldHeight = host.height
        try {
            host.width = 220
            host.height = 140
            wait(20)
            dialog.open()
            tryVerify(function() { return dialog.opened && dialog.width > 0 && dialog.height > 0 })
            verify(dialog.x >= -0.5)
            verify(dialog.y >= -0.5)
            verify(dialog.x + dialog.width <= host.contentItem.width + 0.5)
            verify(dialog.y + dialog.height <= host.contentItem.height + 0.5)
            dialog.close()
            tryVerify(function() { return !dialog.visible })

            const manyCommands = []
            for (let i = 0; i < 18; ++i)
                manyCommands.push({ id: "cmd" + i, label: "Command " + i })
            palette.commands = manyCommands
            palette.open()
            tryVerify(function() { return palette.opened && palette.width > 0 && palette.height > 0 })
            verify(palette.x >= -0.5)
            verify(palette.y >= -0.5)
            verify(palette.x + palette.width <= host.contentItem.width + 0.5)
            verify(palette.y + palette.height <= host.contentItem.height + 0.5)
            const list = findChild(palette.contentItem, "paletteList")
            verify(list !== null)
            tryVerify(function() { return list.contentHeight > list.height && list.height > 0 })
            for (let i = 0; i < manyCommands.length; ++i)
                keyClick(Qt.Key_Down)
            compare(palette.currentRow, manyCommands.length - 1)
            verify(list.contentY > 0)
            palette.close()
            tryVerify(function() { return !palette.visible })
        } finally {
            dialog.close()
            palette.close()
            host.width = oldWidth
            host.height = oldHeight
        }
    }

    function test_palette_fits_below_its_offset_at_large_text() {
        const host = root.Window.window
        const oldWidth = host.width
        const oldHeight = host.height
        const oldTextScale = Tk.Density.textScale
        try {
            host.width = 220
            host.height = 300
            Tk.Density.textScale = 2
            const manyCommands = []
            for (let i = 0; i < 18; ++i)
                manyCommands.push({ id: "cmd" + i, label: "Command " + i })
            palette.commands = manyCommands
            palette.open()
            tryVerify(function() { return palette.opened && palette.height > 0 })
            verify(palette.y + palette.height <= host.contentItem.height + 0.5)
            const list = findChild(palette.contentItem, "paletteList")
            verify(list !== null)
            tryVerify(function() { return list.contentHeight > list.height && list.height > 0 })
        } finally {
            palette.close()
            Tk.Density.textScale = oldTextScale
            host.width = oldWidth
            host.height = oldHeight
        }
    }

    function test_palette_accessible_rows_skip_disabled_commands() {
        const activated = createTemporaryObject(signalSpyComponent, root,
                                                { target: palette, signalName: "activated" })
        palette.commands = [
            { id: "run", label: "Run", section: "Commands" },
            { id: "blocked", label: "Blocked", section: "Commands", enabled: false },
            { id: "later", label: "Later", section: "Commands" }
        ]
        compare(activated.count, 0) // owner model replacement is silent
        palette.open()
        tryVerify(function() { return palette.opened })
        const list = findChild(palette.contentItem, "paletteList")
        verify(list !== null)
        compare(list.Accessible.role, Accessible.List)
        compare(list.Accessible.name, "Commands")
        const run = list.itemAtIndex(1)
        const blocked = list.itemAtIndex(2)
        const later = list.itemAtIndex(3)
        verify(blocked !== null && run !== null)
        verify(later !== null)
        compare(blocked.Accessible.role, Accessible.ListItem)
        compare(blocked.Accessible.name, "Blocked")
        verify(!blocked.Accessible.selectable)
        blocked.Accessible.pressAction()
        compare(activated.count, 0)
        verify(palette.opened)

        keyClick(Qt.Key_Down)
        compare(palette.currentRow, 3) // the disabled row is skipped
        keyClick(Qt.Key_Up)
        compare(palette.currentRow, 1)

        compare(run.Accessible.name, "Run")
        verify(run.Accessible.selectable)
        verify(run.Accessible.selected)
        run.Accessible.pressAction()
        compare(activated.count, 1)
        compare(activated.signalArguments[0][0], "run")
        tryVerify(function() { return !palette.visible })

        palette.commands = [{ id: "only", label: "Only", enabled: false }]
        palette.open()
        tryVerify(function() { return palette.opened })
        compare(palette.currentRow, -1)
        keyClick(Qt.Key_Return)
        compare(activated.count, 1)
        palette.close()
    }

    function test_dialog_disabled_primary_blocks_keyboard_and_accessible_accept() {
        dialog.primaryEnabled = false
        const accepted = createTemporaryObject(signalSpyComponent, root,
                                                { target: dialog, signalName: "accepted" })
        dialog.open()
        tryVerify(function() { return dialog.opened })
        const primary = findChild(dialog.footer, "dialogPrimary")
        verify(primary !== null)
        compare(dialog.contentItem.Accessible.role, Accessible.Dialog)
        compare(dialog.contentItem.Accessible.name, "Confirm")
        dialog.title = ""
        compare(dialog.contentItem.Accessible.name, "Dialog")
        dialog.title = "Confirm"
        verify(!primary.enabled)
        primary.Accessible.pressAction()
        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Enter)
        compare(accepted.count, 0)
        verify(dialog.opened)
        keyClick(Qt.Key_Escape)
        tryVerify(function() { return !dialog.visible })
        dialog.primaryEnabled = true
    }
}
