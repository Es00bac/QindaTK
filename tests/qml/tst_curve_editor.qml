// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "CurveEditor"
    when: windowShown
    visible: true
    width: 420
    height: 260

    Component {
        id: editorComponent
        Tk.CurveEditor {
            width: 400; height: 240
            knots: [{ id: "k-left", x: 0.25, y: 0.75 }, { id: "k-right", x: 0.8, y: 0.2 }]
            samples: [{ x: 0, y: 0 }, { x: 0.5, y: 0.6 }, { x: 1, y: 1 }]
        }
    }

    function test_normalized_mapping_stable_id_and_cancel() {
        const editor = createTemporaryObject(editorComponent, this)
        verify(editor !== null)
        const pixels = editor.pixelAt(0.25, 0.75)
        const mapped = editor.normalizedAt(pixels.x, pixels.y)
        fuzzyCompare(mapped.x, 0.25, 0.0001)
        fuzzyCompare(mapped.y, 0.75, 0.0001)

        const selectSpy = createTemporaryObject(spyComponent, this,
                                                { target: editor, signalName: "knotSelected" })
        const previewSpy = createTemporaryObject(spyComponent, this,
                                                 { target: editor, signalName: "movePreview" })
        const cancelSpy = createTemporaryObject(spyComponent, this,
                                                { target: editor, signalName: "moveCancelled" })
        verify(editor.selectKnot("k-right"))
        compare(selectSpy.count, 1)
        compare(selectSpy.signalArguments[0][0], "k-right")
        verify(editor.requestMove("k-right", 0.82, 0.3))
        compare(previewSpy.signalArguments[0][0], "k-right")
        fuzzyCompare(previewSpy.signalArguments[0][1], 0.82, 0.0001)
        fuzzyCompare(previewSpy.signalArguments[0][2], 0.3, 0.0001)
        verify(editor.cancelMove("k-right"))
        compare(cancelSpy.count, 1)
        compare(cancelSpy.signalArguments[0][0], "k-right")
        compare(editor.knots[1].x, 0.8) // requests never mutate host-owned canonical state
    }

    function test_pointer_drag_survives_host_knot_refresh() {
        const editor = createTemporaryObject(editorComponent, this)
        verify(editor !== null)
        const previewSpy = createTemporaryObject(spyComponent, this,
                                                 { target: editor, signalName: "movePreview" })
        const finishSpy = createTemporaryObject(spyComponent, this,
                                                { target: editor, signalName: "moveFinished" })
        const start = editor.pixelAt(0.25, 0.75)
        mouseClick(editor, start.x, start.y)
        compare(finishSpy.count, 0) // selecting a knot is not an edit
        mousePress(editor, start.x, start.y)
        mouseMove(editor, start.x + 28, start.y + 18)
        verify(previewSpy.count > 0)
        compare(previewSpy.signalArguments[0][0], "k-left")
        editor.knots = [{ id: "k-left", x: 0.31, y: 0.69 },
                        { id: "k-right", x: 0.8, y: 0.2 }]
        mouseMove(editor, start.x + 36, start.y + 22)
        mouseRelease(editor, start.x + 36, start.y + 22)
        compare(finishSpy.count, 1)
        compare(finishSpy.signalArguments[0][0], "k-left")
    }

    Component { id: spyComponent; SignalSpy {} }
}
