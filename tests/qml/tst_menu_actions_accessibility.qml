// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "MenuActionsAccessibility"
    when: windowShown
    visible: true
    width: 480
    height: 320

    Component { id: spyComponent; SignalSpy {} }
    Tk.Button { id: opener; text: "Open"; x: Tk.Theme.space.md; y: Tk.Theme.space.md }

    Tk.Menu {
        id: menu
        Tk.MenuItem { id: first; objectName: "menuFirst"; text: "&Check"; checkable: true }
        Tk.MenuItem { id: disabled; objectName: "menuDisabled"; text: "Disabled"; enabled: false }
        Tk.MenuItem { id: last; objectName: "menuLast"; text: "E&&xit" }
    }

    Tk.MenuBar {
        id: bar
        objectName: "actionBar"
        x: 0
        y: 60
        width: 240
        Tk.Menu {
            id: fileMenu
            title: "&File"
            Tk.MenuItem { id: newItem; text: "&New" }
        }
    }

    function spy(target) {
        const result = createTemporaryObject(spyComponent, root,
                                             { target: target, signalName: "triggered" })
        verify(result !== null)
        return result
    }

    function test_menu_item_keyboard_accessible_checked_and_disabled_paths() {
        if (menu.opened) {
            menu.close()
            tryVerify(function() { return !menu.opened })
        }
        const triggered = spy(first)
        const lastTriggered = spy(last)
        menu.popup(20, 20)
        tryVerify(function() { return menu.opened })
        compare(first.text, "&Check")
        compare(first.Accessible.name, "Check")
        compare(first.Accessible.role, Accessible.MenuItem)
        verify(first.Accessible.focusable)
        compare(last.Accessible.name, "E&xit")
        first.checked = true
        compare(triggered.count, 0)
        verify(first.Accessible.checked)

        menu.currentIndex = 0
        first.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(triggered.count, 1)
        verify(!first.checked)
        first.Accessible.pressAction()
        compare(triggered.count, 2)
        verify(first.checked)

        verify(!disabled.enabled)
        verify(!disabled.Accessible.focusable)
        disabled.Accessible.pressAction()
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Return)
        compare(triggered.count, 2)
        compare(lastTriggered.count, 1)
        menu.close()
        tryVerify(function() { return !menu.opened })
    }

    function test_menu_bar_mnemonic_opens_menu_and_item_mnemonic_activates() {
        const triggered = spy(newItem)
        compare(bar.Accessible.role, Accessible.MenuBar)
        compare(bar.Accessible.name, "Menu bar")
        const barEntry = bar.itemAt(0)
        verify(barEntry !== null)
        compare(barEntry.Accessible.name, "File")
        compare(barEntry.Accessible.role, Accessible.MenuItem)
        opener.forceActiveFocus()
        keyClick(Qt.Key_F, Qt.AltModifier)
        tryVerify(function() { return fileMenu.opened })
        keyClick(Qt.Key_N, Qt.AltModifier)
        tryCompare(triggered, "count", 1)
        verify(!fileMenu.opened)
    }
}
