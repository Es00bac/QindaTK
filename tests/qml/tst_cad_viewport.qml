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
    Component { id: sketchComponent; Tk.CadDocument {} }
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
        viewport.document = sketch
        viewport.mode = "point"
        viewport.handleTap(Qt.point(90, 90))
        compare(sketch.points.length, 1)
        const first = sketch.points[0].entityId
        viewport.mode = "line"
        viewport.handleTap(Qt.point(90, 90))
        compare(viewport.pending.pointId, first)
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
        viewport.mode = "arc"
        viewport.handleTap(Qt.point(300, 150))
        viewport.handleTap(Qt.point(330, 150))
        viewport.handleTap(Qt.point(300, 180))
        compare(sketch.arcs.length, 1)
        compare(sketch.arcs[0].sweepDegrees, 90)
        const endpoints = sketch.segments[0]
        const constraint = sketch.addConstraint("distance", endpoints.startPointId,
                                                endpoints.endPointId, sketch.measure(segment).length)
        verify(constraint.length > 0)
        viewport.mode = "select"
        viewport.selectedEntityId = segment
        const editor = findChild(viewport, "cadDimensionEditor_" + segment)
        verify(editor !== null)
        verify(editor.visible)
        editor.commit(60)
        verify(Math.abs(sketch.measure(segment).length - 60) < 0.001)
        const before = viewport.screenX(sketch.points[1].x)
        verify(sketch.changeUnits("in"))
        compare(viewport.screenX(sketch.points[1].x), before)
    }

    function freshSketch() {
        const doc = createTemporaryObject(sketchComponent, testCase)
        viewport.document = doc
        viewport.mode = "select"
        viewport.select("", false)
        viewport.forceActiveFocus()
        return doc
    }

    function segmentId(doc, index) { return String(doc.segments[index].entityId) }

    // WP-083 audit: the first tap used to commit its point at once, so
    // Escape left an orphan point and a finished tool took several undos.
    function test_cancel_and_undo() {
        const doc = freshSketch()
        viewport.mode = "line"
        viewport.handleTap(Qt.point(30, 30))
        verify(viewport.pending !== null)
        compare(doc.points.length, 0) // nothing written before the tool finishes
        keyClick(Qt.Key_Escape)
        compare(viewport.pending, null)
        compare(doc.points.length, 0)
        verify(!doc.canUndo)
        viewport.handleTap(Qt.point(30, 30))
        viewport.handleTap(Qt.point(150, 30))
        compare(doc.points.length, 2)
        compare(doc.segments.length, 1)
        keyClick(Qt.Key_Z, Qt.ControlModifier) // one step removes both points and the line
        compare(doc.points.length, 0)
        compare(doc.segments.length, 0)
        verify(!doc.canUndo)
        keyClick(Qt.Key_Y, Qt.ControlModifier)
        compare(doc.segments.length, 1)
        keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier) // redo again: nothing left
        compare(doc.segments.length, 1)
        viewport.mode = "circle"
        viewport.handleTap(Qt.point(90, 150))
        viewport.handleTap(Qt.point(90, 180))
        compare(doc.circles.length, 1)
        compare(doc.circles[0].radius, 10)
        verify(doc.undo())
        compare(doc.circles.length, 0)
        compare(doc.points.length, 2)
        // A tool that starts on an existing point reuses it.
        viewport.mode = "line"
        viewport.handleTap(Qt.point(30, 30))
        compare(viewport.pending.pointId, String(doc.points[0].entityId))
        // A rejected finish (zero length) rolls back and reports why.
        viewport.handleTap(Qt.point(30, 30))
        compare(doc.points.length, 2)
        compare(doc.segments.length, 1)
        verify(doc.lastError.length > 0)
        viewport.mode = "arc"
        viewport.handleTap(Qt.point(300, 150))
        viewport.handleTap(Qt.point(330, 150))
        keyClick(Qt.Key_Escape)
        compare(doc.points.length, 2)
        compare(doc.arcs.length, 0)
        viewport.mode = "select"
        viewport.handleTap(Qt.point(90, 30))
        compare(viewport.selectedEntityId, segmentId(doc, 0))
        keyClick(Qt.Key_Delete)
        compare(doc.segments.length, 0)
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(doc.segments.length, 1)
        keyClick(Qt.Key_Escape) // no tool pending: clears the selection
        compare(viewport.selection.length, 0)
    }

    function test_constraint_tools_and_removal() {
        const doc = freshSketch()
        const a = doc.addPoint(10, 10), b = doc.addPoint(50, 14)
        const line = doc.addSegment(a, b)
        const c = doc.addPoint(30, 40)
        const circle = doc.addCircle(c, 5)
        const loose = doc.addPoint(80, 80)
        viewport.select(line, false)
        compare(viewport.constraintOptions, ["horizontal", "vertical", "distance", "angle"])
        const inspector = findChild(viewport, "cadInspector")
        verify(inspector.visible)
        waitForRendering(viewport) // Flex places the buttons at polish
        const horizontal = findChild(viewport, "cadConstrain_horizontal")
        verify(horizontal !== null && horizontal.visible)
        mouseClick(horizontal)
        compare(doc.constraints.length, 1)
        compare(doc.constraints[0].kind, "horizontal")
        compare(doc.measure(a).y, doc.measure(b).y)
        keyClick(Qt.Key_D) // distance at the current length
        compare(doc.constraints.length, 2)
        compare(doc.constraints[1].kind, "distance")
        fuzzyCompare(doc.constraints[1].value, doc.measure(line).length, 1e-9)
        verify(findChild(viewport, "cadDimensionEditor_" + line).visible)
        // Two points (Ctrl+click) offer coincident and distance.
        viewport.select(b, false)
        viewport.select(loose, true)
        compare(viewport.selection.length, 2)
        compare(viewport.constraintOptions, ["coincident", "distance"])
        // Segment plus circle offers tangent.
        viewport.select(line, false)
        viewport.select(circle, true)
        compare(viewport.constraintOptions, ["tangent"])
        keyClick(Qt.Key_T)
        compare(doc.constraints.length, 3)
        fuzzyCompare(Math.abs(doc.measure(c).y - doc.measure(a).y), 5, 1e-6)
        // The inspector lists and removes constraints on the selection.
        viewport.select(line, false)
        const tangent = String(doc.constraints[2].constraintId)
        const remove = findChild(viewport, "cadRemoveConstraint_" + tangent)
        verify(remove !== null)
        waitForRendering(viewport)
        const before = doc.points
        mouseClick(remove)
        compare(doc.constraints.length, 2)
        compare(doc.points, before)
        verify(doc.undo())
        compare(doc.constraints.length, 3)
        verify(!viewport.applyConstraint("coincident")) // not offered for one segment
    }

    function test_radius_and_arc_editing() {
        const doc = freshSketch()
        const circle = doc.addCircle(doc.addPoint(40, 40), 8)
        const arc = doc.addArc(doc.addPoint(100, 40), 10, 0, 90)
        viewport.select(circle, false)
        const radius = findChild(viewport, "cadRadiusEditor")
        verify(radius.visible)
        compare(radius.value, 8)
        radius.commit(12.5)
        compare(doc.circles[0].radius, 12.5)
        verify(doc.undo())
        compare(radius.value, 8) // the field follows the document
        viewport.select(arc, false)
        compare(radius.value, 10)
        const sweep = findChild(viewport, "cadArcSweepEditor")
        const start = findChild(viewport, "cadArcStartEditor")
        verify(sweep.visible && start.visible)
        sweep.commit(135)
        compare(doc.arcs[0].sweepDegrees, 135)
        start.commit(30)
        compare(doc.arcs[0].startDegrees, 30)
        radius.commit(15)
        compare(doc.arcs[0].radius, 15)
        compare(doc.arcs[0].sweepDegrees, 135)
        doc.changeUnits("in")
        fuzzyCompare(radius.value, 15 / 25.4, 0.001)
    }
}
