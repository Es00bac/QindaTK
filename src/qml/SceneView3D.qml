// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    id: control
    objectName: "sceneView3D"
    property Tk.SceneModel3D scene: null
    property Tk.MeshRigDocument meshDocument: null
    property string tooltip: qsTr("3D scene: drag objects to translate; arrows move selection; Escape cancels")
    property vector3d cameraPosition: Qt.vector3d(0, 120, 650)
    property vector3d cameraEulerDegrees: Qt.vector3d(-10, 0, 0)
    property real translationSnap: 0
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
