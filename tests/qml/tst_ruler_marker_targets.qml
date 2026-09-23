// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "RulerMarkerTargets"
    when: windowShown
    visible: true
    width: 400
    height: 120

    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: oneMarkerComponent
        Tk.Ruler {
            width: 300
            markers: [{ id: "left", position: 60, kind: "indent-left", draggable: true }]
        }
    }
    Component {
        id: nearbyMarkersComponent
        Tk.Ruler {
            width: 300
            markers: [
                { id: "left", position: 60, kind: "indent-left", draggable: true },
                { id: "right", position: 100, kind: "indent-left", draggable: true }
            ]
        }
    }

    function bottomMarkerY(ruler) {
        return ruler.height - ruler.markerSize / 2 - Tk.Theme.size.border
    }

    function test_touchEdgeDragUsesWideHitRegionWithoutGrowingArtwork() {
        const formerMode = Tk.Density.modeName
        try {
            Tk.Density.modeName = "touch"
            const ruler = createTemporaryObject(oneMarkerComponent, root, { x: 20, y: 20 })
            verify(ruler !== null)
            waitForRendering(ruler)
            const marker = findChild(ruler, "rulerMarker_left")
            verify(marker !== null)
            compare(marker.width, ruler.markerSize)
            compare(marker.height, ruler.markerSize)
            verify(ruler.markerSize < Tk.Theme.size.controlSm)

            const moved = createTemporaryObject(spyComponent, root,
                { target: ruler, signalName: "markerMoved" })
            const touch = touchEvent(root)
            const edgeX = 60 + Tk.Theme.size.controlSm / 2 - Tk.Theme.size.border
            const y = bottomMarkerY(ruler)
            const pressPoint = ruler.mapToItem(root, edgeX, y)
            touch.press(0, root, pressPoint.x, pressPoint.y)
            touch.commit()
            const moveX = 60 + Tk.Theme.space.xxl
            const movePoint = ruler.mapToItem(root, moveX, y)
            touch.move(0, root, movePoint.x, movePoint.y)
            touch.commit()
            tryVerify(function() { return moved.count > 0 })
            touch.release(0, root, movePoint.x, movePoint.y)
            touch.commit()
            compare(moved.signalArguments[moved.count - 1][0], "left")
            compare(moved.signalArguments[moved.count - 1][1], moveX)
        } finally {
            Tk.Density.modeName = formerMode
        }
    }

    function test_overlappingTargetsChooseNearestAndDoubleClickStillAddsTab() {
        const formerMode = Tk.Density.modeName
        try {
            Tk.Density.modeName = "touch"
            const ruler = createTemporaryObject(nearbyMarkersComponent, root, { y: 20 })
            verify(ruler !== null)
            waitForRendering(ruler)
            const moved = createTemporaryObject(spyComponent, root,
                { target: ruler, signalName: "markerMoved" })
            const added = createTemporaryObject(spyComponent, root,
                { target: ruler, signalName: "tabAdded" })
            const y = bottomMarkerY(ruler)

            // Both 48px target intervals contain 82; the right marker is nearer.
            mousePress(ruler, 82, y)
            mouseMove(ruler, 86, y)
            mouseRelease(ruler, 86, y)
            verify(moved.count > 0)
            compare(moved.signalArguments[moved.count - 1][0], "right")

            // An exact tie keeps the first entry in the caller's marker list.
            const previousCount = moved.count
            mousePress(ruler, 80, y)
            mouseMove(ruler, 84, y)
            mouseRelease(ruler, 84, y)
            verify(moved.count > previousCount)
            compare(moved.signalArguments[moved.count - 1][0], "left")

            mouseDoubleClickSequence(ruler, 160, y)
            compare(added.count, 1)
            compare(added.signalArguments[0][0], 160)
        } finally {
            Tk.Density.modeName = formerMode
        }
    }
}
