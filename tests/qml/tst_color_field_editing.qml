// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "ColorFieldEditing"
    when: windowShown
    visible: true
    width: 480
    height: 240

    SignalSpy { id: spy }
    Component { id: colorField; Tk.ColorField { color: "#112233" } }

    function makeField() {
        const item = createTemporaryObject(colorField, root)
        verify(item !== null)
        return item
    }

    function test_programmatic_hex_and_color_changes_are_silent() {
        const item = makeField()
        spy.target = item
        spy.signalName = "colorEdited"
        spy.clear()
        const hex = findChild(item, "colorHex")
        verify(hex !== null)
        compare(hex.text, "#112233")
        verify(item.applyHex("#abc"))
        compare(item.color.toString(), "#aabbcc")
        compare(spy.count, 0)
        verify(!item.applyHex("nope"))
        compare(hex.text, "#aabbcc")
        verify(!item.inputInvalid)
        item.color = "#445566"
        compare(spy.count, 0)
    }

    function test_invalid_draft_stays_visible_until_escape() {
        const item = makeField()
        const hex = findChild(item, "colorHex")
        verify(hex !== null)
        hex.forceActiveFocus()
        hex.selectAll()
        keyClick(Qt.Key_X)
        keyClick(Qt.Key_Return)
        compare(hex.text, "x")
        verify(item.inputInvalid)
        hex.focus = false
        wait(20)
        compare(hex.text, "x")
        verify(item.inputInvalid)
        hex.forceActiveFocus()
        keyClick(Qt.Key_Escape)
        compare(hex.text, "#112233")
        verify(!item.inputInvalid)
    }

    function test_user_commit_and_readonly_gates() {
        const item = makeField()
        spy.target = item
        spy.signalName = "colorEdited"
        spy.clear()
        const hex = findChild(item, "colorHex")
        const swatch = findChild(item, "colorSwatch")
        verify(hex !== null && swatch !== null)
        hex.forceActiveFocus()
        hex.selectAll()
        keyClick(Qt.Key_A)
        keyClick(Qt.Key_B)
        keyClick(Qt.Key_C)
        keyClick(Qt.Key_Return)
        compare(item.color.toString(), "#aabbcc")
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0].toString(), "#aabbcc")

        item.readOnly = true
        compare(item.Accessible.role, Accessible.Grouping)
        compare(hex.readOnly, true)
        spy.signalName = "pickRequested"
        spy.clear()
        swatch.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(spy.count, 0)
        item.readOnly = false
        swatch.forceActiveFocus()
        verify(swatch.activeFocus)
        keyClick(Qt.Key_Return)
        compare(spy.count, 1)
        item.enabled = false
        compare(hex.readOnly, true)
        spy.clear()
        item.applyHex("#123456")
        compare(item.color.toString(), "#123456")
        compare(spy.count, 0)
    }
}
