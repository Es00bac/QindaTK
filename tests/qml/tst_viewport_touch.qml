// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Touch on a Viewport: two fingers pinch-zoom about their centroid and pan;
// `touchPan` lets one finger drag the view; a drawing child whose
// Tk.StylusHandler accepts touch inks with one finger and hands the finger
// over (its stroke canceled) the moment a second finger lands.
TestCase {
    id: root
    name: "ViewportTouch"
    when: windowShown
    visible: true
    width: 400
    height: 300

    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: viewportComponent
        Tk.Viewport {
            width: 400
            height: 300
            contentPadding: 0
            Rectangle {
                objectName: "paper"
                implicitWidth: 800
                implicitHeight: 1200
                Tk.StylusHandler {
                    objectName: "ink"
                    enabled: false
                    acceptedDevices: PointerDevice.Stylus | PointerDevice.Mouse
                                     | PointerDevice.TouchScreen
                    acceptedPointerTypes: PointerDevice.Pen | PointerDevice.Eraser
                                          | PointerDevice.Generic | PointerDevice.Finger
                }
            }
        }
    }

    function make() {
        const viewport = createTemporaryObject(viewportComponent, root)
        verify(viewport !== null)
        waitForRendering(viewport)
        return viewport
    }

    function pinchOut(steps) {
        const touch = touchEvent(root)
        touch.press(0, root, 150, 150).press(1, root, 250, 150)
        touch.commit()
        for (let i = 1; i <= steps; ++i) {
            touch.move(0, root, 150 - i * 10, 150).move(1, root, 250 + i * 10, 150)
            touch.commit()
            wait(10)
        }
        touch.release(0, root, 150 - steps * 10, 150).release(1, root, 250 + steps * 10, 150)
        touch.commit()
    }

    function test_two_fingers_zoom_about_their_centroid() {
        const viewport = make()
        const modified = createTemporaryObject(spyComponent, root,
                                               { target: viewport, signalName: "zoomModified" })
        const before = viewport.toContent(Qt.point(200, 150))
        pinchOut(8)
        verify(viewport.zoom > 1.5, "zoom " + viewport.zoom)
        verify(modified.count >= 1)
        const after = viewport.toContent(Qt.point(200, 150))
        fuzzyCompare(after.x, before.x, 2)
        fuzzyCompare(after.y, before.y, 2)
    }

    function test_one_finger_pans_only_when_asked() {
        const viewport = make()
        const scroll = viewport.scrollItem
        const drag = function() {
            const touch = touchEvent(root)
            touch.press(0, root, 200, 250)
            touch.commit()
            for (let i = 1; i <= 10; ++i) {
                touch.move(0, root, 200, 250 - i * 15)
                touch.commit()
                wait(10)
            }
            touch.release(0, root, 200, 100)
            touch.commit()
        }
        drag()
        compare(scroll.contentY, 0)
        viewport.touchPan = true
        drag()
        verify(scroll.contentY > 100, "contentY " + scroll.contentY)
    }

    function test_a_finger_inks_and_a_second_finger_takes_over() {
        const viewport = make()
        // With one-finger pan on too: the inking finger must stay ink (the
        // pan's passive grab must not take it at the drag threshold).
        viewport.touchPan = true
        const ink = findChild(viewport, "ink")
        verify(ink !== null)
        ink.enabled = true
        const began = createTemporaryObject(spyComponent, root, { target: ink, signalName: "began" })
        const ended = createTemporaryObject(spyComponent, root, { target: ink, signalName: "ended" })
        const canceled = createTemporaryObject(spyComponent, root, { target: ink, signalName: "canceled" })

        // One finger: a whole stroke, reported as touch.
        let touch = touchEvent(root)
        touch.press(0, root, 100, 100)
        touch.commit()
        for (let i = 1; i <= 8; ++i) {
            touch.move(0, root, 100 + i * 10, 100 + i * 5)
            touch.commit()
            wait(10)
        }
        touch.release(0, root, 180, 140)
        touch.commit()
        compare(began.count, 1)
        compare(began.signalArguments[0][0].pointerType, "touch")
        compare(ended.count, 1)
        compare(canceled.count, 0)
        compare(viewport.zoom, 1)
        compare(viewport.scrollItem.contentY, 0)

        // A second finger cancels the stroke and the pinch zooms.
        touch = touchEvent(root)
        touch.press(0, root, 150, 150)
        touch.commit()
        touch.move(0, root, 152, 150)
        touch.commit()
        compare(began.count, 2)
        touch.stationary(0).press(1, root, 250, 150)
        touch.commit()
        compare(canceled.count, 1)
        verify(!ink.active)
        for (let i = 1; i <= 8; ++i) {
            touch.move(0, root, 152 - i * 10, 150).move(1, root, 250 + i * 10, 150)
            touch.commit()
            wait(10)
        }
        touch.release(0, root, 72, 150).release(1, root, 330, 150)
        touch.commit()
        compare(ended.count, 1)
        verify(viewport.zoom > 1.5, "zoom " + viewport.zoom)
    }
}
