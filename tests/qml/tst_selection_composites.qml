// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "SelectionComposites"
    when: windowShown
    visible: true
    width: 520
    height: 220

    Component {
        id: segmentedComponent
        Tk.Segmented {
            model: ["One", { text: "Two", enabled: false, tooltip: "Unavailable" }, "Three"]
            width: 280
        }
    }
    Component {
        id: pillComponent
        Tk.PillSwitcher {
            model: [{ text: "One", value: 1 },
                    { text: "Two", value: 2, enabled: false },
                    { text: "Three", value: 3 }]
        }
    }
    Component {
        id: tabsComponent
        Tk.TabStrip {
            model: ["One", { text: "Two", enabled: false, closable: true }, "Three"]
            width: 300
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function spy(target, signalName) {
        const result = createTemporaryObject(spyComponent, root, { target: target, signalName: signalName })
        verify(result !== null)
        return result
    }

    function test_segmentedSkipsDisabledEntriesAndReportsAccessibleSelection() {
        const control = createTemporaryObject(segmentedComponent, root)
        verify(control !== null)
        waitForRendering(control)
        const activated = spy(control, "activated")
        const first = findChild(control, "segment_0")
        const disabled = findChild(control, "segment_1")
        const third = findChild(control, "segment_2")
        verify(first !== null && disabled !== null && third !== null)

        compare(control.Accessible.role, Accessible.PageTabList)
        compare(control.Accessible.name, "Selection")
        compare(first.Accessible.role, Accessible.PageTab)
        compare(first.Accessible.selected, true)
        compare(disabled.Accessible.name, "Two")
        verify(!disabled.enabled)
        verify(!disabled.Accessible.focusable)

        first.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(control.currentIndex, 2)
        compare(activated.count, 1)
        verify(third.activeFocus)
        verify(third.Accessible.focused)
        verify(findChild(third, "segmentFocus_2").visible)
        compare(third.Accessible.selected, true)

        disabled.Accessible.pressAction()
        compare(activated.count, 1)
        mouseClick(disabled)
        compare(activated.count, 1)
        compare(control.currentIndex, 2)
        third.Accessible.pressAction()
        compare(activated.count, 2)

        control.enabled_ = false
        verify(!control.enabled)
        verify(!third.enabled)
        control.currentIndex = 1
        compare(control.currentIndex, 1)
        compare(activated.count, 2)
    }

    function test_pillSwitcherSkipsDisabledEntriesAndSeparatesOwnerUpdates() {
        const control = createTemporaryObject(pillComponent, root, { y: 50 })
        verify(control !== null)
        waitForRendering(control)
        const activated = spy(control, "activated")
        const first = control.itemAt(0)
        const disabled = control.itemAt(1)
        const third = control.itemAt(2)
        verify(first !== null && disabled !== null && third !== null)
        compare(control.Accessible.role, Accessible.PageTabList)
        compare(control.Accessible.name, "Selection")
        verify(first.Accessible.selected)
        verify(!disabled.enabled)
        verify(!disabled.Accessible.focusable)

        first.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(control.currentIndex, 2)
        compare(activated.count, 1)
        verify(third.activeFocus)
        verify(third.Accessible.focused)
        verify(findChild(third, "pillSwitcherFocus_2").visible)
        keyClick(Qt.Key_Home)
        compare(control.currentIndex, 0)
        compare(activated.count, 2)

        disabled.Accessible.pressAction()
        control.activate(1)
        compare(control.currentIndex, 0)
        compare(activated.count, 2)

        control.currentIndex = 2
        compare(activated.count, 2)
        third.Accessible.pressAction()
        compare(control.currentIndex, 2)
        compare(activated.count, 2)
        first.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(control.currentIndex, 2)
        compare(activated.count, 2)
        control.currentIndex = 0
        third.Accessible.pressAction()
        compare(control.currentIndex, 2)
        compare(activated.count, 3)
        control.currentIndex = 1
        compare(control.currentIndex, 1)
        compare(activated.count, 3)
        control.enabled = false
        control.activate(0)
        compare(control.currentIndex, 1)
        compare(activated.count, 3)
    }

    function test_tabStripTabsHaveFocusStateAndDisabledClosersAreInert() {
        const control = createTemporaryObject(tabsComponent, root, { y: 110 })
        verify(control !== null)
        waitForRendering(control)
        const activated = spy(control, "tabActivated")
        const closed = spy(control, "tabClosed")
        const first = control.tabItem(0)
        const disabled = control.tabItem(1)
        const third = control.tabItem(2)
        verify(first !== null && disabled !== null && third !== null)

        compare(control.Accessible.role, Accessible.PageTabList)
        compare(control.Accessible.name, "Tabs")
        compare(first.Accessible.role, Accessible.PageTab)
        verify(first.Accessible.selected)
        verify(disabled.Accessible.name === "Two")
        verify(!disabled.enabled)
        verify(!disabled.Accessible.focusable)
        const close = findChild(disabled, "tabClose_1")
        verify(close !== null && close.visible)

        // Owner assignment is silent; keyboard navigation skips the disabled
        // tab and moves focus together with the selected state.
        control.currentIndex = 2
        compare(activated.count, 0)
        third.forceActiveFocus()
        keyClick(Qt.Key_Left)
        compare(control.currentIndex, 0)
        compare(activated.count, 1)
        verify(first.activeFocus)
        verify(first.Accessible.focused)
        verify(findChild(first, "tabFocus_0").visible)
        verify(first.Accessible.selected)

        disabled.Accessible.pressAction()
        mouseClick(disabled)
        compare(control.currentIndex, 0)
        mouseClick(close)
        compare(control.currentIndex, 0)
        compare(activated.count, 1)
        compare(closed.count, 0)
        third.Accessible.pressAction()
        compare(control.currentIndex, 2)
        compare(activated.count, 2)
        control.enabled = false
        third.Accessible.pressAction()
        mouseClick(third)
        compare(control.currentIndex, 2)
        compare(activated.count, 2)
    }
}
