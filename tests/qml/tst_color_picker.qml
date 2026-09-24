// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "ColorPicker"
    when: windowShown
    visible: true
    width: 420
    height: 400

    Component {
        id: pickerComponent
        Tk.ColorPicker {
            width: 280
            height: 320
            color: "#ff0000"
            tooltip: "Test sRGB colour"
        }
    }

    Component { id: spyComponent; SignalSpy {} }

    function test_pointer_preview_finish_is_host_owned() {
        const picker = createTemporaryObject(pickerComponent, this)
        verify(picker !== null)
        const preview = createTemporaryObject(spyComponent, this,
            { target: picker, signalName: "previewRequested" })
        const finish = createTemporaryObject(spyComponent, this,
            { target: picker, signalName: "editFinished" })
        const cancel = createTemporaryObject(spyComponent, this,
            { target: picker, signalName: "editCanceled" })
        const sv = findChild(picker, "colorPickerSaturationValue")
        verify(sv !== null)

        mousePress(sv, 70, 90)
        verify(preview.count > 0)
        verify(!Qt.colorEqual(picker.previewColor, picker.color))
        verify(Qt.colorEqual(picker.color, "#ff0000"))
        mouseRelease(sv, 70, 90)

        compare(finish.count, 1)
        compare(cancel.count, 0)
        verify(Qt.colorEqual(finish.signalArguments[0][0], preview.signalArguments[preview.count - 1][0]))
        verify(Qt.colorEqual(picker.color, "#ff0000"))
    }

    function test_escape_cancels_pointer_preview_to_original() {
        const picker = createTemporaryObject(pickerComponent, this)
        const sv = findChild(picker, "colorPickerSaturationValue")
        const preview = createTemporaryObject(spyComponent, this,
            { target: picker, signalName: "previewRequested" })
        const cancel = createTemporaryObject(spyComponent, this,
            { target: picker, signalName: "editCanceled" })

        mousePress(sv, 80, 100)
        verify(preview.count > 0)
        sv.forceActiveFocus()
        keyClick(Qt.Key_Escape)

        compare(cancel.count, 1)
        verify(Qt.colorEqual(cancel.signalArguments[0][0], "#ff0000"))
        verify(Qt.colorEqual(picker.previewColor, "#ff0000"))
        mouseRelease(sv, 80, 100)
    }

    function test_hue_and_alpha_keyboard_actions_emit_single_edits() {
        const picker = createTemporaryObject(pickerComponent, this)
        const hue = findChild(picker, "colorPickerHue")
        const alpha = findChild(picker, "colorPickerAlpha")
        const preview = createTemporaryObject(spyComponent, this,
            { target: picker, signalName: "previewRequested" })
        const finish = createTemporaryObject(spyComponent, this,
            { target: picker, signalName: "editFinished" })
        verify(hue !== null && alpha !== null)

        hue.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(finish.count, 1)
        verify(preview.count > 0)
        alpha.forceActiveFocus()
        keyClick(Qt.Key_Left)
        compare(finish.count, 2)
        compare(preview.count, 2)
        verify(finish.signalArguments[1][0].a < 1)
        verify(Qt.colorEqual(picker.color, "#ff0000"))
    }

    function test_owner_update_is_silent_and_alpha_can_be_hidden() {
        const picker = createTemporaryObject(pickerComponent, this)
        const finish = createTemporaryObject(spyComponent, this,
            { target: picker, signalName: "editFinished" })
        const alpha = findChild(picker, "colorPickerAlpha")
        const fullHeight = picker.implicitHeight
        picker.color = "#80204060"
        picker.alphaEnabled = false
        wait(0)
        verify(Qt.colorEqual(picker.previewColor, picker.color))
        verify(!alpha.visible)
        compare(picker.implicitHeight,
                fullHeight - Tk.Theme.size.controlSm - Tk.Theme.space.sm)
        compare(finish.count, 0)
    }

    function test_hue_keyboard_wraps_across_red() {
        const picker = createTemporaryObject(pickerComponent, this)
        const hue = findChild(picker, "colorPickerHue")
        const finish = createTemporaryObject(spyComponent, this,
            { target: picker, signalName: "editFinished" })
        hue.forceActiveFocus()
        keyClick(Qt.Key_Left)
        compare(finish.count, 1)
        fuzzyCompare(finish.signalArguments[0][0].hsvHue, 0.99, 0.001)
    }

    function test_normalized_green_blue_hue_marker_and_channel_requests() {
        const picker = createTemporaryObject(pickerComponent, this, { color: "#00ff00" })
        const sv = findChild(picker, "colorPickerSaturationValue")
        const hue = findChild(picker, "colorPickerHue")
        const hueMarker = findChild(hue, "colorPickerHueMarker")
        const alpha = findChild(picker, "colorPickerAlpha")
        const preview = createTemporaryObject(spyComponent, this,
            { target: picker, signalName: "previewRequested" })
        const finish = createTemporaryObject(spyComponent, this,
            { target: picker, signalName: "editFinished" })
        verify(sv !== null && hueMarker !== null && alpha !== null)

        fuzzyCompare(picker.hue, 1 / 3, 0.001)
        fuzzyCompare(hueMarker.x, hue.width / 3 - hueMarker.width / 2, 0.02)

        picker.begin("sv")
        picker.updateSv(sv.width / 2, sv.height / 2, sv.width, sv.height)
        fuzzyCompare(preview.signalArguments[0][0].hsvHue, 1 / 3, 0.001)
        picker.cancel()

        picker.color = "#0000ff"
        fuzzyCompare(picker.hue, 2 / 3, 0.001)
        fuzzyCompare(hueMarker.x, hue.width * 2 / 3 - hueMarker.width / 2, 0.02)
        picker.begin("alpha")
        picker.updateAlpha(alpha.width / 2, alpha.width)
        fuzzyCompare(preview.signalArguments[1][0].hsvHue, 2 / 3, 0.001)
        fuzzyCompare(preview.signalArguments[1][0].a, 0.5, 0.01)
        picker.cancel()

        hue.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(finish.count, 1)
        fuzzyCompare(preview.signalArguments[2][0].hsvHue, 2 / 3 + 0.01, 0.001)
    }
}
