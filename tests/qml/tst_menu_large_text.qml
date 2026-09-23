// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "MenuLargeText"
    visible: true
    when: windowShown
    width: 480
    height: 320

    Tk.Button { id: opener; text: "Menu"; x: 16; y: 16 }
    Component {
        id: menuFixture
        Tk.Menu {
            parent: opener
            Tk.MenuItem { text: "Open recent project" }
            Tk.MenuItem { text: "Export a high-resolution composited document" }
            Tk.MenuItem { text: "Preferences" }
            Tk.MenuItem { text: "Close" }
            Tk.MenuItem { text: "Exit" }
        }
    }

    function test_touch_large_text_menu_data() {
        return [{tag: "dark", preset: "high-contrast-dark"},
                {tag: "light", preset: "high-contrast-light"}]
    }

    function test_touch_large_text_menu(data) {
        const previousDensity = Tk.Density.modeName
        const previousScale = Tk.Density.textScale
        const previousPreset = Tk.Theme.preset
        const menu = createTemporaryObject(menuFixture, root)
        verify(menu !== null)
        try {
            Tk.Density.modeName = "touch"
            Tk.Density.textScale = 3
            Tk.Theme.preset = data.preset
            opener.forceActiveFocus(Qt.TabFocusReason)
            menu.popup()
            tryCompare(menu, "opened", true)
            const first = menu.itemAt(0)
            const label = findChild(first, "menuItemText")
            verify(label !== null)
            verify(menu.width > menu.minWidth)
            const position = opener.mapToItem(root, menu.x, menu.y)
            verify(position.x >= 0 && position.x + menu.width <= root.Window.window.width)
            verify(position.y >= 0 && position.y + menu.height <= root.Window.window.height)
            compare(first.height, Tk.Theme.size.menuItem)
            verify(!label.truncated)
            compare(menu.itemAt(1).Accessible.name,
                    "Export a high-resolution composited document")
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false)
        } finally {
            menu.close()
            Tk.Theme.preset = previousPreset
            Tk.Density.textScale = previousScale
            Tk.Density.modeName = previousDensity
        }
    }
}
