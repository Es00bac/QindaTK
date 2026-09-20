// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Buttons, chips and inputs: each control instantiates at its documented
// height (docs/controls.md size table) and answers one interaction.
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
    Component { id: textField; Tk.TextField { text: "abc"; clearable: true } }
    Component { id: textFieldSmall; Tk.TextField { small: true } }
    Component { id: search; Tk.SearchField { debounce: 0 } }
    Component { id: textArea; Tk.TextArea { rows: 3; width: 200 } }
    Component { id: number; Tk.NumberField { value: 50; from: 0; to: 100; stepSize: 5 } }
    Component { id: numberSmall; Tk.NumberField { small: true } }
    Component {
        id: numberDecorated
        Tk.NumberField { label: "Width"; prefix: "~"; suffix: " px"; value: 50 }
    }
    Component { id: slider; Tk.Slider { from: 0; to: 10; value: 5; stepSize: 1; width: 100 } }
    Component { id: sliderSmall; Tk.Slider { small: true; width: 100 } }
    Component { id: combo; Tk.ComboBox { model: ["One", "Two", "Three"] } }
    Component { id: comboSmall; Tk.ComboBox { model: ["One"]; small: true } }
    Component { id: colorField; Tk.ColorField { color: "#112233" } }

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
            { tag: "TextField", c: textField, h: Tk.Theme.size.control },
            { tag: "TextField small", c: textFieldSmall, h: Tk.Theme.size.controlSm },
            { tag: "SearchField", c: search, h: Tk.Theme.size.control },
            { tag: "NumberField", c: number, h: Tk.Theme.size.control },
            { tag: "NumberField small", c: numberSmall, h: Tk.Theme.size.controlSm },
            { tag: "Slider", c: slider, h: Tk.Theme.size.iconLg },
            { tag: "Slider small", c: sliderSmall, h: Tk.Theme.size.icon },
            { tag: "ComboBox", c: combo, h: Tk.Theme.size.control },
            { tag: "ComboBox small", c: comboSmall, h: Tk.Theme.size.controlSm },
            { tag: "ColorField", c: colorField, h: Tk.Theme.size.control },
        ]
    }
    function test_heights(data) {
        const item = make(data.c)
        compare(item.implicitHeight, data.h)
    }

    function test_textarea_minimum_rows() {
        const item = make(textArea)
        verify(item.implicitHeight >= item.minimumHeight)
        verify(item.minimumHeight > Tk.Theme.font.body * 3)
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

    function test_textfield_clear_button() {
        const item = make(textField)
        spy.target = item
        spy.signalName = "cleared"
        spy.clear()
        const clearButton = findChild(item, "fieldClear")
        verify(clearButton !== null)
        verify(clearButton.visible)
        mouseClick(clearButton)
        compare(item.text, "")
        compare(spy.count, 1)
        verify(!clearButton.visible)
    }

    function test_search_emits_on_enter() {
        const item = make(search)
        spy.target = item
        spy.signalName = "searched"
        spy.clear()
        item.forceActiveFocus()
        keyClick(Qt.Key_A)
        keyClick(Qt.Key_Return)
        verify(spy.count >= 1)
        compare(spy.signalArguments[spy.count - 1][0], "a")
    }

    function test_numberfield_clamps_and_steps() {
        const item = make(number)
        item.value = 500
        compare(item.value, 100)
        item.value = -3
        compare(item.value, 0)
        spy.target = item
        spy.signalName = "valueModified"
        spy.clear()
        item.value = 50
        compare(spy.count, 0)
        const input = findChild(item, "numberInput")
        verify(input !== null)
        input.forceActiveFocus()
        keyClick(Qt.Key_Up)
        compare(item.value, 55)
        compare(spy.count, 1)
        keyClick(Qt.Key_Down, Qt.ShiftModifier)
        compare(item.value, 5)
        compare(input.text, "5")
        keyClick(Qt.Key_Escape)
        compare(input.text, "5")
    }

    function test_numberfield_step_buttons() {
        const item = make(number)
        waitForRendering(item)
        mouseMove(item, 60, 12)
        wait(30)
        const up = findChild(item, "numberStepUp")
        const down = findChild(item, "numberStepDown")
        verify(up !== null && down !== null)
        mouseClick(up)
        compare(item.value, 55)
        mouseClick(down, 7, 5, Qt.LeftButton, Qt.ShiftModifier)
        compare(item.value, 5)
    }

    function test_numberfield_typed_value() {
        const item = make(number)
        const input = findChild(item, "numberInput")
        input.forceActiveFocus()
        input.selectAll()
        keyClick(Qt.Key_7)
        keyClick(Qt.Key_2)
        keyClick(Qt.Key_Return)
        compare(item.value, 72)
    }

    function test_numberfield_inline_text_is_visible() {
        const item = make(numberDecorated)
        item.width = 240
        waitForRendering(item)
        const label = findChild(item, "numberLabel")
        const input = findChild(item, "numberInput")
        verify(label !== null && input !== null)
        compare(label.text, "Width")
        verify(label.width > 0)
        verify(input.x > label.x + label.width)
        verify(input.width < item.width)
    }

    function test_slider_keyboard() {
        const item = make(slider)
        spy.target = item
        spy.signalName = "valueModified"
        spy.clear()
        item.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(item.value, 6)
        compare(spy.count, 1)
    }

    function test_combobox_selection() {
        const item = make(combo)
        compare(item.currentText, "One")
        item.currentIndex = 2
        compare(item.displayText, "Three")
        const popup = findChild(item, "comboPopup")
        verify(popup !== null)
        item.popup.open()
        tryVerify(function() { return item.popup.visible })
        item.popup.close()
    }

    function test_colorfield_hex_edit() {
        const item = make(colorField)
        spy.target = item
        spy.signalName = "colorEdited"
        spy.clear()
        const hex = findChild(item, "colorHex")
        verify(hex !== null)
        compare(hex.text, "#112233")
        verify(item.applyHex("#abc"))
        compare(item.color.toString(), "#aabbcc")
        compare(spy.count, 1)
        verify(!item.applyHex("nope"))
        compare(hex.text, "#aabbcc")
    }

    function test_progress_and_badge() {
        const bar = make(progress)
        compare(bar.value, 0.5)
        const b = make(badge)
        verify(b.implicitWidth >= b.implicitHeight)
    }
}
