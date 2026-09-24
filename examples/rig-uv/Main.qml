// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: fixture
    width: 1120
    height: 760
    property var bones: [
        { boneId: "root", name: "Root", head: Qt.vector3d(0, -0.8, 0), tail: Qt.vector3d(0, 0, 0), pose: Qt.vector3d(0, 0, 0) },
        { boneId: "arm", name: "Arm", parentId: "root", head: Qt.vector3d(0, 0, 0), tail: Qt.vector3d(0.85, 0.55, 0), pose: Qt.vector3d(0, 0, 18) },
        { boneId: "hand", name: "Hand", parentId: "arm", head: Qt.vector3d(0.85, 0.55, 0), tail: Qt.vector3d(1.15, 0.9, 0), pose: Qt.vector3d(0, 0, 0) }
    ]
    property var vertices: [
        { vertexId: "a", position: Qt.vector3d(-1, -1, 0), weights: ({ root: 1 }) },
        { vertexId: "b", position: Qt.vector3d(1, -1, 0), weights: ({ arm: 0.7, hand: 0.3 }) },
        { vertexId: "c", position: Qt.vector3d(1, 1, 0), weights: ({ arm: 0.5, hand: 0.5 }) },
        { vertexId: "d", position: Qt.vector3d(-1, 1, 0), weights: ({ root: 0.6, arm: 0.4 }) }
    ]
    property var faces: [
        { faceId: "face-east", vertexIds: ["a", "b", "c"], uvIds: ["uv-a", "uv-b", "uv-c"],
          uv: [Qt.point(0.05, 0.05), Qt.point(0.95, 0.05), Qt.point(0.95, 0.95)], seams: [false, false, true] },
        { faceId: "face-west", vertexIds: ["a", "c", "d"], uvIds: ["uv-a2", "uv-c2", "uv-d"],
          uv: [Qt.point(0.05, 0.05), Qt.point(0.95, 0.95), Qt.point(0.05, 0.95)], seams: [true, false, false] }
    ]
    property var islands: [{ faceIds: ["face-east"] }, { faceIds: ["face-west"] }]
    property string selectedBone: "arm"
    property string selectedVertex: "b"
    property string selectedFace: "face-east"
    Rectangle { anchors.fill: parent; color: Tk.Theme.color.bg }
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Heading { text: qsTr("Mesh authoring surfaces"); level: 2 }
            Tk.Badge { text: qsTr("MODEL SNAPSHOT FIXTURE"); variant: "accent" }
        }
        Tk.Label {
            text: qsTr("These editors display host snapshots and send stable-ID requests. The skeletal view is a 2D projection; arbitrary mesh rendering and camera picking remain host responsibilities.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.RigEditor {
            bones: fixture.bones
            vertices: fixture.vertices
            selectedBoneId: fixture.selectedBone
            selectedVertexId: fixture.selectedVertex
            Tk.Flex.flex: 1
            onBoneSelected: function(boneId) { fixture.selectedBone = boneId }
            onVertexSelected: function(vertexId) { fixture.selectedVertex = vertexId }
            onBonePoseRequested: function(boneId, euler) { console.log("Pose request", boneId, euler) }
            onVertexWeightsRequested: function(vertexId, weights) { console.log("Weight request", vertexId, weights) }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Flex.flex: 1
            Tk.UVEditor {
                faces: fixture.faces
                vertices: fixture.vertices
                uvIslands: fixture.islands
                selectedFaceId: fixture.selectedFace
                Tk.Flex.flex: 1
                onFaceSelected: function(faceId) { fixture.selectedFace = faceId }
                onUvCornerMoveRequested: function(faceId, corner, uv) { console.log("UV request", faceId, corner, uv) }
                onUvIslandTranslateRequested: function(faceId, delta) { console.log("Island request", faceId, delta) }
                onSeamToggleRequested: function(vertexA, vertexB) { console.log("Seam request", vertexA, vertexB) }
            }
            Tk.TexturePaintView {
                textureDataUrl: Qt.resolvedUrl("texture.svg").toString()
                Tk.Flex.flex: 1
            }
        }
    }
}
