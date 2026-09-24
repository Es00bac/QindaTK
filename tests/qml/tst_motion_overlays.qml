// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    visible: true
    name: "MotionOverlays"
    when: windowShown
    width: 820
    height: 460
    property int commits: 0
    property string requestedLayer: ""
    property string requestedKey: ""
    property string requestedTicks: ""
    Tk.AutomationModel { id: animation }
    Tk.MotionComposition {
        id: composition
        animationModel: animation
        layers: [{layerId: "subject", sourceId: "", zIndex: 0,
            transform: ({x: 0, y: 0}), animatedProperties: ({x: "subject/x"})}]
    }
    Tk.MotionPathOverlay {
        id: path
        width: 400
        height: 200
        composition: composition
        layerId: "subject"
        sampleTicks: ["0", "10", "20"]
        keyPoints: animation.points("subject/x")
        onKeySelectionRequested: function(layerId, keyId, ticks) {
            root.requestedLayer = layerId; root.requestedKey = keyId; root.requestedTicks = ticks
        }
    }
    // Editing fixture: a 200x100 world frame letterboxed into the overlay.
    Tk.AutomationModel { id: editAnimation }
    Tk.MotionComposition {
        id: editComposition
        animationModel: editAnimation
        layers: [{layerId: "ball", sourceId: "", zIndex: 0, transform: ({x: 0, y: 0}),
            animatedProperties: ({x: "ball/x", y: "ball/y"})}]
    }
    Tk.MotionPathOverlay {
        id: editPath
        x: 410
        width: 400
        height: 220
        composition: editComposition
        layerId: "ball"
        frameWidth: 200
        frameHeight: 100
        sampleTicks: ["0", "5", "10", "15", "20"]
        onPathEditCommitted: function(transactionId) { root.commits += 1 }
    }
    Tk.MotionFrameRenderer { id: viewRenderer; composition: editComposition }
    Tk.CompositionView {
        id: view
        x: 410
        y: 230
        width: 200
        height: 120
        renderer: viewRenderer
        timeTicks: "10"
        frameWidth: 20
        frameHeight: 10
    }
    function addEditCurve(id, maximum, values) {
        editAnimation.removeParameter(id)
        verify(editAnimation.addParameterMap({id: id, name: id, dimension: "length", unitId: "px", minimum: 0, maximum: maximum, default: 0, interpolation: "linear"}).ok)
        for (let i = 0; i < values.length; ++i)
            verify(editAnimation.applyEdit({kind: "addPoint", parameterId: id}, {ticks: String(i * 10), value: values[i], interpolation: "linear"}).ok)
    }
    function initEdit() {
        addEditCurve("ball/x", 200, [20, 100, 180])
        addEditCurve("ball/y", 100, [50, 20, 50])
        editAnimation.clearHistory()
        editPath.selectedKeyId = ""
        editPath.keyPoints = editAnimation.points("ball/x")
        editPath.rebuild()
        commits = 0
    }
    function init() {
        animation.removeParameter("subject/x")
        verify(animation.addParameterMap({id: "subject/x", name: "X", dimension: "length", unitId: "px", minimum: 0, maximum: 20, default: 0, interpolation: "linear"}).ok)
        verify(animation.applyEdit({kind: "addPoint", parameterId: "subject/x"}, {ticks: "0", value: 0, interpolation: "linear"}).ok)
        verify(animation.applyEdit({kind: "addPoint", parameterId: "subject/x"}, {ticks: "20", value: 20, interpolation: "linear"}).ok)
        path.keyPoints = animation.points("subject/x")
        path.rebuild()
        requestedLayer = ""; requestedKey = ""; requestedTicks = ""
    }
    function test_samples_composition_and_requests_stable_key_id() {
        compare(path.pathPoints.length, 3)
        compare(path.pathPoints[0].x, 0)
        compare(path.pathPoints[1].x, 10)
        compare(path.pathPoints[2].x, 20)
        compare(path.keyPoints.length, 2)
        const key = animation.points("subject/x")[0]
        const pos = path.mapPoint(path.pathPoints[0])
        verify(path.requestNearestKeyAt(pos.x, pos.y))
        compare(requestedLayer, "subject")
        compare(requestedKey, key.id)
        compare(requestedTicks, "0")
    }
    function test_key_drag_is_one_world_space_transaction() {
        initEdit()
        const keyX = editAnimation.points("ball/x")[1], keyY = editAnimation.points("ball/y")[1]
        const at = editPath.mapPoint(editPath.pathPoints[2])
        mouseDrag(editPath, at.x, at.y, 30, 20)
        tryCompare(root, "commits", 1)
        compare(editAnimation.historyCount, 1)
        compare(editPath.selectedKeyId, String(keyX.id))
        const scale = editPath.worldView.sx
        verify(Math.abs(editAnimation.pointData("ball/x", keyX.id).value - (100 + 30 / scale)) < 1)
        verify(Math.abs(editAnimation.pointData("ball/y", keyY.id).value - (20 + 20 / scale)) < 1)
        verify(editAnimation.undo())
        compare(editAnimation.pointData("ball/x", keyX.id).value, 100)
        compare(editAnimation.pointData("ball/y", keyY.id).value, 20)
    }
    function test_tangent_drag_is_one_transaction() {
        initEdit()
        const keyX = editAnimation.points("ball/x")[1], keyY = editAnimation.points("ball/y")[1]
        editPath.selectedKeyId = String(keyX.id)
        verify(editPath.handles.outHandle !== undefined)
        verify(editPath.handles.inHandle !== undefined)
        const handle = editPath.mapPoint(editPath.handles.outHandle)
        mouseDrag(editPath, handle.x, handle.y, 0, 25)
        tryCompare(root, "commits", 1)
        compare(editAnimation.historyCount, 1)
        compare(editAnimation.pointData("ball/x", keyX.id).interpolation, "cubic")
        compare(editAnimation.pointData("ball/y", keyY.id).interpolation, "cubic")
        verify(editPath.handles.outHandle.y > 30 + 20 / editPath.worldView.sy - 1)
        verify(editAnimation.undo())
        compare(editAnimation.pointData("ball/y", keyY.id).interpolation, "linear")
    }
    function test_composition_view_rerenders_stale_frames() {
        initEdit()
        tryCompare(viewRenderer, "frameTicks", "10")
        tryVerify(function() { return !view.frameStale })
        const key = editAnimation.points("ball/x")[1]
        verify(editAnimation.applyEdit({kind: "setPoint", parameterId: "ball/x", pointId: key.id}, {value: 120}).ok)
        verify(view.frameStale)
        tryVerify(function() { return !view.frameStale })
        compare(viewRenderer.frameTicks, "10")
    }
}
