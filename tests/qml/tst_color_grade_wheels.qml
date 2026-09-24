// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-072 / VID-08: ColorWheels bound to a real ColorGradeEditSession. A
// pointer drag and numeric entry each become one undo step of the session's
// grade, and the wheels redraw from the session's descriptors.
TestCase {
    id: root
    name: "ColorWheelsDriveGradeSession"
    when: windowShown
    visible: true
    width: 700
    height: 300

    Tk.ColorGradePreviewModel { id: fixture }
    Tk.ColorGradeEditSession {
        id: session
        clipId: "clip-wheels"
        frameId: "frame-1"
        sourceImage: fixture.sourceImage
    }
    Tk.ColorWheels {
        id: bound
        width: 660
        height: 220
        visible: true
        gradeId: session.clipId
        wheels: session.wheels
        selectedWheelId: "lift"
        onWheelPreviewRequested: function(id, grade, wheel, x, y) { session.previewWheel(id, wheel, x, y) }
        onWheelEditFinished: function(id, grade, wheel, x, y) { session.finishWheel(id, wheel, x, y) }
        onWheelEditCanceled: function(id) { session.cancelGesture(id) }
        onResetRequested: function(grade, wheel) { session.resetWheel(wheel) }
    }

    function test_drag_and_numeric_edits_are_single_undo_steps() {
        compare(session.wheels.length, 3)
        compare(session.wheels[0].id, "lift")
        verify(session.wheels[0].enabled)
        verify(!session.canUndo)
        // Lift is the first of three 220-pixel slots: centre (110, 110).
        mousePress(bound, 110, 110)
        mouseMove(bound, 130, 100)
        mouseMove(bound, 150, 90)
        mouseRelease(bound, 150, 90)
        verify(!session.editing)
        verify(session.canUndo)
        verify(session.parameters.lift.x > 0)
        verify(session.parameters.lift.y > 0)
        compare(session.wheels[0].x, session.parameters.lift.x)
        verify(session.undo())
        verify(!session.canUndo) // the drag and all its previews were one step
        compare(session.parameters.lift.x, 0)

        verify(bound.requestWheelValue("gain", 0.3, -0.2))
        compare(session.parameters.gain.x, 0.3)
        compare(session.parameters.gain.y, -0.2)
        verify(session.canUndo)
        verify(bound.requestReset("gain"))
        compare(session.parameters.gain.x, 0)
        verify(session.undo())
        compare(session.parameters.gain.x, 0.3)
        verify(session.undo())
        verify(!session.canUndo)
    }
}
