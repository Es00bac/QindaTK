// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "TextAreaEditing"
    when: windowShown
    visible: true
    width: 320
    height: 180

    SignalSpy { id: spy }
    Component { id: textArea; Tk.TextArea { rows: 3; width: 240 } }

    function make() {
        const item = createTemporaryObject(textArea, root)
        verify(item !== null)
        return item
    }

    function typeText(value) {
        for (let i = 0; i < value.length; ++i)
            keyClick(value[i] === " " ? Qt.Key_Space : Qt["Key_" + value[i].toUpperCase()])
    }

    function test_live_edit_and_focus_loss_contract() {
        const item = make()
        spy.target = item
        spy.signalName = "editingFinished"
        spy.clear()

        item.text = "owner value"
        compare(item.text, "owner value")
        compare(spy.count, 0)

        item.forceActiveFocus()
        verify(item.activeFocus)
        item.selectAll()
        typeText("user edit")
        compare(item.text, "user edit")
        compare(spy.count, 0)

        root.forceActiveFocus()
        tryCompare(item, "activeFocus", false)
        compare(spy.count, 1)
    }

    function test_owner_assignment_while_focused_is_not_a_user_operation() {
        const item = make()
        spy.target = item
        spy.signalName = "editingFinished"
        spy.clear()

        item.forceActiveFocus()
        verify(item.activeFocus)
        item.text = "owner sync while focused"
        compare(item.text, "owner sync while focused")
        compare(spy.count, 0)

        root.forceActiveFocus()
        tryCompare(item, "activeFocus", false)
        // editingFinished ends a focus session; it does not identify an edit.
        compare(spy.count, 1)
    }

    function test_readonly_and_disabled_gate_user_edits_only() {
        const item = make()
        item.text = "owner value"
        item.readOnly = true
        item.forceActiveFocus()
        verify(item.activeFocus)
        typeText("blocked")
        compare(item.text, "owner value")

        item.readOnly = false
        item.enabled = false
        typeText("blocked")
        compare(item.text, "owner value")

        item.text = "owner update while disabled"
        compare(item.text, "owner update while disabled")
    }

    function test_escape_does_not_revert_live_text() {
        const item = make()
        item.text = "before"
        item.forceActiveFocus()
        item.selectAll()
        typeText("draft")
        compare(item.text, "draft")
        keyClick(Qt.Key_Escape)
        compare(item.text, "draft")
    }
}
