// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "BoxCardAccessibility"
    when: windowShown
    visible: true
    width: 420
    height: 220

    Component {
        id: interactiveBoxComponent
        Tk.Box {
            width: 160; height: 56
            padding: Tk.Theme.space.sm
            interactive: true
            tooltip: "Timeline lane"
            color: Tk.Theme.color.panelAlt
            Tk.Label { text: "Lane 1" }
        }
    }
    Component {
        id: interactiveCardComponent
        Tk.Card {
            width: 180; height: 64
            interactive: true
            tooltip: "Selected clip"
            Tk.Label { text: "Clip" }
        }
    }
    Component { id: passiveComponent; Tk.Box { width: 120; height: 40; Tk.Label { text: "Layout" } } }
    Component { id: passiveCardComponent; Tk.Card { width: 140; height: 48; Tk.Label { text: "Passive card" } } }
    Component { id: spyComponent; SignalSpy {} }

    function exerciseInteractive(component, name) {
        const item = createTemporaryObject(component, root)
        verify(item !== null)
        compare(item.contentItem.children.length, 1)
        const spy = createTemporaryObject(spyComponent, root,
                                          { target: item, signalName: "clicked" })
        compare(item.Accessible.role, Accessible.Button)
        compare(item.Accessible.name, name)
        verify(item.activeFocusOnTab)
        verify(item.Accessible.focusable)
        verify(!item.Accessible.ignored)

        mouseClick(item)
        compare(spy.count, 1)
        item.forceActiveFocus()
        verify(item.activeFocus)
        verify(item.Accessible.focused)
        const ring = findChild(item, "boxFocusRing")
        verify(ring !== null && ring.visible)

        keyClick(Qt.Key_Space)
        compare(spy.count, 2)
        keyClick(Qt.Key_Return)
        compare(spy.count, 3)
        keyClick(Qt.Key_Enter)
        compare(spy.count, 4)
        item.Accessible.pressAction()
        compare(spy.count, 5)

        item.enabled = false
        verify(!item.activeFocusOnTab)
        verify(!item.Accessible.focusable)
        verify(!ring.visible)
        item.Accessible.pressAction()
        keyClick(Qt.Key_Space)
        mouseClick(item)
        compare(spy.count, 5)
    }

    function test_interactive_box_uses_one_guarded_activation_path() {
        exerciseInteractive(interactiveBoxComponent, "Timeline lane")
    }

    function test_interactive_card_inherits_activation_and_name() {
        exerciseInteractive(interactiveCardComponent, "Selected clip")
    }

    function test_passive_box_has_no_interactive_accessibility_or_focus() {
        const item = createTemporaryObject(passiveComponent, root)
        const card = createTemporaryObject(passiveCardComponent, root)
        verify(item !== null && card !== null)
        for (const passive of [item, card]) {
            const spy = createTemporaryObject(spyComponent, root,
                                              { target: passive, signalName: "clicked" })
            compare(passive.Accessible.role, Accessible.NoRole)
            verify(passive.Accessible.ignored)
            verify(!passive.Accessible.focusable)
            verify(!passive.activeFocusOnTab)
            const ring = findChild(passive, "boxFocusRing")
            verify(ring === null || !ring.visible)
            passive.Accessible.pressAction()
            compare(spy.count, 0)
        }
    }
}
