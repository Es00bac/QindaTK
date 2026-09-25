// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Buttons, chips, bars, status and toggles: each control instantiates at its
// documented height (docs/controls.md size table) and answers one interaction.
// Text, number, slider, combo and colour fields: tst_controls_input_fields.qml.
TestCase {
    id: root
    name: "ControlsInput"
    when: windowShown
    visible: true
    width: 480
    height: 360

    SignalSpy { id: spy }

    Component { id: button; Tk.Button { text: "Save" } }
    Component { id: buttonSmall; Tk.Button { text: "Save"; small: true } }
    Component { id: buttonLarge; Tk.Button { text: "Save"; large: true } }
    Component { id: buttonBusy; Tk.Button { text: "Save"; busy: true } }
    Component { id: chip; Tk.Chip { text: "Layers"; checkable: true } }
    Component { id: chipSmall; Tk.Chip { text: "Layers"; small: true } }
    Component { id: chipRounded; Tk.Chip { text: "Export"; rounded: true } }
    Component { id: segmented; Tk.Segmented { model: ["A", "B", "C"]; width: 200 } }
    Component { id: toolbar; Tk.ToolBar { width: 300; Tk.Button { text: "X" } Tk.ToolSeparator {} } }
    Component { id: toolbarCompact; Tk.ToolBar { compact: true; width: 300 } }
    Component { id: badge; Tk.Badge { text: "12" } }
    Component { id: badgeDot; Tk.Badge { dot: true } }
    Component { id: progress; Tk.ProgressBar { value: 0.5; width: 100 } }
    Component { id: progressThin; Tk.ProgressBar { thin: true; width: 100 } }
    Component { id: spinner; Tk.Spinner {} }
    Component { id: checkbox; Tk.CheckBox { text: "Grid" } }
    Component { id: checkboxTri; Tk.CheckBox { text: "Grid"; tristate: true } }
    Component { id: switchC; Tk.Switch { text: "Rulers" } }
    Component { id: radio; Tk.RadioButton { text: "Solid" } }

    function make(component) {
        const item = createTemporaryObject(component, root)
        verify(item !== null)
        return item
    }

    function test_heights_data() {
        return [
            { tag: "Button", c: button, h: Tk.Theme.size.control },
            { tag: "Button small", c: buttonSmall, h: Tk.Theme.size.controlSm },
            { tag: "Button large", c: buttonLarge, h: Tk.Theme.size.controlLg },
            { tag: "Chip", c: chip, h: Tk.Theme.size.chip },
            { tag: "Chip small", c: chipSmall, h: Tk.Theme.size.controlLg },
            { tag: "Chip rounded", c: chipRounded, h: Tk.Theme.size.action },
            { tag: "Segmented", c: segmented, h: Tk.Theme.size.control },
            { tag: "ToolBar", c: toolbar, h: Tk.Theme.size.toolbar },
            { tag: "ToolBar compact", c: toolbarCompact, h: Tk.Theme.size.controlLg },
            { tag: "Badge", c: badge, h: Tk.Theme.size.controlSm - Tk.Theme.space.sm },
            { tag: "Badge dot", c: badgeDot, h: Tk.Theme.space.md },
            { tag: "ProgressBar", c: progress, h: Tk.Theme.space.xs + 1 },
            { tag: "ProgressBar thin", c: progressThin, h: Tk.Theme.space.xs },
            { tag: "Spinner", c: spinner, h: Tk.Theme.size.icon },
            { tag: "CheckBox", c: checkbox, h: Tk.Theme.size.controlSm },
            { tag: "Switch", c: switchC, h: Tk.Theme.size.controlSm },
            { tag: "RadioButton", c: radio, h: Tk.Theme.size.controlSm },
        ]
    }
    function test_heights(data) {
        const item = make(data.c)
        compare(item.implicitHeight, data.h)
    }

    function test_button_clicks_and_busy() {
        const item = make(button)
        spy.target = item
        spy.signalName = "clicked"
        spy.clear()
        mouseClick(item)
        compare(spy.count, 1)
        const busy = make(buttonBusy)
        verify(!busy.enabled)
        busy.busy = false
        verify(busy.enabled)
        busy.available = false
        verify(!busy.enabled)
    }

    function test_chip_toggles_active() {
        const item = make(chip)
        verify(!item.active)
        mouseClick(item)
        verify(item.active)
        verify(item.checked)
    }

    function test_segmented_activates() {
        const item = make(segmented)
        spy.target = item
        spy.signalName = "activated"
        spy.clear()
        const second = findChild(item, "segment_1")
        verify(second !== null)
        waitForRendering(item)
        mouseClick(second)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], 1)
        compare(item.currentIndex, 1)
        compare(item.currentValue, "B")
    }

    function test_checkbox_and_tristate() {
        const item = make(checkbox)
        mouseClick(item)
        verify(item.checked)
        const tri = make(checkboxTri)
        tri.checkState = Qt.PartiallyChecked
        compare(tri.checkState, Qt.PartiallyChecked)
    }

    function test_switch_toggles() {
        const item = make(switchC)
        spy.target = item
        spy.signalName = "toggled"
        spy.clear()
        mouseClick(item)
        verify(item.checked)
        compare(spy.count, 1)
    }

    function test_radio_checks() {
        const item = make(radio)
        mouseClick(item)
        verify(item.checked)
    }

    function test_template_toggle_keyboard_and_user_notifications() {
        for (const component of [checkbox, switchC, radio]) {
            const item = make(component)
            spy.target = item
            spy.signalName = "clicked"
            spy.clear()
            item.checked = true
            compare(spy.count, 0)
            verify(item.Accessible.checked)
            item.checked = false
            compare(spy.count, 0)
            item.forceActiveFocus()
            keyClick(Qt.Key_Space)
            verify(item.checked)
            compare(spy.count, 1)
            item.checked = false
            compare(spy.count, 1)
            item.Accessible.pressAction()
            verify(item.checked)
            compare(spy.count, 2)
            item.enabled = false
            keyClick(Qt.Key_Space)
            item.Accessible.pressAction()
            verify(item.checked)
            compare(spy.count, 2)
        }
    }

    function test_template_toggled_signal_is_user_only() {
        for (const component of [checkbox, switchC, radio]) {
            const item = make(component)
            spy.target = item
            spy.signalName = "toggled"
            spy.clear()
            item.checked = true
            compare(spy.count, 0)
            item.checked = false
            compare(spy.count, 0)
            item.forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(spy.count, 1)
        }
    }

    function test_tristate_checkbox_keyboard_and_accessible_state() {
        const item = make(checkboxTri)
        spy.target = item
        spy.signalName = "clicked"
        spy.clear()
        item.checkState = Qt.PartiallyChecked
        compare(spy.count, 0)
        verify(item.Accessible.description.indexOf("Partially checked") >= 0)
        item.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(spy.count, 1)
        verify(item.checkState !== Qt.PartiallyChecked)
    }

    function test_progress_and_badge() {
        const bar = make(progress)
        compare(bar.value, 0.5)
        const b = make(badge)
        verify(b.implicitWidth >= b.implicitHeight)
    }
}
