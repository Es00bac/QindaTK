// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    visible: true
    name: "MotionEvaluator"
    when: windowShown
    readonly property string startTicks: "9007199254740990"
    readonly property string middleTicks: "9007199254741090"
    readonly property string endTicks: "9007199254741190"

    Tk.AutomationModel { id: model }
    Tk.MotionComposition {
        id: composition
        animationModel: model
        layers: [
            {layerId: "parent", parentLayerId: "", sourceId: "bg", zIndex: 0,
             transform: ({x: 10, y: 5, rotation: 0, scaleX: 1, scaleY: 1, opacity: 0.5})},
            {layerId: "child", parentLayerId: "parent", sourceId: "shape", zIndex: 1,
             transform: ({x: 2, y: 2, rotation: 0, scaleX: 1, scaleY: 1, opacity: 1}),
             animatedProperties: ({x: "layer/child/x"})}]
    }
    Tk.KeyframeTimeline {
        id: timeline
        width: 760
        height: 180
        model: model
        parameterIds: ["layer/child/x"]
        selectedParameterId: "layer/child/x"
        timeBoundaries: [root.startTicks, root.middleTicks, root.endTicks]
    }

    function init() {
        model.clearHistory()
        model.removeParameter("layer/child/x")
        verify(model.addParameterMap({id: "layer/child/x", name: "Child X", dimension: "length", unitId: "px",
            minimum: -100, maximum: 100, default: 0, interpolation: "linear"}).ok)
        verify(model.applyEdit({kind: "addPoint", parameterId: "layer/child/x"},
            {ticks: startTicks, value: 0, interpolation: "linear"}).ok)
        verify(model.applyEdit({kind: "addPoint", parameterId: "layer/child/x"},
            {ticks: endTicks, value: 100, interpolation: "linear"}).ok)
    }

    function test_exact_composition_sample_and_undoable_key_edit() {
        const before = composition.evaluate(middleTicks)
        verify(before.ok)
        compare(before.timeTicks, middleTicks)
        const childBefore = before.layers.find(function(layer) { return layer.layerId === "child" })
        compare(childBefore.matrix[4], 60) // parent x 10 + animated child x 50
        compare(childBefore.matrix[5], 7)
        compare(childBefore.opacity, 0.5)

        const first = model.points("layer/child/x")[0]
        verify(timeline.choose("layer/child/x", first.id))
        verify(timeline.moveSelectedKey(1))
        compare(model.pointData("layer/child/x", first.id).ticks, middleTicks)
        const edited = composition.evaluate(middleTicks)
        const childEdited = edited.layers.find(function(layer) { return layer.layerId === "child" })
        compare(childEdited.matrix[4], 10)
        verify(model.canUndo)
        verify(model.undo())
        const restored = composition.evaluate(middleTicks)
        compare(restored.layers.find(function(layer) { return layer.layerId === "child" }).matrix[4], 60)
    }
}
