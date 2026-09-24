// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 1060
    height: 720
    property string ticks: "141120000"
    property var rendered: ({ok: false})
    property string setupStatus: "Fixture setup pending"
    property var motionKeys: []
    property var pathControl: null
    property var onionControl: null
    function addCurve(id, name, initial, values) {
        const added = animation.addParameterMap({id: id, name: name, dimension: "length", unitId: "px", minimum: 0, maximum: 520, default: initial, interpolation: "linear"})
        if (!added.ok) { setupStatus = String(added.error); return }
        for (let i = 0; i < values.length; ++i) {
            const point = animation.applyEdit({kind: "addPoint", parameterId: id}, {ticks: values[i].ticks, value: values[i].value, interpolation: "linear"})
            if (!point.ok) { setupStatus = String(point.error); return }
        }
        setupStatus = qsTr("%1 curve ready · x@0 %2").arg(id).arg(animation.valueAt(id, "0"))
    }
    function renderAt(t) {
        ticks = String(t)
        rendered = renderer.render(ticks, 640, 360)
        if (pathControl) pathControl.rebuild()
        Qt.callLater(function() { if (onionControl) onionControl.refresh() })
    }
    function initializeFixture() {
        addCurve("ball/x", "Ball · X", 72, [{ticks: "0", value: 72}, {ticks: "141120000", value: 285}, {ticks: "282240000", value: 500}])
        addCurve("ball/y", "Ball · Y", 150, [{ticks: "0", value: 150}, {ticks: "141120000", value: 90}, {ticks: "282240000", value: 210}])
        motionKeys = animation.points("ball/x")
        setupStatus = qsTr("%1 X keys · %2 Y keys").arg(motionKeys.length).arg(animation.points("ball/y").length)
        if (pathControl && motionKeys.length > 1) pathControl.selectedKeyId = String(motionKeys[1].id)
        renderer.loadSource("ball-art", Qt.resolvedUrl("box.svg"))
        renderAt(ticks)
    }
    Component.onCompleted: Qt.callLater(initializeFixture)
    Tk.AutomationModel {
        id: animation

    }
    Tk.MotionComposition {
        id: composition
        animationModel: animation
        layers: [{layerId: "ball", sourceId: "ball-art", zIndex: 1,
            transform: ({x: 0, y: 0, scaleX: 1, scaleY: 1, opacity: 1}),
            animatedProperties: ({x: "ball/x", y: "ball/y"})}]
    }
    Tk.MotionFrameRenderer {
        id: renderer
        composition: composition

    }
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Motion path and onion skin") }
                Tk.Caption { text: qsTr("Host-owned curves · exact neighboring ticks · sampled path"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Segmented {
                model: [{text: qsTr("Previous"), value: "0"}, {text: qsTr("Current"), value: "141120000"}, {text: qsTr("Next"), value: "282240000"}]
                currentIndex: ["0", "141120000", "282240000"].indexOf(root.ticks)
                tooltip: qsTr("View one of the adjacent exact-time frames")
                onActivated: function(index) { root.renderAt(String(currentValue)) }
            }
        }
        Tk.Grid {
            width: parent.width; height: parent.height
            Tk.Flex.grow: 1; Tk.Flex.basis: 0
            columns: "minmax(0, 1.3fr) minmax(0, 1fr)"
            columnGap: Tk.Theme.space.md
            Tk.Panel {
                title: qsTr("Sampled transform path")
                iconName: "move"
                padding: Tk.Theme.space.md
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Tk.MotionPathOverlay {
                        id: pathOverlay
                        Component.onCompleted: root.pathControl = pathOverlay
                        width: parent.width; height: parent.height
                        Tk.Flex.grow: 1
                        composition: composition
                        layerId: "ball"
                        sampleTicks: ["0", "35280000", "70560000", "105840000", "141120000", "176400000", "211680000", "246960000", "282240000"]
                        keyPoints: root.motionKeys
                        frameWidth: 640
                        frameHeight: 360
                        onKeySelectionRequested: function(layer, key, time) { status.text = qsTr("Selection request · %1 · %2 · %3 ticks").arg(layer).arg(key).arg(time) }
                        onPathEditCommitted: function(transaction) { status.text = qsTr("Path edit committed · undo steps %1").arg(animation.historyCount) }
                        onPathEditRejected: function(reason) { status.text = reason }
                    }
                    Tk.Caption { text: qsTr("Drag a key or a tangent handle of the selected key: each gesture is one AutomationModel transaction."); color: Tk.Theme.color.textMuted; wrapMode: Text.WordWrap }
                    Tk.Caption { objectName: "motionFixtureState"; text: root.setupStatus + qsTr(" · %1 samples").arg(pathControl ? pathControl.pathPoints.length : 0); color: Tk.Theme.color.accent }
                }
            }
            Tk.Panel {
                title: qsTr("Adjacent frame preview")
                iconName: "layers-2"
                padding: Tk.Theme.space.md
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Tk.OnionSkinView {
                        id: onion
                        Component.onCompleted: root.onionControl = onion
                        width: parent.width; height: parent.height
                        Tk.Flex.grow: 1
                        renderer: renderer
                        currentTicks: root.ticks
                        previousTicks: "0"
                        nextTicks: "282240000"

                    }
                    Tk.Caption { id: status; text: root.setupStatus + " · " + (root.rendered.ok ? qsTr("%1 layers · current %2 ticks").arg(root.rendered.drawnLayers).arg(root.ticks) : String(root.rendered.error || "Waiting for renderer")) + (onion.lastError.length ? " · " + onion.lastError : ""); color: Tk.Theme.color.accent; wrapMode: Text.WordWrap }
                    Tk.Caption { text: qsTr("Neighbor composites are tinted for display and are not written to the composition."); color: Tk.Theme.color.textMuted; wrapMode: Text.WordWrap }
                }
            }
        }
    }
}
