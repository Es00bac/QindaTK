// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "StatusFieldAccessibility"
    when: windowShown
    visible: true
    width: 420
    height: 160

    Component {
        id: actionField
        Tk.StatusField {
            text: "3 assets"
            tooltip: "Show assets"
            iconName: "layers"
            width: 180
        }
    }
    Component {
        id: staticField
        Tk.StatusField { text: "Saved"; actionable: false; width: 180 }
    }
    Component {
        id: bar
        Tk.StatusBar {
            width: 300
            Tk.StatusField { objectName: "touchStatusField"; text: "Ready" }
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_actionable_template_button_uses_one_guarded_path() {
        const field = createTemporaryObject(actionField, root)
        verify(field !== null)
        const spy = createTemporaryObject(spyComponent, root,
                                          { target: field, signalName: "clicked" })
        const button = findChild(field, "statusAction")
        verify(button !== null)
        compare(button.Accessible.role, Accessible.Button)
        compare(button.Accessible.name, "3 assets")
        compare(button.Accessible.description, "Show assets")
        verify(button.activeFocusOnTab)

        field.text = "4 assets" // owner synchronization is silent
        compare(button.Accessible.name, "4 assets")
        compare(spy.count, 0)
        field.text = ""
        compare(button.Accessible.name, "Show assets")
        field.text = "4 assets"
        compare(spy.count, 0)

        button.forceActiveFocus()
        verify(button.activeFocus)
        verify(findChild(field, "statusFocusRing").visible)
        keyClick(Qt.Key_Space)
        compare(spy.count, 1)
        keyClick(Qt.Key_Return)
        compare(spy.count, 2)
        keyClick(Qt.Key_Enter)
        compare(spy.count, 3)
        button.Accessible.pressAction()
        compare(spy.count, 4)
        mouseClick(field)
        compare(spy.count, 5)

        field.enabled = false
        verify(!button.enabled)
        verify(!findChild(field, "statusFocusRing").visible)
        button.Accessible.pressAction()
        keyClick(Qt.Key_Space)
        mouseClick(field)
        compare(spy.count, 5)
    }

    function test_static_mode_has_no_button_or_action() {
        const field = createTemporaryObject(staticField, root, { y: 48 })
        verify(field !== null)
        compare(field.Accessible.role, Accessible.StaticText)
        compare(field.Accessible.name, "Saved")
        verify(!field.activeFocusOnTab)
        compare(findChild(field, "statusAction"), null)
        const spy = createTemporaryObject(spyComponent, root,
                                          { target: field, signalName: "clicked" })
        mouseClick(field)
        compare(spy.count, 0)
    }

    function test_status_band_and_fields_follow_touch_floor() {
        const previous = Tk.Density.modeName
        try {
            Tk.Density.modeName = "compact"
            const compact = createTemporaryObject(bar, root)
            verify(compact !== null)
            compare(compact.implicitHeight, Tk.Theme.size.statusBar)
            compare(compact.implicitHeight, 22)
            compact.destroy()

            Tk.Density.modeName = "touch"
            const touch = createTemporaryObject(bar, root)
            verify(touch !== null)
            compare(touch.implicitHeight, Tk.Theme.size.control + touch.edgeTop)
            const field = findChild(touch, "touchStatusField")
            verify(field !== null)
            compare(field.height, Tk.Theme.size.control)
            touch.destroy()
        } finally {
            Tk.Density.modeName = previous
        }
    }
}
