// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-077 audit gaps, driven through the real controls wired to a live
// MeshRigDocument the way examples/mesh-rig-live routes them: slider drags as
// one undo step, UV Rotate/Unwrap/Pack with the pack preview, rig authoring,
// timeline lanes from the WP-073 AutomationModel, camera painting under the
// pointer and 3D <-> editor selection synchronization (no RHI renderer needed).
TestCase {
    id: testCase
    name: "MeshRigLive"
    when: windowShown
    visible: true
    width: 960
    height: 760

    Component {
        id: workbench
        Item {
            id: bench
            width: 960; height: 760
            property alias doc: mesh
            property alias animation: animationModel
            property alias rig: rigEditor
            property alias uv: uvEditor
            property alias painter: paintView
            property alias view: viewport
            property alias timeline: keyTimeline
            property string boneId: ""
            property string faceId: ""
            property string vertexId: ""
            property string bone: ""
            property string first: ""
            property string second: ""
            function build() {
                const a = mesh.addVertex(Qt.vector3d(-105, -105, 0)), b = mesh.addVertex(Qt.vector3d(105, -105, 0))
                const c = mesh.addVertex(Qt.vector3d(105, 105, 0)), d = mesh.addVertex(Qt.vector3d(-105, 105, 0))
                first = mesh.addFace(a, b, c); second = mesh.addFace(a, c, d)
                mesh.setUV(first, 0, Qt.point(0, 0)); mesh.setUV(first, 1, Qt.point(1, 0)); mesh.setUV(first, 2, Qt.point(1, 1))
                mesh.setUV(second, 0, Qt.point(0, 0)); mesh.setUV(second, 1, Qt.point(1, 1)); mesh.setUV(second, 2, Qt.point(0, 1))
                mesh.setSeam(a, c, true)
                mesh.resizeTexture(64, 64)
                bone = mesh.addBone("Panel", "", Qt.vector3d(-80, -100, 0), Qt.vector3d(-80, 60, 0))
                const weights = ({}); weights[bone] = 1
                mesh.setVertexWeights(c, weights)
                vertexId = c
            }
            Tk.AutomationModel { id: animationModel }
            Tk.MeshRigDocument { id: mesh; animationModel: animationModel }
            Tk.RigEditor {
                id: rigEditor
                x: 0; y: 0; width: 560; height: 420
                bones: mesh.bones
                vertices: mesh.vertices
                selectedBoneId: bench.boneId
                selectedVertexId: bench.vertexId
                currentFrame: mesh.currentFrame
                onBoneSelected: function(id) { bench.boneId = id }
                onBonePoseRequested: function(id, pose) { mesh.setBonePose(id, pose) }
                onEditGestureStarted: mesh.beginEditGesture()
                onEditGestureFinished: mesh.endEditGesture()
                onBoneAddRequested: function(name, parentId, head, tail) {
                    const id = mesh.addBone(name, parentId, head, tail)
                    if (id.length) bench.boneId = id
                }
                onBoneRemoveRequested: function(id) { mesh.removeBone(id) }
                onBoneRenameRequested: function(id, name) { mesh.renameBone(id, name) }
                onBoneParentRequested: function(id, parentId) { mesh.setBoneParent(id, parentId) }
                onBoneRestRequested: function(id, head, tail) { mesh.setBoneRest(id, head, tail) }
                onBoneLimitsRequested: function(id, minimum, maximum) { mesh.setBoneLimits(id, minimum, maximum) }
            }
            Tk.UVEditor {
                id: uvEditor
                x: 560; y: 0; width: 400; height: 420
                faces: mesh.faces
                vertices: mesh.vertices
                uvIslands: mesh.uvIslands
                selectedFaceId: bench.faceId
                packPreview: uvEditor.packHovered ? mesh.packUvLayout(uvEditor.packMargin) : []
                onFaceSelected: function(id) { bench.faceId = id }
                onUvIslandTransformRequested: function(id, move, degrees, scale) { mesh.transformUvIsland(id, move, degrees, scale) }
                onUvUnwrapRequested: function(id) { mesh.unwrapUvIsland(id) }
                onUvPackRequested: function(margin) { mesh.packUvIslands(margin) }
            }
            Tk.SceneView3D {
                id: viewport
                x: 0; y: 420; width: 400; height: 280
                meshDocument: mesh
                selectedMeshFaceId: bench.faceId
                selectedBoneId: bench.boneId
                onMeshFacePicked: function(id, uv) { bench.faceId = id }
                onMeshBonePicked: function(id) { bench.boneId = id }
                onMeshStrokeBegan: mesh.beginPaint(paintView.channel)
                onMeshPaintRayRequested: function(origin, direction) {
                    mesh.paintRay(origin, direction, paintView.brushColor, paintView.brushRadius,
                                  paintView.brushOpacity, paintView.brushHardness)
                }
                onMeshStrokeFinished: mesh.endPaint()
                onMeshStrokeCancelled: mesh.cancelPaint()
            }
            Tk.TexturePaintView {
                id: paintView
                x: 400; y: 420; width: 360; height: 280
                paintEnabled: true
                brushColor: "#ff0000"
                brushRadius: 2
                textureWidth: mesh.textureWidth
                textureHeight: mesh.textureHeight
                onStrokeBegan: function(channel) { mesh.beginPaint(channel) }
                onPaintUvRequested: function(uv, color, radius, opacity, hardness) { mesh.paintUv(uv, color, radius, opacity, hardness) }
                onStrokeFinished: mesh.endPaint()
                onStrokeCancelled: mesh.cancelPaint()
            }
            Tk.KeyframeTimeline {
                id: keyTimeline
                x: 760; y: 420; width: 200; height: 280
                model: animationModel
                timeBoundaries: ["0", "12", "24"]
            }
        }
    }

    SignalSpy { id: requestSpy }
    SignalSpy { id: secondSpy }

    function makeBench() {
        const bench = createTemporaryObject(workbench, testCase)
        verify(bench !== null)
        bench.build()
        waitForRendering(bench) // Flex rows are laid out (polished) before input
        return bench
    }
    // Retarget a spy without connecting the old signal name to the new target.
    function watch(spy, target, name) {
        spy.target = null
        spy.signalName = name
        spy.target = target
        spy.clear()
    }
    function texelOf(bench, uv) {
        return Qt.point(Math.floor(uv.x * bench.doc.textureWidth), Math.floor((1 - uv.y) * bench.doc.textureHeight))
    }
    function uvText(face) { return face.uv.map(p => p.x.toFixed(6) + "," + p.y.toFixed(6)).join(";") }
    function isRed(color) { return color.r > 0.99 && color.g < 0.01 && color.b < 0.01 }

    function test_rig_slider_drag_is_one_undo_step() {
        const bench = makeBench()
        bench.boneId = bench.bone
        waitForRendering(bench.rig) // the pose rows appear with the selection
        const slider = findChild(bench.rig, "rigPoseAxis_Z")
        verify(slider !== null)
        tryVerify(() => slider.visible && slider.width > 60)
        const y = slider.height / 2
        mousePress(slider, slider.width / 2, y)
        verify(bench.doc.editGestureActive)
        for (let i = 1; i <= 20; ++i) mouseMove(slider, slider.width / 2 + i * 3, y)
        mouseRelease(slider, slider.width / 2 + 60, y)
        verify(!bench.doc.editGestureActive)
        const posed = bench.doc.evaluatedPose(bench.bone).z
        verify(posed > 20, "the drag posed the bone: " + posed)
        // One undo removes the whole drag, not its last tick.
        verify(bench.doc.undo())
        compare(bench.doc.evaluatedPose(bench.bone).z, 0)
        const weights = bench.doc.vertices[2].weights
        compare(weights[bench.bone], 1)
        verify(bench.doc.undo()) // the edit before the drag is still reachable
        verify(bench.doc.vertices[2].weights[bench.bone] === undefined)
    }

    function test_uv_rotate_unwrap_pack_and_preview() {
        const bench = makeBench()
        bench.faceId = bench.first
        watch(requestSpy, bench.uv, "uvIslandTransformRequested")
        const before = uvText(bench.doc.faces[0])
        mouseClick(findChild(bench.uv, "uvRotate"))
        compare(requestSpy.count, 1)
        compare(requestSpy.signalArguments[0][0], bench.first)
        compare(requestSpy.signalArguments[0][2], 90)
        verify(uvText(bench.doc.faces[0]) !== before, "rotated " + uvText(bench.doc.faces[0]))
        compare(bench.doc.faces[0].faceId, bench.first)
        watch(secondSpy, bench.uv, "uvUnwrapRequested")
        const revision = bench.doc.revision
        mouseClick(findChild(bench.uv, "uvUnwrap"))
        compare(secondSpy.count, 1)
        compare(secondSpy.signalArguments[0][0], bench.first)
        verify(bench.doc.revision !== revision)
        // Hovering Pack previews the packed bounds; clicking packs.
        const pack = findChild(bench.uv, "uvPack")
        const surface = findChild(bench.uv, "uvSurface")
        compare(bench.uv.packPreview.length, 0)
        mouseMove(pack, pack.width / 2, pack.height / 2)
        tryVerify(() => bench.uv.packHovered)
        compare(bench.uv.packPreview.length, bench.doc.uvIslands.length)
        const r = bench.uv.packPreview[0].bounds
        const corner = bench.uv.uvToPoint(Qt.point(r.x, r.y + r.height))
        const far = bench.uv.uvToPoint(Qt.point(r.x + r.width, r.y + r.height))
        // Canvas repaints a frame or two after packPreview changes. The
        // software backend leaves Canvas out of an item grab, so grab the
        // whole bench and read at the surface's position in it.
        const origin = surface.mapToItem(bench, 0, 0)
        const dashes = function() {
            const image = grabImage(bench)
            let count = 0
            for (let x = Math.ceil(corner.x); x < Math.floor(far.x); ++x) {
                for (let dy = -1; dy <= 1; ++dy) {
                    const c = image.pixel(Math.round(origin.x) + x, Math.round(origin.y + corner.y) + dy)
                    if (Math.abs(c.r - Tk.Theme.color.warning.r) < 0.12 && Math.abs(c.g - Tk.Theme.color.warning.g) < 0.12
                            && Math.abs(c.b - Tk.Theme.color.warning.b) < 0.12) { ++count; break }
                }
            }
            return count
        }
        tryVerify(() => dashes() > 3, 3000, "pack preview outline drawn")
        watch(requestSpy, bench.uv, "uvPackRequested")
        mouseClick(pack)
        compare(requestSpy.count, 1)
        fuzzyCompare(requestSpy.signalArguments[0][0], bench.uv.packMargin, 1e-9)
        verify(bench.doc.canUndo)
        compare(bench.doc.faces.length, 2)
    }

    function test_rig_authoring_mode_edits_the_rig() {
        const bench = makeBench()
        bench.boneId = bench.bone
        bench.rig.mode = "author"
        waitForRendering(bench.rig)
        const add = findChild(bench.rig, "rigAddBone")
        tryVerify(() => add.visible && add.width > 0)
        mouseClick(add)
        compare(bench.doc.bones.length, 2)
        const child = bench.boneId
        verify(child !== bench.bone)
        const created = bench.rig.boneById(child)
        compare(created.parentId, bench.bone)
        compare(created.head, Qt.vector3d(-80, 60, 0))
        findChild(bench.rig, "rigAuthor_minZ").valueModified(-45)
        compare(bench.rig.boneById(child).minimum.z, -45)
        findChild(bench.rig, "rigAuthor_tailX").valueModified(-60)
        compare(bench.rig.boneById(child).tail.x, -60)
        // The parent picker never offers the bone's own descendants.
        bench.boneId = bench.bone
        verify(!bench.rig.boneById(bench.bone).parentId)
        const candidates = findChild(bench.rig, "rigAuthoringPanel").parentCandidates.map(b => b.boneId)
        verify(candidates.indexOf(child) < 0)
        bench.boneId = child
        mouseClick(findChild(bench.rig, "rigRemoveBone"))
        compare(bench.doc.bones.length, 1)
        verify(bench.doc.undo())
        compare(bench.doc.bones.length, 2)
    }

    function test_rig_keys_show_in_the_keyframe_timeline() {
        const bench = makeBench()
        verify(bench.doc.bindBoneAnimation(bench.bone))
        compare(bench.timeline.parameterIds.length, 3)
        compare(bench.timeline.parameterIds, bench.doc.boneAnimationParameters(bench.bone))
        bench.doc.currentFrame = 12
        verify(bench.doc.setBonePose(bench.bone, Qt.vector3d(0, 0, 30)))
        const lane = bench.doc.boneAnimationParameters(bench.bone)[2]
        compare(bench.animation.points(lane).length, 1)
        compare(bench.animation.points(lane)[0].ticks, "12")
        // Editing the key in the timeline's model moves the evaluated rig.
        verify(bench.animation.applyEdit({ kind: "setPoint", parameterId: lane, pointId: bench.animation.points(lane)[0].id },
                                         { value: 50 }).ok)
        compare(bench.doc.evaluatedPose(bench.bone).z, 50)
    }

    function test_scene_view_paints_the_texel_under_the_pointer() {
        const bench = makeBench()
        const view = bench.view
        view.meshTool = "paint"
        const points = [Qt.point(view.width / 2 + 10, view.height / 2), Qt.point(view.width / 2 + 30, view.height / 2 + 6)]
        const hits = points.map(p => {
            const ray = view.meshRay(p.x, p.y)
            return bench.doc.pickFace(ray.origin, ray.direction)
        })
        verify(hits[0].faceId && hits[1].faceId)
        for (const hit of hits) verify(!isRed(bench.doc.texel("baseColor", texelOf(bench, hit.uv).x, texelOf(bench, hit.uv).y)))
        mousePress(view, points[0].x, points[0].y)
        verify(bench.doc.painting)
        mouseMove(view, points[1].x, points[1].y)
        mouseRelease(view, points[1].x, points[1].y)
        verify(!bench.doc.painting)
        verify(bench.doc.canUndo)
        for (const hit of hits) {
            const t = texelOf(bench, hit.uv)
            verify(isRed(bench.doc.texel("baseColor", t.x, t.y)), "painted under the pointer at " + t.x + "," + t.y)
        }
        // The texture surface paints the texel it shows, too.
        const paint = bench.painter
        paint.selectChannel("roughness")
        const r = paint.textureRect
        const surface = findChild(paint, "texturePaintSurface")
        mouseClick(surface, r.x + r.width * 0.75, r.y + r.height * 0.5)
        const uv = paint.uvAt(Qt.point(r.x + r.width * 0.75, r.y + r.height * 0.5))
        const t = texelOf(bench, uv)
        const scalar = bench.doc.texel("roughness", t.x, t.y)
        fuzzyCompare(scalar.r, 87 / 255, 0.01) // "#ff0000" in a scalar channel is qGray: 87
    }

    function test_scene_view_selection_syncs_both_ways() {
        const bench = makeBench()
        const view = bench.view
        view.meshTool = "select"
        const layer = findChild(view, "sceneMeshLayer3D")
        compare(layer.overlay.face.length, 0)
        watch(requestSpy, view, "meshFacePicked")
        const x = view.width / 2 + 20, y = view.height / 2
        const ray = view.meshRay(x, y)
        const expected = bench.doc.pickFace(ray.origin, ray.direction).faceId
        mouseClick(view, x, y)
        compare(requestSpy.count, 1)
        compare(requestSpy.signalArguments[0][0], expected)
        compare(bench.uv.selectedFaceId, expected) // the UV editor follows
        compare(layer.overlay.face.length, 3)     // and the viewport highlights it
        // Clicking the posed bone selects it in the rig editor.
        watch(secondSpy, view, "meshBonePicked")
        const bone = layer.overlay.bones[0]
        mouseClick(view, (bone.head.x + bone.tail.x) / 2, (bone.head.y + bone.tail.y) / 2)
        compare(secondSpy.count, 1)
        compare(secondSpy.signalArguments[0][0], bench.bone)
        compare(bench.rig.selectedBoneId, bench.bone)
        // A UV-editor selection highlights in 3D without a click.
        bench.faceId = bench.second
        compare(layer.overlay.face.length, 3)
        verify(layer.boneAt(x, y) === "")
    }
}
