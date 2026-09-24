// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    id: control
    objectName: "sceneView3D"
    property Tk.SceneModel3D scene: null
    property Tk.MeshRigDocument meshDocument: null
    property string tooltip: qsTr("3D scene: left drag transforms, right drag orbits, middle drag pans, wheel zooms; Escape cancels")
    property vector3d cameraPosition: Qt.vector3d(0, 120, 650)
    property vector3d cameraEulerDegrees: Qt.vector3d(-10, 0, 0)
    property vector3d cameraTarget: Qt.vector3d(0, 0, 0)
    function orbitCamera(dx, dy) {
        const offset = cameraPosition.minus(cameraTarget)
        const radius = Math.max(1, offset.length())
        const yaw = Math.atan2(offset.x, offset.z) + dx * Math.PI / 600
        const pitch = Math.max(-Math.PI * 0.49, Math.min(Math.PI * 0.49,
            Math.atan2(offset.y, Math.hypot(offset.x, offset.z)) - dy * Math.PI / 600))
        cameraPosition = Qt.vector3d(cameraTarget.x + radius * Math.sin(yaw) * Math.cos(pitch),
                                     cameraTarget.y + radius * Math.sin(pitch),
                                     cameraTarget.z + radius * Math.cos(yaw) * Math.cos(pitch))
        cameraEulerDegrees = Qt.vector3d(-pitch * 180 / Math.PI, yaw * 180 / Math.PI, 0)
    }
    function panCamera(dx, dy) {
        const offset = cameraPosition.minus(cameraTarget)
        const radius = Math.max(1, offset.length())
        const yaw = Math.atan2(offset.x, offset.z)
        const pitch = Math.atan2(offset.y, Math.hypot(offset.x, offset.z))
        const worldPerPixel = 2 * radius * Math.tan(Math.PI / 6) / Math.max(1, contentItem.height)
        const right = Qt.vector3d(Math.cos(yaw), 0, -Math.sin(yaw))
        const up = Qt.vector3d(-Math.sin(yaw) * Math.sin(pitch), Math.cos(pitch),
                               -Math.cos(yaw) * Math.sin(pitch))
        const shift = Qt.vector3d((-dx * right.x + dy * up.x) * worldPerPixel,
                                  (-dx * right.y + dy * up.y) * worldPerPixel,
                                  (-dx * right.z + dy * up.z) * worldPerPixel)
        cameraPosition = cameraPosition.plus(shift)
        cameraTarget = cameraTarget.plus(shift)
    }
    function zoomCamera(steps) {
        const offset = cameraPosition.minus(cameraTarget)
        const distance = Math.max(10, Math.min(1000000, offset.length() * Math.pow(2, -steps / 4)))
        const direction = offset.normalized()
        cameraPosition = cameraTarget.plus(Qt.vector3d(direction.x * distance,
            direction.y * distance, direction.z * distance))
    }
    property real translationSnap: 0
    property real rotationSnap: 0
    property real scaleSnap: 0
    property string transformMode: "translate" // translate, rotate, scale
    property string transformConstraint: "free" // free, x, y, z, xy, xz, yz
    property string transformSpace: "world" // world, local
    readonly property bool rendererAvailable: GraphicsInfo.api !== GraphicsInfo.Software && renderer.status === Loader.Ready
    function projectWorldPosition(position) {
        return renderer.item ? renderer.item.mapFrom3DScene(position) : Qt.vector3d(0, 0, -1)
    }
    function pickEntity(x, y) {
        if (!renderer.item) return ({})
        const hit = renderer.item.pick(x, y)
        return hit.objectHit ? {entityId: hit.objectHit.entityId, worldPosition: hit.scenePosition, distance: hit.distance} : ({})
    }
    function meshRay(x, y) {
        if (!renderer.item || !meshDocument) return ({})
        const nearPoint = renderer.item.mapTo3DScene(Qt.vector3d(x, y, 0))
        const farPoint = renderer.item.mapTo3DScene(Qt.vector3d(x, y, 1))
        return {origin: nearPoint, direction: farPoint.minus(nearPoint).normalized()}
    }
    signal entityPicked(string entityId, vector3d worldPosition)
    signal meshPicked(vector3d worldPosition)
    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.waveformHeight * 8
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Graphic
    Accessible.name: tooltip
    Accessible.description: rendererAvailable ? qsTr("Real 3D scene and mesh rendering") : qsTr("3D requires a Qt RHI graphics backend")
    background: Rectangle { color: Tk.Theme.color.canvas; border.color: Tk.Theme.color.border; border.width: Tk.Theme.size.border }
    contentItem: Item {
        Loader {
            id: renderer
            anchors.fill: parent
            active: control.GraphicsInfo.api !== GraphicsInfo.Software
            source: "SceneView3DRenderer.qml"
            onLoaded: item.control = control
        }
        Tk.Flex {
            id: transformBar
            objectName: "sceneTransformBar3D"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: Tk.Theme.space.sm
            z: 1
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            padding: Tk.Theme.space.xs
            Rectangle {
                anchors.fill: parent
                z: -1
                radius: Tk.Theme.radius.sm
                color: Tk.Theme.color.panel
                border.color: Tk.Theme.color.border
            }
            Repeater {
                model: [{label: qsTr("Move"), value: "translate"},
                        {label: qsTr("Rotate"), value: "rotate"},
                        {label: qsTr("Scale"), value: "scale"}]
                delegate: Tk.Button {
                    required property var modelData
                    objectName: "sceneMode3D_" + modelData.value
                    text: modelData.label
                    small: true
                    checkable: true
                    checked: control.transformMode === modelData.value
                    tooltip: qsTr("Drag the selected object to %1").arg(modelData.label.toLowerCase())
                    onClicked: {
                        control.transformMode = modelData.value
                        if (modelData.value === "rotate" && control.transformConstraint.length !== 1)
                            control.transformConstraint = "z"
                    }
                }
            }
            Repeater {
                model: ["free", "x", "y", "z", "xy", "xz", "yz"]
                delegate: Tk.Button {
                    required property string modelData
                    objectName: "sceneAxis3D_" + modelData
                    text: modelData.toUpperCase()
                    small: true
                    checkable: true
                    checked: control.transformConstraint === modelData
                    available: control.transformMode === "translate"
                        || (control.transformMode === "rotate" && modelData.length === 1)
                    tooltip: qsTr("Constrain movement to %1 in %2 space").arg(modelData).arg(control.transformSpace)
                    onClicked: control.transformConstraint = modelData
                }
            }
            Tk.Button {
                objectName: "sceneSpace3D"
                text: control.transformSpace === "world" ? qsTr("World") : qsTr("Local")
                small: true
                tooltip: qsTr("Toggle world or local transform axes")
                onClicked: control.transformSpace = control.transformSpace === "world" ? "local" : "world"
            }
        }
        Tk.Label {
            anchors.centerIn: parent
            visible: !control.rendererAvailable
            text: renderer.status === Loader.Error ? qsTr("Qt Quick 3D could not be loaded")
                : qsTr("3D rendering requires OpenGL, Vulkan, Metal or Direct3D")
        }
    }
    Keys.onEscapePressed: if (scene && scene.editing) scene.cancelTransform()
    Keys.onPressed: function(event) {
        if (!scene || !scene.selectedId.length || scene.editing) return
        let dx = 0, dy = 0
        const step = translationSnap > 0 ? translationSnap : 1
        if (event.key === Qt.Key_Left) dx = -step
        else if (event.key === Qt.Key_Right) dx = step
        else if (event.key === Qt.Key_Up) dy = step
        else if (event.key === Qt.Key_Down) dy = -step
        else return
        if (scene.beginTransform(scene.selectedId)) {
            scene.previewWorldTranslation(Qt.vector3d(dx, dy, 0), translationSnap)
            scene.commitTransform()
        }
        event.accepted = true
    }
}
