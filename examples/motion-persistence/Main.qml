// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 1040
    height: 680
    property string playheadTicks: "141120000"
    property url projectUrl: Qt.resolvedUrl("motion-session.qtmotion")
    property string statusText: qsTr("Host-owned scene ready · source bytes remain external")
    property var frameResult: ({ok: false})
    function addCurve(id, label, initial, left, right) {
        const descriptor = animation.addParameterMap({id: id, name: label, dimension: "length", unitId: "px", minimum: 0, maximum: 520, default: initial, interpolation: "linear"})
        if (!descriptor.ok) { statusText = String(descriptor.error); return false }
        for (let i = 0; i < 2; ++i) {
            const record = i === 0 ? left : right
            const added = animation.applyEdit({kind: "addPoint", parameterId: id}, {ticks: record.ticks, value: record.value, interpolation: "linear"})
            if (!added.ok) { statusText = String(added.error); return false }
        }
        return true
    }
    function draw() { frameResult = renderer.render(playheadTicks, 640, 360) }
    function saveScene() {
        if (composition.saveToFile(projectUrl)) statusText = qsTr("Saved versioned composition · %1 layers · %2 exact-time keys").arg(composition.layers.length).arg(animation.points("layer/box/x").length)
        else statusText = composition.lastError
    }
    function changeKey() {
        const point = animation.points("layer/box/x")[0]
        const changed = animation.applyEdit({kind: "setPoint", parameterId: "layer/box/x", pointId: point.id}, {value: 28})
        statusText = changed.ok ? qsTr("Host changed key %1; reopen restores the saved value").arg(point.id) : String(changed.error)
        draw()
    }
    function reopenScene() {
        const result = composition.loadFromFile(projectUrl)
        statusText = result.ok ? qsTr("Reopened %1 layers and %2 automation curves").arg(result.layerCount).arg(result.animationParameterCount) : String(result.error)
        draw()
    }
    Component.onCompleted: {
        if (addCurve("layer/box/x", "Box X", 60, {ticks: "0", value: 60}, {ticks: "282240000", value: 420})
            && addCurve("layer/box/y", "Box Y", 92, {ticks: "0", value: 92}, {ticks: "282240000", value: 210}))
            Qt.callLater(root.draw)
    }
    Tk.AutomationModel { id: animation }
    Tk.MotionComposition {
        id: composition
        animationModel: animation
        layers: [
            {layerId: "scene-root", sourceId: "", zIndex: 0, visible: true,
             transform: ({x: 0, y: 0, opacity: 1})},
            {layerId: "layer/box", parentLayerId: "scene-root", sourceId: "asset:box-art", zIndex: 3, visible: true,
             transform: ({x: 0, y: 0, opacity: 0.9}),
             animatedProperties: ({x: "layer/box/x", y: "layer/box/y"}),
             effects: [{effectId: "blur-soft", enabled: false, parameters: ({radius: 3.5})}]}]
    }
    Tk.MotionFrameRenderer {
        id: renderer
        composition: composition
        Component.onCompleted: loadSource("asset:box-art", Qt.resolvedUrl("box.svg"))
    }
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            Tk.Flex { direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Motion composition persistence") }
                Tk.Caption { text: qsTr("Versioned manifest · exact-tick curves · stable external asset IDs"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button { objectName: "motionSave"; text: qsTr("Save local"); iconName: "save"; variant: "accent"; tooltip: qsTr("Atomically save this composition JSON beside the fixture"); onClicked: root.saveScene() }
            Tk.Button { objectName: "motionChangeKey"; text: qsTr("Change key"); iconName: "move"; tooltip: qsTr("Change a host-owned key before reopening the saved scene"); onClicked: root.changeKey() }
            Tk.Button { objectName: "motionReopen"; text: qsTr("Reopen"); iconName: "rotate-ccw"; tooltip: qsTr("Reload the saved composition and automation snapshot"); onClicked: root.reopenScene() }
        }
        Tk.Grid {
            width: parent.width; height: parent.height
            Tk.Flex.grow: 1; Tk.Flex.basis: 0
            columns: "minmax(0, 1.25fr) minmax(0, 1fr)"
            columnGap: Tk.Theme.space.md
            Tk.Panel {
                title: qsTr("Rendered scene")
                iconName: "layers-2"
                padding: Tk.Theme.space.md
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Tk.CompositionView { width: parent.width; height: parent.height; Tk.Flex.grow: 1; renderer: renderer; tooltip: qsTr("Frame composed from externally supplied source pixels") }
                    Tk.Caption { text: frameResult.ok ? qsTr("%1 evaluated layers at %2 ticks").arg(frameResult.drawnLayers).arg(playheadTicks) : String(frameResult.error || "Renderer ready"); color: frameResult.ok ? Tk.Theme.color.accent : Tk.Theme.color.warning }
                }
            }
            Tk.Panel {
                title: qsTr("Document manifest")
                iconName: "file-json"
                padding: Tk.Theme.space.md
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    Tk.Badge { text: qsTr("qindatk.motion-composition · v1"); variant: "accent"; tooltip: qsTr("Versioned composition data format") }
                    Tk.Caption { text: qsTr("Layer IDs, hierarchy, visibility, transforms, effect parameter records and exact decimal tick keys are saved. Asset bytes stay external behind stable source IDs."); wrapMode: Text.WordWrap; color: Tk.Theme.color.textMuted }
                    Tk.Caption { text: qsTr("Effect records persist as descriptors; this slice does not execute effects."); wrapMode: Text.WordWrap; color: Tk.Theme.color.textMuted }
                    Tk.Spacer { Tk.Flex.grow: 1 }
                    Tk.Caption { objectName: "motionPersistenceStatus"; text: statusText; wrapMode: Text.WordWrap; color: composition.lastError.length ? Tk.Theme.color.warning : Tk.Theme.color.text }
                }
            }
        }
    }
}
