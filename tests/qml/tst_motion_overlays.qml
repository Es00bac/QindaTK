// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    visible: true
    name: "MotionOverlays"
    when: windowShown
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
}
