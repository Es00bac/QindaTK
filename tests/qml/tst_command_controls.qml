// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-020 fixture: shared stable IDs, command context presentation, keyboard
// popup cancellation, tool requests, customization and measured overflow.
TestCase {
    id: root
    name: "CommandControls"
    when: windowShown
    visible: true
    width: 640
    height: 420

    Tk.MenuButton {
        id: menuButton
        objectName: "menuButtonFixture"
        x: Tk.Theme.space.md
        y: Tk.Theme.space.md
        menuModel: [
            { id: "edit.undo", label: "Undo", iconName: "undo", enabled: true },
            { id: "edit.redo", label: "Redo", enabled: false }
        ]
    }
    Tk.SplitButton {
        id: splitButton
        objectName: "splitButtonFixture"
        x: 180
        y: Tk.Theme.space.md
        commandId: "file.save"
        menuModel: [{ id: "file.saveAs", label: "Save As" }]
    }
    Tk.ToolShelf {
        id: toolShelf
        x: Tk.Theme.space.md
        y: 64
        tools: [
            { id: "select", label: "Select", iconName: "cursor", tooltip: "Select tool" },
            { id: "pan", label: "Pan", iconName: "hand", enabled: true },
            { id: "locked", label: "Locked", enabled: false }
        ]
    }
    Tk.CommandBar {
        id: commandBar
        x: Tk.Theme.space.md
        y: 132
        width: 480
        model: [
            { id: "file.open", label: "Open", iconName: "folder-open" },
            { id: "edit.undo", label: "Undo", iconName: "undo" },
            { id: "view.zoom", kind: "property", label: "Zoom", width: 120 }
        ]
        defaultOrder: ["file.open", "edit.undo", "view.zoom"]
    }
    Tk.OverflowToolBar {
        id: overflowBar
        x: Tk.Theme.space.md
        y: 196
        width: 170
        model: [
            { id: "cmd.one", label: "One", priority: 2, groupId: "file" },
            { id: "cmd.two", label: "Two", priority: 2, groupId: "file" },
            { id: "cmd.three", label: "Adjust Preferences", priority: 1 },
            { id: "cmd.four", label: "Synchronize Media", priority: 0 }
        ]
    }
    Component { id: spyComponent; SignalSpy {} }

    function spy(target, signalName) {
        const result = createTemporaryObject(spyComponent, root,
                                             { target: target, signalName: signalName })
        verify(result !== null)
        return result
    }

    function test_menu_button_opens_and_escape_restores_focus() {
        menuButton.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return menuButton.activeFocus })
        menuButton.openMenu()
        tryVerify(function() { return menuButton.menu.visible })
        compare(menuButton.menu.count, 2)
        verify(!menuButton.menu.itemAt(1).enabled)
        keyClick(Qt.Key_Escape)
        tryVerify(function() { return !menuButton.menu.visible })
        tryVerify(function() { return menuButton.activeFocus })
    }

    function test_menu_model_replacement_updates_open_context() {
        menuButton.forceActiveFocus(Qt.TabFocusReason)
        menuButton.openMenu()
        tryVerify(function() { return menuButton.menu.visible })
        menuButton.menuModel = [
            { id: "document.export", label: "Export document", enabled: true },
            { id: "document.close", label: "Close document", enabled: false }
        ]
        tryCompare(menuButton.menu, "count", 2)
        compare(menuButton.menu.itemAt(0).text, "Export document")
        verify(menuButton.menu.itemAt(0).enabled)
        verify(!menuButton.menu.itemAt(1).enabled)
        menuButton.menu.close()
        tryVerify(function() { return menuButton.activeFocus })
        menuButton.menuModel = [
            { id: "edit.undo", label: "Undo", iconName: "undo", enabled: true },
            { id: "edit.redo", label: "Redo", enabled: false }
        ]
    }

    function test_split_arrow_opens_menu_without_primary_command() {
        const triggered = spy(splitButton, "triggered")
        const arrow = findChild(splitButton, "splitMenuButton")
        verify(arrow)
        arrow.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryVerify(function() { return splitButton.menu.visible })
        compare(triggered.count, 0)
        splitButton.menu.close()
        tryVerify(function() { return arrow.activeFocus })
    }

    function test_tool_shelf_requests_selection_and_temporary_override() {
        const requested = spy(toolShelf, "toolRequested")
        const select = findChild(toolShelf, "tool_select")
        const locked = findChild(toolShelf, "tool_locked")
        verify(select && locked)
        select.click()
        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "select")
        verify(!locked.enabled)
        toolShelf.activeToolId = "select"
        toolShelf.beginTemporaryTool("pan")
        compare(toolShelf.effectiveToolId, "pan")
        toolShelf.endTemporaryTool()
        compare(toolShelf.effectiveToolId, "select")
    }

    function test_command_bar_stable_id_customization_and_reset() {
        compare(commandBar.orderedItems[0].id, "file.open")
        commandBar.setOrder(["edit.undo", "file.open", "view.zoom"])
        compare(commandBar.orderedItems[0].id, "edit.undo")
        commandBar.resetOrder()
        compare(commandBar.orderedItems[0].id, "file.open")
        compare(commandBar.orderedItems[2].kind, "property")
    }

    function test_overflow_moves_groups_atomically_and_recovers_on_resize() {
        tryVerify(function() { return overflowBar.overflowIndices.length > 0 })
        const visible = overflowBar.visibleIndices
        const hidden = overflowBar.overflowIndices
        verify(visible.length > 0)
        compare(visible.length + hidden.length, 4)
        const all = visible.concat(hidden).sort(function(a, b) { return a - b })
        compare(all.join(","), "0,1,2,3")
        // Group members move together even though only one command is focused.
        verify((visible.indexOf(0) >= 0) === (visible.indexOf(1) >= 0))
        compare(overflowBar.overflowIndices.map(function(i) { return overflowBar.model[i].id }).join(","), "cmd.three,cmd.four")
        overflowBar.width = 560
        tryVerify(function() { return overflowBar.overflowIndices.length === 0 })
        compare(overflowBar.visibleIndices.length, 4)
        overflowBar.width = 170
        tryVerify(function() { return overflowBar.overflowIndices.length === 2 })
    }
}
