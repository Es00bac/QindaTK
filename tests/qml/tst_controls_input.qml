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
    // NumberField never writes `value`/`mixed` (D-198); these fixtures are
    // their own host and echo each user edit back.
    Component { id: number; Tk.NumberField { value: 50; from: 0; to: 100; stepSize: 5; onValueModified: (v) => value = v } }
    Component { id: numberMixed; Tk.NumberField { value: 40; defaultValue: 25; mixed: true; from: 0; to: 100; onValueModified: (v) => { value = v; mixed = false } } }
    Component { id: numberBare; Tk.NumberField { value: 50; from: 0; to: 100 } }
    Component {
        id: numberBound
        Item {
            id: numberHost
            property real model: 50
            property alias field: boundField
            width: 200
            height: 40
            Tk.NumberField {
                id: boundField
                width: 160
                from: 0
                to: 100
                decimals: 1
                value: numberHost.model
                onValueModified: function(next) { numberHost.model = next }
            }
        }
    }
    Component { id: numberReadOnly; Tk.NumberField { value: 50; from: 0; to: 100; editable: false } }
    Component { id: numberSmall; Tk.NumberField { small: true } }
    Component {
        id: numberDecorated
        Tk.NumberField { label: "Width"; prefix: "~"; suffix: " px"; value: 50 }
    }
    Component { id: slider; Tk.Slider { from: 0; to: 10; value: 5; stepSize: 1; width: 100 } }
    Component { id: sliderSmall; Tk.Slider { small: true; width: 100 } }
    Component { id: combo; Tk.ComboBox { model: ["One", "Two", "Three"] } }
    Component { id: comboSmall; Tk.ComboBox { model: ["One"]; small: true } }
    Component { id: comboLong; Tk.ComboBox { model: ["Fit to paper width", "Actual size"]; width: 70 } }
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

    function test_textfield_readonly_blocks_clear_edits() {
        const item = make(textField)
        item.readOnly = true
        spy.target = item
        spy.signalName = "cleared"
        spy.clear()
        const clearButton = findChild(item, "fieldClear")
        verify(clearButton !== null)
        verify(!clearButton.visible)
        compare(item.Accessible.description, "Read only")

        item.forceActiveFocus()
        keyClick(Qt.Key_Escape)
        item.clearText()
        compare(item.text, "abc")
        compare(spy.count, 0)

        item.readOnly = false
        verify(clearButton.visible)
        item.clearText()
        compare(item.text, "")
        compare(spy.count, 1)
    }

    function test_searchfield_readonly_blocks_escape_clear() {
        const item = make(search)
        item.text = "query"
        item.readOnly = true
        spy.target = item
        spy.signalName = "searched"
        spy.clear()
        item.forceActiveFocus()
        keyClick(Qt.Key_Escape)
        compare(item.text, "query")
        compare(spy.count, 0)
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

    function test_numberfield_clamps_for_display_and_steps() {
        const item = make(number)
        const input = findChild(item, "numberInput")
        verify(input !== null)
        // Clamping is display-only: `value` keeps what the host assigned.
        item.value = 500
        compare(item.value, 500)
        compare(item.clampedValue, 100)
        compare(input.text, "100")
        item.value = -3
        compare(item.value, -3)
        compare(item.clampedValue, 0)
        compare(input.text, "0")
        spy.target = item
        spy.signalName = "valueModified"
        spy.clear()
        item.value = 50
        compare(spy.count, 0)
        compare(input.text, "50")
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

    function test_numberfield_keeps_host_binding() {
        const host = make(numberBound)
        const field = host.field
        const input = findChild(field, "numberInput")
        verify(input !== null)
        // A clamp or rounding that changes the shown number leaves the binding.
        host.model = 250
        compare(field.value, 250)
        compare(field.clampedValue, 100)
        compare(input.text, "100.0")
        host.model = 12.34
        compare(field.value, 12.34)
        compare(field.clampedValue, 12.3)
        compare(input.text, "12.3")
        // A user edit reaches the host through the signal and comes back
        // through the binding, which is still live afterwards.
        input.forceActiveFocus()
        input.selectAll()
        keyClick(Qt.Key_4)
        keyClick(Qt.Key_2)
        keyClick(Qt.Key_0)
        keyClick(Qt.Key_Return)
        compare(host.model, 100)
        compare(field.value, 100)
        compare(input.text, "100.0")
        host.model = 7
        compare(field.value, 7)
        compare(input.text, "7.0")
        field.step(1, Qt.NoModifier)
        compare(host.model, 8)
        host.model = 3
        compare(field.value, 3)
        compare(input.text, "3.0")
    }

    function test_numberfield_never_writes_value_or_mixed() {
        const item = make(numberBare)
        const input = findChild(item, "numberInput")
        spy.target = item
        spy.signalName = "valueModified"
        spy.clear()
        input.forceActiveFocus()
        input.selectAll()
        keyClick(Qt.Key_7)
        keyClick(Qt.Key_2)
        keyClick(Qt.Key_Return)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], 72)
        // No host applied the edit, so the field shows `value` again.
        compare(item.value, 50)
        compare(input.text, "50")
        item.step(1, Qt.NoModifier)
        compare(spy.count, 2)
        compare(spy.signalArguments[1][0], 51)
        compare(item.value, 50)
        compare(input.text, "50")
        item.commit(250)
        compare(spy.count, 3)
        compare(spy.signalArguments[2][0], 100)
        compare(item.value, 50)
        item.mixed = true
        item.commit(30)
        compare(spy.count, 4)
        verify(item.mixed)
        compare(item.value, 50)
    }

    function test_combobox_elides_label_and_keeps_full_implicit_width() {
        const item = make(comboLong)
        waitForRendering(item)
        const label = findChild(item, "comboDisplayText")
        verify(label !== null)
        verify(label.visible)
        compare(label.text, "Fit to paper width")
        compare(label.elide, Text.ElideRight)
        compare(label.width, item.contentItem.width)
        verify(label.truncated)
        // The input keeps the full text (glyphs hidden) and measures it.
        compare(item.contentItem.text, "Fit to paper width")
        compare(item.contentItem.color.a, 0)
        const implicitAtNarrow = item.implicitWidth
        verify(implicitAtNarrow > item.width)
        item.width = 400
        waitForRendering(item)
        verify(!label.truncated)
        compare(item.implicitWidth, implicitAtNarrow)
        item.width = 70
        compare(item.implicitWidth, implicitAtNarrow)
        // Editable mode is the plain text input, unchanged.
        item.editable = true
        verify(!label.visible)
        verify(item.contentItem.color.a > 0)
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

    function test_numberfield_reset_and_programmatic_notifications() {
        const item = make(number)
        item.defaultValue = 20
        spy.target = item
        spy.signalName = "valueModified"
        spy.clear()
        item.value = 70
        compare(spy.count, 0)
        item.reset()
        compare(item.value, 20)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], 20)
        item.value = 30
        compare(spy.count, 1)
    }

    function test_numberfield_mixed_and_invalid_drafts() {
        const item = make(numberMixed)
        const input = findChild(item, "numberInput")
        const placeholder = findChild(item, "numberMixedPlaceholder")
        verify(input !== null)
        verify(placeholder !== null)
        spy.target = item
        spy.signalName = "valueModified"
        spy.clear()
        verify(item.mixed)
        compare(input.text, "")
        verify(placeholder.visible)
        input.forceActiveFocus()
        input.focus = false
        compare(item.value, 40)
        verify(item.mixed)

        input.forceActiveFocus()
        keyClick(Qt.Key_7)
        verify(!placeholder.visible)
        keyClick(Qt.Key_Return)
        compare(item.value, 7)
        verify(!item.mixed)
        verify(!item.inputInvalid)
        compare(spy.count, 1)

        item.value = 40
        item.mixed = true
        input.forceActiveFocus()
        keyClick(Qt.Key_4)
        keyClick(Qt.Key_0)
        keyClick(Qt.Key_Return)
        compare(item.value, 40)
        verify(!item.mixed)
        compare(spy.count, 2)

        input.selectAll()
        keyClick(Qt.Key_X)
        keyClick(Qt.Key_Return)
        verify(item.inputInvalid)
        compare(input.text, "x")
        keyClick(Qt.Key_Escape)
        verify(!item.inputInvalid)
        compare(input.text, "40")
    }

    function test_numberfield_readonly_and_disabled_reject_edits() {
        const item = make(numberReadOnly)
        spy.target = item
        spy.signalName = "valueModified"
        spy.clear()
        item.step(1, 0)
        item.forceActiveFocus()
        keyClick(Qt.Key_Up)
        compare(item.value, 50)
        compare(spy.count, 0)
        item.editable = true
        item.enabled = false
        item.step(1, 0)
        compare(item.value, 50)
        compare(spy.count, 0)
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
        compare(item.Accessible.name, "Value")
        spy.target = item
        spy.signalName = "valueModified"
        spy.clear()
        item.value = 7
        compare(spy.count, 0)
        compare(item.Accessible.description, "7")
        item.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(item.value, 8)
        compare(spy.count, 1)
        item.enabled = false
        keyClick(Qt.Key_Right)
        compare(item.value, 8)
        compare(spy.count, 1)
    }

    function test_combobox_selection() {
        const item = make(combo)
        compare(item.currentText, "One")
        spy.target = item
        spy.signalName = "activated"
        spy.clear()
        item.currentIndex = 2
        compare(item.displayText, "Three")
        compare(spy.count, 0)
        item.currentIndex = 0
        const popup = findChild(item, "comboPopup")
        verify(popup !== null)
        const list = popup.contentItem
        item.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryVerify(function() { return item.popup.visible })
        compare(item.highlightedIndex, 0)
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Return)
        tryVerify(function() { return !item.popup.visible })
        compare(item.currentIndex, 1)
        compare(spy.count, 1)
        compare(item.activeFocus, true)
        item.popup.open()
        tryVerify(function() { return item.popup.visible && list.count === 3 })
        compare(list.itemAtIndex(0).Accessible.selected, false)
        compare(list.itemAtIndex(1).Accessible.selected, true)
        mouseClick(list.itemAtIndex(2))
        tryVerify(function() { return !item.popup.visible })
        compare(item.currentIndex, 2)
        compare(spy.count, 2)
        item.popup.open()
        tryVerify(function() { return item.popup.visible })
        keyClick(Qt.Key_Escape)
        tryVerify(function() { return !item.popup.visible })
        compare(item.currentIndex, 2)
        compare(item.activeFocus, true)
        item.popup.close()
    }

    function test_combobox_popup_fits_short_host() {
        const host = root.Window.window
        const oldWidth = host.width
        const oldHeight = host.height
        const oldTextScale = Tk.Density.textScale
        try {
            host.width = 220
            host.height = 140
            tryVerify(function() { return host.contentItem.height === 140 })
            Tk.Density.textScale = 3
            const item = make(combo)
            item.model = ["One", "Two", "Three", "Four", "Five", "Six",
                          "Seven", "Eight", "Nine", "Ten", "Eleven", "Twelve"]
            item.width = 180
            item.y = 100
            item.popup.open()
            tryVerify(function() { return item.popup.visible && item.popup.height > 0 })
            const popupTop = item.popup.contentItem.mapToItem(host.contentItem, 0, 0).y
            const list = item.popup.contentItem
            verify(popupTop >= 0)
            verify(popupTop + item.popup.height <= host.contentItem.height + 0.5)
            verify(list.contentHeight > list.height)
            verify(list.itemAtIndex(0).height >= Tk.Theme.font.small + 2 * Tk.Theme.space.xs)
            verify(list.height > 0)
            item.popup.close()
        } finally {
            host.width = oldWidth
            host.height = oldHeight
            Tk.Density.textScale = oldTextScale
        }
    }

    function test_progress_and_badge() {
        const bar = make(progress)
        compare(bar.value, 0.5)
        const b = make(badge)
        verify(b.implicitWidth >= b.implicitHeight)
    }
}
