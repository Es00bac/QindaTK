// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 1600
    height: 900
    color: Tk.Theme.color.bg
    property string boneId: ""
    property string vertexId: ""
    property string faceId: ""

    function seamState(a, b) {
        for (const face of mesh.faces) {
            for (let i = 0; i < 3; ++i) {
                const first = String(face.vertexIds[i])
                const second = String(face.vertexIds[(i + 1) % 3])
                if ((first === a && second === b) || (first === b && second === a))
                    return Boolean(face.seams[i])
            }
        }
        return false
    }

    Tk.MeshRigDocument {
        id: mesh
        Component.onCompleted: {
            const a = addVertex(Qt.vector3d(-105, -105, 0))
            const b = addVertex(Qt.vector3d(105, -105, 0))
            const c = addVertex(Qt.vector3d(105, 105, 0))
            const d = addVertex(Qt.vector3d(-105, 105, 0))
            root.vertexId = c
            const first = addFace(a, b, c)
            const second = addFace(a, c, d)
            root.faceId = first
            setUV(first, 0, Qt.point(0, 0))
            setUV(first, 1, Qt.point(1, 0))
            setUV(first, 2, Qt.point(1, 1))
            setUV(second, 0, Qt.point(0, 0))
            setUV(second, 1, Qt.point(1, 1))
            setUV(second, 2, Qt.point(0, 1))
            setSeam(a, c, true)
            resizeTexture(256, 256)
            beginPaint()
            paintRay(Qt.vector3d(-45, 35, 100), Qt.vector3d(0, 0, -1), "#e9a358", 24, 1)
            paintRay(Qt.vector3d(35, -25, 100), Qt.vector3d(0, 0, -1), "#3caec2", 30, 1)
            endPaint()
            root.boneId = addBone("Upper panel", "", Qt.vector3d(0, -105, 0),
                                  Qt.vector3d(0, 105, 0))
            const weights = ({})
            weights[root.boneId] = 1
            setVertexWeights(c, weights)
            setVertexWeights(d, weights)
            setBonePose(root.boneId, Qt.vector3d(0, 0, 12))
        }
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Heading { text: qsTr("Mesh, rig, UV and texture workbench"); level: 1 }
        Tk.Caption {
            text: qsTr("One live MeshRigDocument drives the controls and GPU viewport; edits produce actual posed geometry and texture pixels")
        }
        Tk.Flex {
            Tk.Flex.grow: 1
            width: parent.width
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Flex {
                width: Tk.Theme.size.panelMinWidth * 2.8
                height: parent.height
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.RigEditor {
                    objectName: "liveRigEditor"
                    width: parent.width
                    Tk.Flex.grow: 1
                    bones: mesh.bones
                    vertices: mesh.vertices
                    selectedBoneId: root.boneId
                    selectedVertexId: root.vertexId
                    onBoneSelected: function(id) { root.boneId = id }
                    onVertexSelected: function(id) { root.vertexId = id }
                    onBonePoseRequested: function(id, pose) { mesh.setBonePose(id, pose) }
                    onVertexWeightsRequested: function(id, weights) {
                        mesh.setVertexWeights(id, weights)
                    }
                    onResetPoseRequested: mesh.resetPose()
                }
                Tk.UVEditor {
                    objectName: "liveUVEditor"
                    width: parent.width
                    Tk.Flex.grow: 1
                    faces: mesh.faces
                    vertices: mesh.vertices
                    uvIslands: mesh.uvIslands
                    selectedFaceId: root.faceId
                    onFaceSelected: function(id) { root.faceId = id }
                    onUvCornerMoveRequested: function(id, corner, uv) {
                        mesh.setUV(id, corner, uv)
                    }
                    onUvIslandTranslateRequested: function(id, delta) {
                        mesh.translateUvIsland(id, delta)
                    }
                    onSeamToggleRequested: function(a, b) {
                        mesh.setSeam(a, b, !root.seamState(a, b))
                    }
                }
            }
            Tk.Flex {
                Tk.Flex.grow: 1
                height: parent.height
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.SceneView3D {
                    id: viewport
                    objectName: "liveMeshViewport"
                    width: parent.width
                    Tk.Flex.grow: 1
                    meshDocument: mesh
                    cameraPosition: Qt.vector3d(0, 80, 520)
                    cameraEulerDegrees: Qt.vector3d(-8, 0, 0)
                }
                Tk.TexturePaintView {
                    objectName: "liveTexturePaintView"
                    width: parent.width
                    height: Tk.Theme.size.panelMinWidth * 1.2
                    textureDataUrl: mesh.textureDataUrl
                    rayProvider: function(position) {
                        return viewport.meshRay(position.x * viewport.width,
                                                position.y * viewport.height)
                    }
                    onStrokeBegan: mesh.beginPaint()
                    onPaintRayRequested: function(origin, direction, color, radius, opacity) {
                        mesh.paintRay(origin, direction, color, radius, opacity)
                    }
                    onStrokeFinished: mesh.endPaint()
                    onStrokeCancelled: mesh.cancelPaint()
                }
            }
        }
        Tk.Caption {
            text: qsTr("%1 triangles · %2 UV islands · %3 bones · %4 texture pixels")
                .arg(mesh.faces.length).arg(mesh.uvIslands.length).arg(mesh.bones.length)
                .arg(mesh.textureWidth * mesh.textureHeight)
        }
    }
}
