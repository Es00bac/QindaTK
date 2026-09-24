// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The fixture owns a small animation document. AutomationModel supplies exact
// time curves and MotionComposition samples hierarchical 2D transforms.
Item {
    id: root
    width: 1480
    height: 1060
    property string playheadTicks: "0"
    property var rendered: ({ok: false, drawnLayers: 0})
    property var exposureRows: [
        {layerId: "layer-ink", label: "Ink · 24 fps", exposures: [
            {exposureId: "exposure-1", startTicks: "0", endTicks: "70560000", artworkId: "drawing-a", label: "A"},
            {exposureId: "exposure-2", startTicks: "70560000", endTicks: "141120000", artworkId: "drawing-a", label: "HOLD", held: true},
            {exposureId: "exposure-3", startTicks: "141120000", endTicks: "211680000", artworkId: "drawing-b", label: "B"},
            {exposureId: "exposure-4", startTicks: "211680000", endTicks: "282240000", artworkId: "drawing-c", label: "C"}]}]
    property var effectList: [
        {effectId: "motion-ease", label: "Motion Ease", categoryId: "motion", category: "Motion", description: "Cubic temporal remap for transform properties.", compatible: true},
        {effectId: "camera-blur", label: "Camera Blur", categoryId: "blur", category: "Blur", description: "Depth-aware optical blur.", compatible: false, reason: "The current composition has no depth channel."},
        {effectId: "trail", label: "Motion Trail", categoryId: "motion", category: "Motion", description: "Echo previous evaluated transforms.", compatible: true}]
    property var compositionLayers: [
        {layerId: "scene-root", parentLayerId: "", sourceId: "", zIndex: 0, visible: true,
         transform: ({x: 0, y: 0, rotation: 0, scaleX: 1, scaleY: 1, opacity: 1})},
        {layerId: "layer-box", parentLayerId: "scene-root", sourceId: "box-art", zIndex: 1, visible: true,
         transform: ({x: 70, y: 76, rotation: 0, scaleX: 1, scaleY: 1, opacity: 1}),
         animatedProperties: ({x: "layer-box/x", y: "layer-box/y", rotation: "layer-box/rotation"})},
        {layerId: "layer-orbit", parentLayerId: "scene-root", sourceId: "orbit-art", zIndex: 2, visible: true,
         transform: ({x: 205, y: 160, rotation: 0, scaleX: 1, scaleY: 1, opacity: 0.8}),
         animatedProperties: ({x: "layer-orbit/x", y: "layer-orbit/y", rotation: "layer-orbit/rotation"})}]

    function evaluateFrame() {
        root.rendered = frameRenderer.render(root.playheadTicks, 560, 360)
    }
    function addCurve(parameterId, name, min, max, initial, keys) {
        const result = animation.addParameterMap({id: parameterId, name: name, dimension: parameterId.indexOf("rotation") >= 0 ? "angle" : "length",
            unitId: parameterId.indexOf("rotation") >= 0 ? "deg" : "px", minimum: min, maximum: max, default: initial,
            interpolation: "linear"})
        if (!result.ok) return false
        for (let i = 0; i < keys.length; ++i) {
            const added = animation.applyEdit({kind: "addPoint", parameterId: parameterId},
                {ticks: keys[i].ticks, value: keys[i].value, interpolation: keys[i].interpolation || "linear"})
            if (!added.ok) return false
        }
        return true
    }
    function acknowledgeExposure(requestId, action, layerId, exposureIds, startTicks, endTicks) {
        if (action === "insertHold") {
            const nextRows = exposureRows.slice()
            const nextLayer = Object.assign({}, nextRows[0])
            nextLayer.exposures = nextLayer.exposures.concat([{exposureId: "host-exposure-" + (nextRows[0].exposures.length + 1),
                startTicks: startTicks, endTicks: endTicks, artworkId: "drawing-a", label: "HOLD", held: true}])
            nextRows[0] = nextLayer
            exposureRows = nextRows
        } else if (action === "delete") {
            const nextRows = exposureRows.slice()
            const nextLayer = Object.assign({}, nextRows[0])
            nextLayer.exposures = nextLayer.exposures.filter(function(row) { return row.exposureId !== exposureIds[0] })
            nextRows[0] = nextLayer
            exposureRows = nextRows
        } else if (action === "retime") {
            const nextRows = exposureRows.slice()
            const nextLayer = Object.assign({}, nextRows[0])
            nextLayer.exposures = nextLayer.exposures.map(function(row) {
                return row.exposureId === exposureIds[0] ? Object.assign({}, row, {startTicks: startTicks, endTicks: endTicks}) : row
            })
            nextRows[0] = nextLayer
            exposureRows = nextRows
        }
        dopeStatus = qsTr("Host accepted %1 · %2 · revision 19").arg(action).arg(requestId)
    }

    property string dopeStatus: qsTr("Four host-owned exposure records")
    property string effectStatus: qsTr("Effect registry ready · composition revision 18")

    Tk.AutomationModel {
        id: animation
        Component.onCompleted: {
            root.addCurve("layer-box/x", "Box · X", -100, 600, 70,
                [{ticks: "0", value: 70}, {ticks: "141120000", value: 300, interpolation: "cubic"}, {ticks: "282240000", value: 420}])
            root.addCurve("layer-box/y", "Box · Y", -100, 600, 76,
                [{ticks: "0", value: 76}, {ticks: "141120000", value: 220}, {ticks: "282240000", value: 98}])
            root.addCurve("layer-box/rotation", "Box · Rotation", -360, 360, 0,
                [{ticks: "0", value: 0}, {ticks: "141120000", value: 18}, {ticks: "282240000", value: 0}])
            root.addCurve("layer-orbit/x", "Orbit · X", -100, 600, 205,
                [{ticks: "0", value: 205}, {ticks: "141120000", value: 380}, {ticks: "282240000", value: 205}])
            root.addCurve("layer-orbit/y", "Orbit · Y", -100, 600, 160,
                [{ticks: "0", value: 160}, {ticks: "141120000", value: 140}, {ticks: "282240000", value: 160}])
            root.addCurve("layer-orbit/rotation", "Orbit · Rotation", -360, 360, 0,
                [{ticks: "0", value: 0}, {ticks: "141120000", value: -45}, {ticks: "282240000", value: -90}])
            root.evaluateFrame()
        }
    }
    Tk.MotionComposition {
        id: composition
        animationModel: animation
        layers: root.compositionLayers
    }
    Tk.MotionFrameRenderer {
        id: frameRenderer
        composition: composition
        Component.onCompleted: {
            loadSource("box-art", Qt.resolvedUrl("box.svg"))
            loadSource("orbit-art", Qt.resolvedUrl("orbit.svg"))
            root.evaluateFrame()
        }
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Motion composition") }
                Tk.Caption { text: qsTr("Exact key times · typed transforms · host-owned edits"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Segmented {
                model: [{text: qsTr("Frame 0"), value: "0"}, {text: qsTr("Frame 12"), value: "141120000"}, {text: qsTr("Frame 24"), value: "282240000"}]
                currentIndex: Math.max(0, ["0", "141120000", "282240000"].indexOf(root.playheadTicks))
                tooltip: qsTr("Evaluate at a host-supplied exact frame time")
                onActivated: function(index) { root.playheadTicks = String(currentValue); root.evaluateFrame() }
            }
            Tk.Mono { text: qsTr("%1 ticks · rev %2").arg(root.playheadTicks).arg(animation.revision); color: Tk.Theme.color.accent }
        }
        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1.25fr) minmax(0, 1fr)"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.sm
            Tk.Panel {
                title: qsTr("Evaluated composition")
                iconName: "layers-2"
                padding: Tk.Theme.space.md
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Item {
                        objectName: "motionCompositionPreview"
                        width: parent.width
                        height: parent.height
                        Tk.Flex.grow: 1
                        Tk.CompositionView {
                            anchors.fill: parent
                            renderer: frameRenderer
                            tooltip: qsTr("Exact-tick composited source artwork")
                        }
                        Tk.Caption {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: Tk.Theme.space.sm
                            text: root.rendered.ok ? qsTr("%1 source layers rendered").arg(root.rendered.drawnLayers) : String(root.rendered.error || "")
                            color: root.rendered.ok ? Tk.Theme.color.textMuted : Tk.Theme.color.warning
                        }
                    }
                    Tk.Caption { text: qsTr("Parent matrices compose deterministically; key values come from exact-tick step/linear/cubic curves."); color: Tk.Theme.color.textMuted; wrapMode: Text.WordWrap }
                }
            }
            Tk.Panel {
                title: qsTr("Keyframe timeline")
                iconName: "key-round"
                padding: Tk.Theme.space.sm
                Tk.KeyframeTimeline {
                    id: keyframes
                    width: parent.width
                    model: animation
                    timeBoundaries: ["0", "70560000", "141120000", "211680000", "282240000"]
                    playheadTicks: root.playheadTicks
                    selectedParameterId: "layer-box/x"
                    tooltip: qsTr("Animated property keys")
                    onEditCommitted: function(transactionId) { root.effectStatus = qsTr("Key edit committed · %1").arg(transactionId); root.evaluateFrame() }
                    onEditRejected: function(reason) { root.effectStatus = reason }
                }
            }
            Tk.Panel {
                title: qsTr("Exposure dope sheet")
                iconName: "grid-3x3"
                padding: Tk.Theme.space.sm
                Tk.DopeSheet {
                    id: dope
                    width: parent.width
                    layers: root.exposureRows
                    frameBoundaries: ["0", "70560000", "141120000", "211680000", "282240000"]
                    selectedLayerId: "layer-ink"
                    playheadTicks: root.playheadTicks
                    viewId: "motion-exposure"
                    statusText: root.dopeStatus
                    onExposureEditRequested: function(requestId, action, layerId, ids, start, end) {
                        root.acknowledgeExposure(requestId, action, layerId, ids, start, end)
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Effect registry")
                iconName: "wand-sparkles"
                padding: Tk.Theme.space.sm
                Tk.EffectBrowser {
                    id: effects
                    width: parent.width
                    effects: root.effectList
                    categories: [{label: qsTr("All"), value: "all"}, {label: qsTr("Motion"), value: "motion"}, {label: qsTr("Blur"), value: "blur"}]
                    targetLayerId: "layer-box"
                    documentId: "doc-motion-1"
                    revision: "18"
                    selectedEffectId: "motion-ease"
                    statusText: root.effectStatus
                    onEffectPreviewRequested: function(requestId, effectId, targetId) {
                        root.effectStatus = qsTr("Preview job requested · %1 → %2 · %3").arg(effectId).arg(targetId).arg(requestId)
                    }
                    onEffectInsertRequested: function(requestId, effectId, targetId, revision) {
                        root.effectStatus = qsTr("Host inserted %1 on %2 at rev %3 · %4").arg(effectId).arg(targetId).arg(revision).arg(requestId)
                    }
                }
            }
        }
    }
}
