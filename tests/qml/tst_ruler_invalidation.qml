// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "RulerInvalidation"
    when: windowShown
    visible: true
    width: 480
    height: 100

    SignalSpy { id: paintedSpy }

    Component {
        id: rulerComponent
        Tk.Ruler {
            objectName: "invalidationRuler"
            x: 20
            y: 20
            width: 400
            height: 24
            origin: 0
            pixelsPerUnit: 10
            majorEvery: 2
            minorEvery: 1
        }
    }

    function findCanvas(item) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (child instanceof Canvas) {
                return child
            }
            const nested = findCanvas(child)
            if (nested !== null) {
                return nested
            }
        }
        return null
    }

    function test_pixelsPerUnitRepaintsAtConstantGeometry() {
        const ruler = createTemporaryObject(rulerComponent, root)
        verify(ruler !== null)
        waitForRendering(ruler)
        const canvas = findCanvas(ruler)
        verify(canvas !== null)
        paintedSpy.target = canvas
        paintedSpy.signalName = "painted"
        paintedSpy.clear()
        canvas.requestPaint()
        tryVerify(function() { return paintedSpy.count > 0 }, 1000)

        const beforeWidth = ruler.width
        const beforeHeight = ruler.height
        const beforeX = ruler.x
        const beforeY = ruler.y
        const before = canvas.toDataURL("image/png")
        verify(before.length > 100)
        paintedSpy.clear()

        ruler.pixelsPerUnit = 20
        compare(ruler.width, beforeWidth)
        compare(ruler.height, beforeHeight)
        compare(ruler.x, beforeX)
        compare(ruler.y, beforeY)

        // A Canvas with Cooperative rendering may finish after the property
        // binding runs. Compare pixels only after its documented paint fence.
        tryVerify(function() { return paintedSpy.count > 0 }, 1000)
        tryVerify(function() {
            return canvas.toDataURL("image/png") !== before
        }, 1000)
    }
}
