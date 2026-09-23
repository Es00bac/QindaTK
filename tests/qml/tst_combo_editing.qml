// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "ComboEditing"
    when: windowShown
    visible: true
    width: 260
    height: 120

    Component {
        id: editableCombo
        Tk.ComboBox {
            width: 180
            editable: true
            model: ["1", "2"]
            validator: IntValidator { bottom: 0; top: 9 }
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_owner_text_and_user_commit_have_distinct_signals() {
        const combo = createTemporaryObject(editableCombo, root)
        verify(combo !== null)
        const accepted = createTemporaryObject(spyComponent, root,
                                              { target: combo, signalName: "accepted" })
        const activated = createTemporaryObject(spyComponent, root,
                                               { target: combo, signalName: "activated" })
        combo.editText = "5"
        compare(combo.editText, "5")
        compare(accepted.count, 0)
        compare(activated.count, 0)

        combo.contentItem.forceActiveFocus()
        verify(combo.contentItem.activeFocus)
        combo.contentItem.selectAll()
        keyClick(Qt.Key_3)
        compare(combo.editText, "3")
        keyClick(Qt.Key_Return)
        compare(accepted.count, 1)
        compare(activated.count, 0)
        compare(combo.currentIndex, -1)

        combo.editText = "12"
        compare(combo.acceptableInput, false)
        keyClick(Qt.Key_Return)
        compare(accepted.count, 1)

        combo.editText = "4"
        combo.enabled = false
        keyClick(Qt.Key_Return)
        compare(accepted.count, 1)
        compare(combo.editText, "4")
        combo.editText = "6"
        compare(combo.editText, "6")
    }
}
