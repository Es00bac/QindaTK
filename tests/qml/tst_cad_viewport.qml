// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: testCase
    name: "CadViewport"
    when: windowShown
    visible: true
    width: 500
    height: 320

    Tk.CadDocument { id: sketch }
    Tk.CadViewport {
        id: viewport
        width: parent.width
        height: parent.height
        document: sketch
        panX: 0
        panY: 0
        zoom: 3
    }

    function test_creation_selection_and_units() {
        viewport.mode = "point"
        viewport.handleTap(Qt.point(90, 90))
        compare(sketch.points.length, 1)
        const first = sketch.points[0].entityId
        viewport.mode = "line"
        viewport.handleTap(Qt.point(90, 90))
        compare(viewport.pendingPointId, first)
        viewport.handleTap(Qt.point(240, 90))
        compare(sketch.points.length, 2)
        compare(sketch.segments.length, 1)
        const segment = sketch.segments[0].entityId
        compare(sketch.measure(segment).length, 50)
        viewport.mode = "circle"
        viewport.handleTap(Qt.point(90, 90))
        viewport.handleTap(Qt.point(90, 150))
        compare(sketch.circles.length, 1)
        compare(sketch.circles[0].radius, 20)
        viewport.mode = "select"
        viewport.handleTap(Qt.point(165, 90))
        compare(viewport.selectedEntityId, segment)
        mouseDrag(viewport, 90, 90, 30, 0, Qt.LeftButton)
        verify(sketch.points[0].x > 30)
        const before = viewport.screenX(sketch.points[1].x)
        verify(sketch.changeUnits("in"))
        compare(viewport.screenX(sketch.points[1].x), before)
    }
}
