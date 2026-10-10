// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// D-326: opt-in drag and drop between TreeRows — a ghost follows the pointer,
// the target shows before/inside/after, and only rows with the same drag key
// accept; the rows report the drop and move nothing.
TestCase {
    id: root
    name: "TreeRowDrag"
    when: windowShown
    visible: true
    width: 360
    height: 220

    Component { id: spyComponent; SignalSpy {} }
    Column {
        y: 20
        Tk.TreeRow { id: first; width: 300; text: "Page A"; itemId: "a"; dragEnabled: true; dropEnabled: true }
        Tk.TreeRow { id: section; width: 300; text: "Section B"; itemId: "b"; expandable: true; dragEnabled: true; dropEnabled: true }
        Tk.TreeRow { id: foreign; width: 300; text: "Page C"; itemId: "c"; dropEnabled: true; dragKey: "other" }
    }

    function test_drop_positions_and_requests() {
        compare(section.dropPositionAt(1), "before")
        compare(section.dropPositionAt(section.height / 2), "inside")
        compare(section.dropPositionAt(section.height - 1), "after")
        compare(first.dropPositionAt(first.height / 2 + 1), "after")
        const dropped = createTemporaryObject(spyComponent, root, { target: section, signalName: "dropRequested" })
        const finished = createTemporaryObject(spyComponent, root, { target: first, signalName: "dragFinished" })
        const clicked = createTemporaryObject(spyComponent, root, { target: first, signalName: "clicked" })
        const x = 60
        mousePress(first, x, first.height / 2)
        mouseMove(first, x, first.height / 2 + 12)
        mouseMove(first, x, first.height + section.height / 2)
        verify(first.dragging)
        verify(findChild(first, "treeDragGhost").visible)
        tryCompare(section, "dropPosition", "inside")
        verify(findChild(section, "treeDropFrame").visible)
        mouseMove(first, x, first.height + section.height - 2)
        tryCompare(section, "dropPosition", "after")
        verify(findChild(section, "treeDropLine").visible)
        mouseRelease(first, x, first.height + section.height - 2)
        compare(dropped.count, 1)
        compare(dropped.signalArguments[0][0], "a")
        compare(dropped.signalArguments[0][1], "after")
        compare(finished.count, 1)
        compare(finished.signalArguments[0][0], true)
        compare(section.dropPosition, "")
        verify(!first.dragging)
        compare(clicked.count, 0)

        // A row with another drag key never offers a place.
        mousePress(first, x, first.height / 2)
        mouseMove(first, x, first.height / 2 + 12)
        mouseMove(first, x, first.height + section.height + foreign.height / 2)
        compare(foreign.dropPosition, "")
        mouseRelease(first, x, first.height + section.height + foreign.height / 2)
        compare(finished.count, 2)
        compare(finished.signalArguments[1][0], false)
        compare(dropped.count, 1)
        // A row is not its own target.
        mousePress(section, x, section.height / 2)
        mouseMove(section, x, section.height / 2 + 8)
        mouseMove(section, x, section.height / 2 + 4)
        compare(section.dropPosition, "")
        mouseRelease(section, x, section.height / 2 + 4)
        compare(dropped.count, 1)
    }
}
