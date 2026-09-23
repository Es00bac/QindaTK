// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "NumberFieldTouchStep"
    visible: true
    when: windowShown
    width: 480
    height: 320

    Component {
        id: fieldFixture
        Tk.NumberField {
            x: 20
            y: 20
            width: 360
            value: 10
            from: 0
            to: 20
            tooltip: "Gain"
        }
    }
    SignalSpy { id: changed; signalName: "valueModified" }

    function test_touch_buttons_and_accessible_steps() {
        const previousDensity = Tk.Density.modeName
        const previousScale = Tk.Density.textScale
        try {
            Tk.Density.modeName = "touch"
            Tk.Density.textScale = 3
            const field = createTemporaryObject(fieldFixture, root)
            verify(field !== null)
            changed.target = field
            changed.clear()
            const up = findChild(field, "numberStepUp")
            const down = findChild(field, "numberStepDown")
            verify(up && down)
            waitForRendering(field)
            verify(up.visible && down.visible)
            verify(up.width >= Tk.Theme.size.controlSm)
            verify(up.height >= Tk.Theme.size.controlSm)
            verify(down.width >= Tk.Theme.size.controlSm)
            verify(down.height >= Tk.Theme.size.controlSm)
            verify(up.x + up.width <= down.x)
            compare(up.Accessible.role, Accessible.Button)
            compare(up.Accessible.name, "Increase value")

            const edge = up.mapToItem(root, up.width - 1, up.height / 2)
            const touch = touchEvent(root)
            touch.press(0, root, edge.x, edge.y).commit()
            touch.release(0, root, edge.x, edge.y).commit()
            tryCompare(field, "value", 11)
            compare(changed.count, 1)
            down.Accessible.pressAction()
            compare(field.value, 10)
            compare(changed.count, 2)
            field.Accessible.increaseAction()
            compare(field.value, 11)
            compare(changed.count, 3)
            field.value = 12
            compare(changed.count, 3)
            field.enabled = false
            up.Accessible.pressAction()
            field.Accessible.decreaseAction()
            compare(field.value, 12)
            compare(changed.count, 3)
        } finally {
            Tk.Density.textScale = previousScale
            Tk.Density.modeName = previousDensity
        }
    }
}
