// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Repro for the remaining Qt SplitView hit-test limitation. Run manually with
// build/dev/tests/tst_qml -input tests/repros. The expected failing assertion
// shows a 48px target request does not activate SplitView outside its handle.
TestCase {
    id: root
    name: "SplitterTouchHitRepro"
    when: windowShown
    visible: true
    width: 320
    height: 140

    Tk.Splitter {
        id: split
        width: 240
        height: 100
        Item { objectName: "firstPane"; implicitWidth: 120 }
        Item { implicitWidth: 120 }

        handle: Item {
            id: handleItem
            objectName: "splitterHandle"
            implicitWidth: split.orientation === Qt.Horizontal ? split.handleSize : split.width
            implicitHeight: split.orientation === Qt.Horizontal ? split.height : split.handleSize
            containmentMask: Item {
                readonly property real target: Tk.Theme.size.seamTarget
                x: split.orientation === Qt.Horizontal ? (handleItem.width - target) / 2 : 0
                y: split.orientation === Qt.Horizontal ? 0 : (handleItem.height - target) / 2
                width: split.orientation === Qt.Horizontal ? target : handleItem.width
                height: split.orientation === Qt.Horizontal ? handleItem.height : target
            }
            Rectangle {
                anchors.centerIn: parent
                width: split.orientation === Qt.Horizontal ? Tk.Theme.size.seam : parent.width
                height: split.orientation === Qt.Horizontal ? parent.height : Tk.Theme.size.seam
                color: Tk.Theme.color.seam
            }
        }
    }

    function test_touch_drag_begins_outside_painted_handle() {
        const formerMode = Tk.Density.modeName
        try {
            Tk.Density.modeName = "touch"
            const handle = findChild(split, "splitterHandle")
            const pane = findChild(split, "firstPane")
            verify(handle && pane)
            const before = pane.width
            const touch = touchEvent(root)
            const start = handle.mapToItem(root, handle.width / 2 + 20, handle.height / 2)
            touch.press(0, root, start.x, start.y)
            touch.commit()
            const move = handle.mapToItem(root, handle.width / 2 + 30, handle.height / 2)
            touch.move(0, root, move.x, move.y)
            touch.commit()
            const further = handle.mapToItem(root, handle.width / 2 + 40, handle.height / 2)
            touch.move(0, root, further.x, further.y)
            touch.commit()
            tryVerify(() => pane.width !== before)
            touch.release(0, root, further.x, further.y)
            touch.commit()
        } finally {
            Tk.Density.modeName = formerMode
        }
    }
}
