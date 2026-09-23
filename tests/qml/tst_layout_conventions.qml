// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "LayoutConventions"
    when: windowShown
    visible: true
    width: 480
    height: 240

    Component { id: keyCapComponent; Tk.KeyCap { sequence: "Ctrl+K" } }
    Component {
        id: filmstripComponent
        Tk.Filmstrip {
            width: 400
            height: 40
            frames: [{caption: "in"}, {caption: "b"}, {caption: "c"}, {caption: "out"}]
        }
    }

    function test_keyCapUsesToolkitFlexAndRetainsSize() {
        const cap = createTemporaryObject(keyCapComponent, root)
        verify(cap)
        const row = findChild(cap, "keyCapRow")
        verify(row)
        waitForRendering(cap)
        compare(cap.implicitWidth, row.implicitWidth)
        compare(cap.implicitHeight, row.implicitHeight)
        const caps = row.children.filter(item => item.objectName === "keyCapKey")
        compare(caps.length, 2)
        compare(caps[0].x, 0)
        fuzzyCompare(caps[1].x - (caps[0].x + caps[0].width), Tk.Theme.space.xs, 0.01)
    }

    function test_filmstripUsesToolkitFlexAndRetainsFrameGeometry() {
        const strip = createTemporaryObject(filmstripComponent, root)
        verify(strip)
        const row = findChild(strip, "filmstripRow")
        verify(row)
        waitForRendering(strip)
        compare(strip.shown, 4)
        const cells = row.children.filter(item => item.objectName.startsWith("filmstripFrame"))
        compare(cells.length, 4)
        compare(cells[0].x, 0)
        fuzzyCompare(cells[0].width, (strip.width - Tk.Theme.space.xs * 3) / 4, 0.01)
        fuzzyCompare(cells[1].x - (cells[0].x + cells[0].width), Tk.Theme.space.xs, 0.01)
        compare(cells[0].height, strip.height)
        compare(cells[3].caption, "out")
    }
}
