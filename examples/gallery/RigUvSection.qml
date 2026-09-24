// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    id: section
    title: "Rig, UV and texture"
    note: "RigEditor · UVEditor · TexturePaintView · stable-ID host requests"
    property string selectedBone: "arm"
    property string selectedVertex: "vertex-2"
    property string selectedFace: "face-1"
    property var bones: [
        {boneId: "root", name: "Root", head: Qt.vector3d(0, -0.8, 0), tail: Qt.vector3d(0, 0, 0), pose: Qt.vector3d(0, 0, 0)},
        {boneId: "arm", name: "Arm", parentId: "root", head: Qt.vector3d(0, 0, 0), tail: Qt.vector3d(0.8, 0.5, 0), pose: Qt.vector3d(0, 0, 18)}
    ]
    property var vertices: [
        {vertexId: "vertex-1", position: Qt.vector3d(-1, -1, 0), weights: ({root: 1})},
        {vertexId: "vertex-2", position: Qt.vector3d(1, -1, 0), weights: ({root: 0.4, arm: 0.6})},
        {vertexId: "vertex-3", position: Qt.vector3d(0, 1, 0), weights: ({root: 0.2, arm: 0.8})}
    ]
    property var faces: [{faceId: "face-1", vertexIds: ["vertex-1", "vertex-2", "vertex-3"], uvIds: ["uv-1", "uv-2", "uv-3"],
        uv: [Qt.point(0.1, 0.1), Qt.point(0.9, 0.1), Qt.point(0.5, 0.9)], seams: [false, true, false]}]
    property var islands: [{faceIds: ["face-1"]}]

    GalleryRow {
        label: "Pose / weights"
        Item {
            implicitWidth: 620
            implicitHeight: rig.implicitHeight
            Tk.RigEditor {
                id: rig
                anchors.fill: parent
                bones: section.bones
                vertices: section.vertices
                selectedBoneId: section.selectedBone
                selectedVertexId: section.selectedVertex
                onBoneSelected: function(id) { section.selectedBone = id }
                onVertexSelected: function(id) { section.selectedVertex = id }
                onBonePoseRequested: function(id, pose) { console.log("Gallery pose request", id, pose) }
                onVertexWeightsRequested: function(id, weights) { console.log("Gallery weight request", id, weights) }
            }
        }
    }
    GalleryRow {
        label: "UV / texture"
        Item {
            implicitWidth: 480
            implicitHeight: uv.implicitHeight
            Tk.UVEditor {
                id: uv
                anchors.fill: parent
                faces: section.faces
                vertices: section.vertices
                uvIslands: section.islands
                selectedFaceId: section.selectedFace
                onFaceSelected: function(id) { section.selectedFace = id }
                onUvCornerMoveRequested: function(id, corner, point) { console.log("Gallery UV request", id, corner, point) }
                onUvIslandTranslateRequested: function(id, delta) { console.log("Gallery island request", id, delta) }
                onSeamToggleRequested: function(a, b) { console.log("Gallery seam request", a, b) }
            }
        }
        Item {
            implicitWidth: 400
            implicitHeight: paint.implicitHeight
            Tk.TexturePaintView {
                id: paint
                anchors.fill: parent
                textureDataUrl: Qt.resolvedUrl("../rig-uv/texture.svg").toString()
            }
        }
    }
    Tk.Caption { text: qsTr("Gallery snapshots are illustrative. Pose, UV, weight and camera-projected paint requests require a host model."); muted: true; wrapMode: Text.Wrap }
}
