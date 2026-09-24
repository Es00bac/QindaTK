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
        picker.color = "#80204060"
        picker.alphaEnabled = false
        wait(0)
        verify(Qt.colorEqual(picker.previewColor, picker.color))
        verify(!alpha.visible)
        compare(finish.count, 0)
    }
}
