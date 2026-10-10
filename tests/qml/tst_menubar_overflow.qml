// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// D-315: a narrow MenuBar moves its trailing menus into a "»" menu (the real
// menus, as submenus) and moves them back, in order, when it widens again;
// host insertions and destroyed menus are re-flowed.
TestCase {
    id: root
    name: "MenuBarOverflow"
    when: windowShown
    visible: true
    width: 640
    height: 360

    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: barComponent
        Tk.MenuBar {
            width: 600
            Tk.Menu { title: "&File"; Tk.MenuItem { text: "&New" } }
            Tk.Menu { title: "&Edit"; Tk.MenuItem { text: "&Undo" } }
            Tk.Menu { title: "&View"; Tk.MenuItem { text: "&Zoom" } }
            Tk.Menu { title: "&Insert"; Tk.MenuItem { text: "&Table" } }
            Tk.Menu { title: "F&ormat"; Tk.MenuItem { text: "&Font" } }
            Tk.Menu { title: "&Tools"; Tk.MenuItem { text: "&Spelling" } }
            Tk.Menu { title: "&Help"; Tk.MenuItem { text: "&About" } }
        }
    }
    Component { id: menuComponent; Tk.Menu { Tk.MenuItem { text: "Item" } } }

    function titles(bar) {
        const out = []
        for (let i = 0; i < bar.count; ++i) out.push(bar.menuAt(i).title)
        return out
    }

    function test_trailing_menus_move_into_overflow_and_back() {
        const bar = createTemporaryObject(barComponent, root)
        verify(bar !== null)
        waitForRendering(bar)
        compare(bar.count, 7)
        compare(bar.overflowCount, 0)
        const natural = bar.implicitWidth
        verify(natural > 0 && natural < 600)

        bar.width = Math.round(natural * 0.6)
        tryVerify(function() { return bar.overflowCount > 0 })
        compare(bar.menuAt(bar.count - 1), bar.overflowMenu)
        compare(bar.count - 1 + bar.overflowCount, 7)
        compare(bar.overflowMenus[bar.overflowCount - 1].title, "&Help")
        compare(bar.implicitWidth, natural)
        compare(bar.overflowMenu.count, bar.overflowCount)
        compare(bar.overflowMenu.itemAt(0).subMenu, bar.overflowMenus[0])
        waitForRendering(bar)
        const more = bar.itemAt(bar.count - 1)
        compare(more.Accessible.name, "More menus")
        verify(more.x + more.width <= bar.width - bar.rightPadding + 0.5)
        const shownTitles = titles(bar)
        compare(shownTitles[0], "&File")

        // The moved menu still works through "»".
        const help = bar.overflowMenus[bar.overflowCount - 1]
        const about = help.itemAt(0)
        const triggered = createTemporaryObject(spyComponent, root, { target: about, signalName: "triggered" })
        mouseClick(more)
        tryVerify(function() { return bar.overflowMenu.opened })
        mouseClick(bar.overflowMenu.itemAt(bar.overflowCount - 1))
        tryVerify(function() { return help.opened })
        mouseClick(about)
        tryCompare(triggered, "count", 1)
        tryVerify(function() { return !bar.overflowMenu.visible && !help.visible })

        bar.width = 600
        tryCompare(bar, "overflowCount", 0)
        compare(titles(bar), ["&File", "&Edit", "&View", "&Insert", "F&ormat", "&Tools", "&Help"])
        bar.width = Math.round(natural * 0.6)
        tryVerify(function() { return bar.overflowCount > 0 })
        bar.overflowEnabled = false
        tryCompare(bar, "overflowCount", 0)
        compare(bar.count, 7)
    }

    function test_host_changes_reflow_while_overflowing() {
        const bar = createTemporaryObject(barComponent, root)
        waitForRendering(bar)
        bar.width = Math.round(bar.implicitWidth * 0.5)
        tryVerify(function() { return bar.overflowCount > 0 })
        const extra = createTemporaryObject(menuComponent, root, { title: "&Window" })
        bar.addMenu(extra)
        tryVerify(function() { return bar.overflowCount > 0 && bar.overflowMenus[bar.overflowCount - 1] === extra })
        compare(bar.count - 1 + bar.overflowCount, 8)
        extra.destroy()
        wait(0)
        bar.width = bar.width - 1
        tryVerify(function() { return bar.count - 1 + bar.overflowCount === 7 })
        compare(bar.overflowMenus[bar.overflowCount - 1].title, "&Help")
        bar.width = 600
        tryCompare(bar, "overflowCount", 0)
        compare(titles(bar), ["&File", "&Edit", "&View", "&Insert", "F&ormat", "&Tools", "&Help"])
    }
}
