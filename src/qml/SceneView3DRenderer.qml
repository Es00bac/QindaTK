// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D as Q3D
import QindaTK as Tk

Q3D.View3D {
    id: viewport
    objectName: "sceneRenderer3D"
    property var control: null
    environment: Q3D.SceneEnvironment {
        backgroundMode: Q3D.SceneEnvironment.Color
        clearColor: Tk.Theme.color.canvas
    }
    camera: Q3D.PerspectiveCamera {
        id: sceneCamera
        position: viewport.control ? viewport.control.cameraPosition : Qt.vector3d(0, 0, 0)
        eulerRotation: viewport.control ? viewport.control.cameraEulerDegrees : Qt.vector3d(0, 0, 0)
    }
    Q3D.DirectionalLight { eulerRotation: Qt.vector3d(-30, -35, 0) }
    Q3D.Repeater3D {
        model: viewport.control && viewport.control.scene ? viewport.control.scene.entities : []
        delegate: Q3D.Model {
            id: sceneEntity
            required property var modelData
            readonly property string entityId: modelData.entityId
            objectName: "sceneEntity3D_" + entityId
            source: modelData.primitive === "sphere" ? "#Sphere" : modelData.primitive === "cylinder" ? "#Cylinder" : "#Cube"
            position: modelData.worldPosition
            rotation: modelData.worldRotation
            scale: Qt.vector3d(modelData.worldScale, modelData.worldScale, modelData.worldScale)
            visible: modelData.visible
            pickable: true
            materials: Q3D.PrincipledMaterial {
                baseColor: sceneEntity.modelData.color
                roughness: 0.6
                emissiveFactor: viewport.control && viewport.control.scene && viewport.control.scene.selectedId === sceneEntity.entityId
                    ? Qt.vector3d(Tk.Theme.color.accentSubtle.r, Tk.Theme.color.accentSubtle.g, Tk.Theme.color.accentSubtle.b)
                    : Qt.vector3d(0, 0, 0)
            }
        }
    }
    Q3D.Model {
        id: meshEntity
        objectName: "sceneMeshRig3D"
        readonly property string entityId: "__mesh_document__"
        geometry: Tk.MeshRigGeometry {
            id: meshGeometry
            document: viewport.control ? viewport.control.meshDocument : null
        }
        visible: meshGeometry.triangleCount > 0
        pickable: true
        materials: Q3D.PrincipledMaterial {
            roughness: 0.8
            cullMode: Q3D.Material.NoCulling
            baseColorMap: Q3D.Texture {
                textureData: meshGeometry.textureData
            }
        }
    }
    // AGENT-CONTRACT: the viewport maps pointer motion into the chosen transform
    // mode; SceneModel3D owns the transaction, constraints and parent conversion.
    MouseArea {
        id: gesture
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        property bool dragging: false
        property var gestureScene: null
        function cancel() {
            if (dragging && gestureScene && gestureScene.editing) gestureScene.cancelTransform()
            dragging = false
            gestureScene = null
        }
        Component.onDestruction: cancel()
        Connections {
            target: viewport.control
            function onSceneChanged() { gesture.cancel() }
        }
        property real depth: 0
        property vector3d start: Qt.vector3d(0, 0, 0)
        property real startX: 0
        property real startY: 0
        property string mode: "translate"
        property string axis: "free"
        property string space: "world"
        onPressed: function(mouse) {
            if (!viewport.control || (!viewport.control.scene && !viewport.control.meshDocument)) return
            const scene = viewport.control.scene
            gestureScene = scene
            viewport.control.forceActiveFocus()
            const hit = viewport.pick(mouse.x, mouse.y)
            const id = hit.objectHit ? hit.objectHit.entityId : ""
            if (id === meshEntity.entityId) {
                viewport.control.meshPicked(hit.scenePosition)
                return
            }
            if (!scene) return
            scene.select(id)
            if (!id.length) return
            viewport.control.entityPicked(id, hit.scenePosition)
            depth = viewport.mapFrom3DScene(hit.scenePosition).z
            start = viewport.mapTo3DScene(Qt.vector3d(mouse.x, mouse.y, depth))
            startX = mouse.x
            startY = mouse.y
            mode = viewport.control.transformMode
            axis = viewport.control.transformConstraint
            space = viewport.control.transformSpace
            dragging = scene.beginTransform(id)
        }
        onPositionChanged: function(mouse) {
            if (!dragging || !gestureScene || !gestureScene.editing) return
            const point = viewport.mapTo3DScene(Qt.vector3d(mouse.x, mouse.y, depth))
            if (mode === "rotate") {
                gestureScene.previewRotation((mouse.x - startX) * 0.5, axis.length === 1 ? axis : "z",
                                             space, viewport.control.rotationSnap)
            } else if (mode === "scale") {
                gestureScene.previewUniformScale(Math.exp((mouse.x - startX - mouse.y + startY) * 0.01),
                                                 viewport.control.scaleSnap)
            } else {
                let delta = point.minus(start)
                // A Z axis viewed head-on has no screen projection; vertical
                // motion supplies a depth gesture at the picked pixel scale.
                if (axis === "z") {
                    const up = viewport.mapTo3DScene(Qt.vector3d(startX, startY - 1, depth))
                    const perPixel = up.minus(start).length()
                    gestureScene.previewAxisTranslation((startY - mouse.y) * perPixel,
                                                        "z", space, viewport.control.translationSnap)
                } else {
                    gestureScene.previewConstrainedTranslation(delta, axis, space,
                                                               viewport.control.translationSnap)
                }
            }
        }
        onReleased: {
            if (dragging && gestureScene && gestureScene.editing) gestureScene.commitTransform()
            dragging = false
            gestureScene = null
        }
        onCanceled: cancel()
    }
}
