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

    Tk.CommandPalette {
        id: palette
        commands: [
            { id: "one", label: "First command", section: "Commands" },
            { id: "two", label: "Second command", section: "Commands" }
        ]
    }

    Tk.Menu {
        id: menu
        Tk.MenuItem { text: "Parent" }
        Tk.MenuItem { text: "Other" }
    }

    function focusOrigin() {
        origin.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return root.Window.window.activeFocusItem === origin })
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
}
