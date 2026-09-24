// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "CompareViewGestures"
    when: windowShown
    visible: true
    width: 500
    height: 340

    Component {
        id: comparison
        Tk.CompareView { width: 480; height: 320 }
    }

    function test_split_drag_blink_and_pan() {
        const view = createTemporaryObject(comparison, root)
        verify(view !== null)
        waitForRendering(view)
        const viewport = findChild(view, "compareViewport")
        verify(viewport !== null)
        view.mode = "split"
        mousePress(viewport, 30, 30)
        mouseMove(viewport, viewport.width * 0.75, 30)
        mouseRelease(viewport, viewport.width * 0.75, 30)
        verify(view.split > 0.7 && view.split < 0.8)

        view.mode = "blink"
        view.showAfter = false
        mouseClick(viewport, 30, 30)
        compare(view.showAfter, true)

        view.mode = "difference"
        mousePress(viewport, 70, 70)
        mouseMove(viewport, 100, 90)
        mouseRelease(viewport, 100, 90)
        compare(view.panX, 30)
        compare(view.panY, 20)
    }
}
