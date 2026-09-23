// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "CommandPresentations"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Component {
        id: paletteComponent
        Tk.CommandPalette {
            commands: [
                { id: "file.new", label: "New project", section: "File" },
                { id: "edit.undo", label: "Undo", shortcut: "Ctrl+Z", section: "Edit" }
            ]
        }
    }
    Component { id: menuItemComponent; Tk.MenuItem { text: "Open"; shortcut: "Ctrl+O" } }
    Component { id: commandButtonComponent; Tk.CommandButton { commandId: "missing" } }

    function test_direct_palette_commands_remain_usable() {
        const palette = createTemporaryObject(paletteComponent, root)
        verify(palette)
        compare(palette.resultCount, 2)
        compare(palette.rows[1].label, "New project")
        compare(palette.rows[3].shortcut, "Ctrl+Z")
        let activated = ""
        palette.activated.connect(function(id) { activated = id })
        palette.filterText = "undo"
        compare(palette.resultCount, 1)
        compare(palette.currentRow, 1)
        palette.activateCurrent()
        compare(activated, "edit.undo")
    }

    function test_unbound_controls_keep_direct_values() {
        const menuItem = createTemporaryObject(menuItemComponent, root)
        const button = createTemporaryObject(commandButtonComponent, root)
        verify(menuItem && button)
        compare(menuItem.text, "Open")
        compare(menuItem.shortcut, "Ctrl+O")
        compare(button.enabled, false)
        compare(button.disabledReason, "No command registry")
    }
}
