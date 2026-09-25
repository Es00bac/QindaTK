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

    Component {
        id: liveComponent
        Tk.MeshRigDocument {}
    }

    SignalSpy { id: constraintSpy }
    SignalSpy { id: keySpy }
    SignalSpy { id: stitchSpy }
    SignalSpy { id: channelSpy }
    SignalSpy { id: strokeSpy }
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

    function test_rig_requests_constraint_and_key_with_stable_ids() {
        const rig = createTemporaryObject(rigComponent, this)
        verify(rig !== null)
        constraintSpy.target = rig; constraintSpy.signalName = "constraintAddRequested"
        keySpy.target = rig; keySpy.signalName = "poseKeyRequested"
        rig.selectedBoneId = "other-id"
        rig.currentFrame = 12
        compare(rig.constraintTargets.length, 1)
        compare(rig.constraintTargets[0].boneId, "root-id")
        verify(!rig.requestCopyRotation("other-id"))
        verify(rig.requestCopyRotation("root-id"))
        compare(constraintSpy.count, 1)
        compare(constraintSpy.signalArguments[0][0], "other-id")
        compare(constraintSpy.signalArguments[0][1], "copyRotation")
        compare(constraintSpy.signalArguments[0][2], "root-id")
        compare(constraintSpy.signalArguments[0][3], 1)
        verify(!rig.hasKeyAtFrame())
        verify(rig.requestKey())
        compare(keySpy.count, 1)
        compare(keySpy.signalArguments[0][0], "other-id")
        compare(keySpy.signalArguments[0][1], 12)
    }

    function test_uv_stitch_mode_emits_seam_edge() {
        const editor = createTemporaryObject(uvComponent, this, {
            faces: [{ faceId: "face-stable", vertexIds: ["v0", "v1", "v2"], uvIds: ["u0", "u1", "u2"],
                      uv: [Qt.point(0.1, 0.1), Qt.point(0.9, 0.1), Qt.point(0.5, 0.9)], seams: [true, false, false] }]
        })
        verify(editor !== null)
        stitchSpy.target = editor; stitchSpy.signalName = "seamStitchRequested"
        editor.seamMode = true
        editor.stitchMode = true
        // Just above the bottom edge v0-v1, which is the seam.
        editor.selectAt(editor.uvToPoint(Qt.point(0.5, 0.12)))
        compare(stitchSpy.count, 1)
        compare(stitchSpy.signalArguments[0][0], "v0")
        compare(stitchSpy.signalArguments[0][1], "v1")
        // A non-seam edge is not stitched.
        editor.selectAt(editor.uvToPoint(Qt.point(0.72, 0.45)))
        compare(stitchSpy.count, 1)
    }

    function test_texture_channel_is_named_by_stroke() {
        const paint = createTemporaryObject(paintComponent, this)
        verify(paint !== null)
        channelSpy.target = paint; channelSpy.signalName = "channelSelected"
        strokeSpy.target = paint; strokeSpy.signalName = "strokeBegan"
        verify(!paint.selectChannel("specular"))
        verify(paint.selectChannel("roughness"))
        compare(channelSpy.count, 1)
        compare(paint.channel, "roughness")
        verify(paint.beginAt(Qt.point(10, 10)))
        compare(strokeSpy.count, 1)
        compare(strokeSpy.signalArguments[0][0], "roughness")
    }

    function test_live_document_paints_through_the_cpu_camera() {
        const mesh = createTemporaryObject(liveComponent, this)
        verify(mesh !== null)
        const a = mesh.addVertex(Qt.vector3d(0, 0, 0)), b = mesh.addVertex(Qt.vector3d(16, 0, 0))
        const c = mesh.addVertex(Qt.vector3d(16, 16, 0)), d = mesh.addVertex(Qt.vector3d(0, 16, 0))
        const face = mesh.addFace(a, b, c)
        verify(mesh.addFace(a, c, d).length > 0)
        verify(mesh.resizeTexture(16, 16))
        const ray = mesh.cameraRay(Qt.vector3d(8, 8, 30), Qt.vector3d(0, 0, 0), 60, Qt.size(100, 100), Qt.point(50, 50))
        compare(mesh.pickFace(ray.origin, ray.direction).faceId.length > 0, true)
        verify(mesh.beginPaint("baseColor"))
        verify(mesh.paintRay(ray.origin, ray.direction, "#ff0000", 1, 1, 1))
        verify(mesh.endPaint())
        verify(mesh.canUndo)
        compare(mesh.textureChannels.length, 3)
        verify(String(mesh.textureDataUrls.roughness).indexOf("data:image/png") === 0)
        verify(face.length > 0)
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
