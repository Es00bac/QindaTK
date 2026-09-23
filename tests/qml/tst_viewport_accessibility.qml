// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "ViewportAccessibility"
    when: windowShown
    visible: true
    width: 360
    height: 260

    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: viewportComponent
        Tk.Viewport {
            width: 200
            height: 140
            accessibleName: "Score canvas"
            Item {
                implicitWidth: 500
                implicitHeight: 500
                Tk.Button {
                    objectName: "documentAction"
                    x: 30
                    y: 30
                    text: "Note"
                }
            }
        }
    }

    function make() {
        const viewport = createTemporaryObject(viewportComponent, root)
        verify(viewport !== null)
        waitForRendering(viewport)
        return viewport
    }

    function test_focus_and_keyboard_pan_reach_bounds() {
        const viewport = make()
        const scroll = viewport.scrollItem
        verify(viewport.activeFocusOnTab)
        compare(viewport.Accessible.role, Accessible.Pane)
        compare(viewport.Accessible.name, "Score canvas")
        viewport.forceActiveFocus()
        verify(viewport.Accessible.focused)
        verify(findChild(viewport, "viewportFocusRing").visible)

        keyClick(Qt.Key_Down)
        compare(scroll.contentY, Tk.Theme.size.row)
        keyClick(Qt.Key_Right)
        compare(scroll.contentX, Tk.Theme.size.row)
        keyClick(Qt.Key_End)
        compare(scroll.contentY, scroll.contentHeight - scroll.viewportHeight)
        keyClick(Qt.Key_Home)
        compare(scroll.contentY, 0)
    }

    function test_zoom_actions_are_user_only_and_guarded() {
        const viewport = make()
        const changed = createTemporaryObject(spyComponent, root,
                                              { target: viewport, signalName: "zoomModified" })
        viewport.zoom = 1.5
        compare(changed.count, 0)
        viewport.forceActiveFocus()
        keyClick(Qt.Key_Plus)
        compare(viewport.zoom, 1.5 * viewport.wheelZoomStep)
        compare(changed.count, 1)
        viewport.Accessible.decreaseAction()
        compare(viewport.zoom, 1.5)
        compare(changed.count, 2)

        viewport.interactive = false
        verify(!viewport.Accessible.focusable)
        verify(!findChild(viewport, "viewportFocusRing").visible)
        viewport.Accessible.increaseAction()
        keyClick(Qt.Key_Plus)
        compare(viewport.zoom, 1.5)
        compare(changed.count, 2)

        viewport.zoom = 2
        compare(changed.count, 2)
    }

    function test_document_child_keeps_keyboard_input() {
        const viewport = make()
        const action = findChild(viewport, "documentAction")
        verify(action !== null)
        action.forceActiveFocus()
        verify(action.activeFocus)
        const originalY = viewport.scrollItem.contentY
        keyClick(Qt.Key_Down)
        compare(viewport.scrollItem.contentY, originalY)
        verify(!findChild(viewport, "viewportFocusRing").visible)
    }
}
