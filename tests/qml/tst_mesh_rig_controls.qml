// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "MeshRigControls"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Component {
        id: rigComponent
        Tk.RigEditor {
            width: 560; height: 420
            bones: [
                { boneId: "root-id", name: "Root", head: Qt.vector3d(0, 0, 0), tail: Qt.vector3d(1, 0, 0), pose: Qt.vector3d(0, 0, 0) },
                { boneId: "other-id", name: "Other", head: Qt.vector3d(1, 0, 0), tail: Qt.vector3d(2, 0, 0), pose: Qt.vector3d(0, 0, 0) }
            ]
            vertices: [{ vertexId: "v0", weights: ({ "root-id": 0.4, "other-id": 0.6 }) }]
        }
    }
    Component {
        id: uvComponent
        Tk.UVEditor {
            width: 400; height: 380
            faces: [{ faceId: "face-stable", vertexIds: ["v0", "v1", "v2"], uvIds: ["u0", "u1", "u2"],
                     uv: [Qt.point(0.1, 0.1), Qt.point(0.9, 0.1), Qt.point(0.5, 0.9)], seams: [false, false, false] }]
            uvIslands: [{ faceIds: ["face-stable"] }]
        }
    }
    Component {
        id: paintComponent
        Tk.TexturePaintView {
            width: 400; height: 380
            rayProvider: function(point) {
                return { origin: Qt.vector3d(point.x, point.y, 5), direction: Qt.vector3d(0, 0, -1) }
            }
        }
    }

    SignalSpy { id: boneSpy }
    SignalSpy { id: poseSpy }
    SignalSpy { id: weightsSpy }
    SignalSpy { id: faceSpy }
    SignalSpy { id: raySpy }

    function test_rig_selection_and_pose_keep_bone_id() {
        const rig = createTemporaryObject(rigComponent, this)
        verify(rig !== null)
        boneSpy.target = rig; boneSpy.signalName = "boneSelected"
        poseSpy.target = rig; poseSpy.signalName = "bonePoseRequested"
        weightsSpy.target = rig; weightsSpy.signalName = "vertexWeightsRequested"
        rig.selectBone("root-id")
        compare(boneSpy.count, 1)
        compare(boneSpy.signalArguments[0][0], "root-id")
        rig.selectedBoneId = "root-id" // host echoes the selected stable ID
        rig.setPoseAxis(2, 22)
        compare(poseSpy.count, 1)
        compare(poseSpy.signalArguments[0][0], "root-id")
        compare(poseSpy.signalArguments[0][1].z, 22)
        rig.selectedVertexId = "v0" // host echoes vertex selection
        rig.requestBoneWeight(0.25)
        compare(weightsSpy.count, 1)
        compare(weightsSpy.signalArguments[0][0], "v0")
        compare(weightsSpy.signalArguments[0][1]["root-id"], 0.25)
        compare(weightsSpy.signalArguments[0][1]["other-id"], 0.75)
    }

    function test_uv_hit_selection_uses_stable_face_id() {
        const editor = createTemporaryObject(uvComponent, this)
        verify(editor !== null)
        faceSpy.target = editor; faceSpy.signalName = "faceSelected"
        const point = editor.uvToPoint(Qt.point(0.5, 0.4))
        compare(editor.faceAt(point).faceId, "face-stable")
        editor.selectAt(point)
        compare(faceSpy.count, 1)
        compare(faceSpy.signalArguments[0][0], "face-stable")
        editor.selectedFaceId = faceSpy.signalArguments[0][0] // host publishes selection
    }

    function test_texture_input_emits_host_camera_ray() {
        const paint = createTemporaryObject(paintComponent, this)
        verify(paint !== null)
        raySpy.target = paint; raySpy.signalName = "paintRayRequested"
        verify(paint.paintingAvailable)
        verify(paint.sendRay(Qt.point(40, 60)))
        compare(raySpy.count, 1)
        compare(raySpy.signalArguments[0][0].z, 5)
        compare(raySpy.signalArguments[0][1].z, -1)
    }
}
