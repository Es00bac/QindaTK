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

    Tk.EffectBrowser {
        id: browser
        y: 190
        width: 360
        height: 240
        targetLayerId: "child"
        revision: composition.revision
        effects: [{effectId: "grayscale", label: "Grayscale", category: "Color", compatible: true},
                  {effectId: "blur", label: "Blur", category: "Blur", compatible: false, reason: "Needs a GPU blur"}]
        onEffectInsertRequested: function(requestId, effectId, targetLayerId, expectedRevision) {
            root.insertResult = composition.insertEffect(targetLayerId, {effectId: effectId}, -1, expectedRevision)
        }
    }
    property var insertResult: ({})
    Tk.DopeSheet {
        id: dope
        y: 440
        width: 480
        height: 160
        frameBoundaries: ["0", "10", "20", "30"]
        layers: [{layerId: "ink", label: "Ink", exposures: [{exposureId: "e1", startTicks: "0", endTicks: "10", artworkId: "a1"}]}]
    }
    SignalSpy { id: exposureSpy; target: dope; signalName: "exposureEditRequested" }

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

    function test_effect_browser_insert_is_one_layer_transaction() {
        const effectsOf = function() {
            return composition.layers.find(function(layer) { return layer.layerId === "child" }).effects || []
        }
        verify(browser.select("blur"))
        verify(!browser.insert()) // incompatible rows are refused with their reason
        verify(!composition.canUndo)
        verify(browser.select("grayscale"))
        verify(browser.insert())
        verify(insertResult.ok)
        compare(effectsOf().length, 1)
        compare(effectsOf()[0].effectId, "grayscale")
        verify(composition.canUndo)
        verify(composition.undo())
        compare(effectsOf().length, 0)
        verify(!composition.canUndo)
    }

    function test_dope_sheet_gesture_emits_one_request() {
        exposureSpy.clear()
        verify(dope.select("ink", "e1"))
        verify(dope.retimeSelected(1))
        compare(exposureSpy.count, 1)
        compare(exposureSpy.signalArguments[0][1], "retime")
        compare(exposureSpy.signalArguments[0][4], "10")
        compare(exposureSpy.signalArguments[0][5], "20")
        dope.pendingRequestId = exposureSpy.signalArguments[0][0]
        verify(!dope.retimeSelected(1)) // one request in flight per gesture
        compare(exposureSpy.count, 1)
        dope.pendingRequestId = ""
    }
}
