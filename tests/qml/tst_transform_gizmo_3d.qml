// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// The software backend has no Qt Quick 3D renderer, so this suite drives the
// gizmo through an exact orthographic adapter (camera on +Z looking down -Z,
// `pixelsPerUnit` screen pixels per world unit, world origin at 200,150).
// tests/gpu/tst_scene_view_3d.qml repeats the drags through the real viewport.
TestCase {
    id: root
    name: "TransformGizmo3D"
    when: windowShown
    visible: true
    width: 400
    height: 300

    Component {
        id: sceneComponent
        Tk.SceneModel3D {}
    }
    Item {
        id: orthoView
        anchors.fill: parent
        property real pixelsPerUnit: 1
        property int presses: 0
        function projectWorldPosition(p) {
            return Qt.vector3d(200 + p.x * pixelsPerUnit, 150 - p.y * pixelsPerUnit, 1000 - p.z)
        }
        function viewRay(x, y) {
            return {origin: Qt.vector3d((x - 200) / pixelsPerUnit, (150 - y) / pixelsPerUnit, 1000),
                    direction: Qt.vector3d(0, 0, -1)}
        }
        // Stands in for the viewport's own pick/drag area below the gizmo.
        MouseArea {
            anchors.fill: parent
            onPressed: ++orthoView.presses
        }
        Tk.TransformGizmo3D {
            id: gizmo
            anchors.fill: parent
            view: orthoView
        }
    }

    function init() {
        orthoView.pixelsPerUnit = 1
        orthoView.presses = 0
        gizmo.mode = "translate"
        gizmo.space = "world"
        gizmo.translationSnap = 0
    }
    function makeScene() {
        const scene = createTemporaryObject(sceneComponent, root)
        verify(scene)
        gizmo.scene = scene
        return scene
    }
    function selectAt(scene, position) {
        const id = scene.addEntity("Cube", "cube", position, "#35bbc4")
        verify(scene.select(id))
        gizmo.relayout()
        verify(gizmo.handles.visible)
        return id
    }
    function axis(name) {
        return gizmo.handles.axes.find(a => a.name === name)
    }
    function drag(fromX, fromY, toX, toY) {
        mousePress(gizmo, fromX, fromY)
        mouseMove(gizmo, toX, toY)
        mouseRelease(gizmo, toX, toY)
    }
    function worldPosition(scene, id) {
        return scene.worldTransform(id).position
    }

    function test_handlesKeepScreenLengthAcrossZoom() {
        const scene = makeScene()
        selectAt(scene, Qt.vector3d(0, 0, 0))
        const origin = gizmo.handles.origin
        fuzzyCompare(axis("x").end.x - origin.x, gizmo.armLength, 0.01)
        fuzzyCompare(axis("y").end.y - origin.y, -gizmo.armLength, 0.01)
        verify(!axis("z").usable, "an axis along the sight line has no drag direction")
        orthoView.pixelsPerUnit = 4
        gizmo.relayout()
        fuzzyCompare(axis("x").end.x - gizmo.handles.origin.x, gizmo.armLength, 0.01)
        fuzzyCompare(gizmo.handles.length, gizmo.armLength / 4, 0.0001)
        compare(gizmo.handleAt(origin.x + gizmo.armLength * 0.8, origin.y), "x")
        compare(gizmo.handleAt(origin.x, origin.y), "", "the centre belongs to the object")
    }

    function test_localAxisDragAgreesWithNumericEdit() {
        const scene = makeScene()
        const parentId = scene.addEntity("Parent", "cube", Qt.vector3d(0, 0, 0), "#35bbc4")
        verify(scene.setTransform(parentId, Qt.vector3d(20, 10, 0), Qt.vector3d(0, 0, 90), 2))
        const child = scene.addEntity("Child", "sphere", Qt.vector3d(0, 0, 0), "#ed9d49")
        verify(scene.reparent(child, parentId, false))
        verify(scene.select(child))
        gizmo.space = "local"
        gizmo.relayout()
        // Local X of a child under a +90 degree Z parent is world +Y: the arrow points up.
        const origin = gizmo.handles.origin
        fuzzyCompare(origin.x, 220, 0.001)
        fuzzyCompare(axis("x").end.x, origin.x, 0.01)
        fuzzyCompare(axis("x").end.y, origin.y - gizmo.armLength, 0.01)
        const pressY = origin.y - gizmo.armLength * 0.7
        drag(origin.x, pressY, origin.x, pressY - 30)
        compare(orthoView.presses, 0, "a handle press must not reach the viewport")
        verify(!scene.editing)
        const moved = worldPosition(scene, child)
        fuzzyCompare(moved.x, 20, 0.001)
        fuzzyCompare(moved.y, 40, 0.001)
        fuzzyCompare(moved.z, 0, 0.001)
        const local = scene.entities.find(e => e.entityId === child).position
        fuzzyCompare(local.x, 15, 0.001)
        fuzzyCompare(local.y, 0, 0.001)
        verify(scene.undo(), "one gizmo drag is one undo step")
        fuzzyCompare(worldPosition(scene, child).y, 10, 0.001)
        fuzzyCompare(scene.entities.find(e => e.entityId === child).position.x, 0, 0.001)
        // The same edit through the numeric APIs lands on the same transform.
        verify(scene.beginTransform(child))
        verify(scene.previewAxisTranslation(30, "x", "local"))
        verify(scene.commitTransform())
        fuzzyCompare(worldPosition(scene, child).y, 40, 0.001)
        verify(scene.undo())
        verify(scene.setTransform(child, Qt.vector3d(15, 0, 0), Qt.vector3d(0, 0, 0), 1))
        fuzzyCompare(worldPosition(scene, child).x, moved.x, 0.001)
        fuzzyCompare(worldPosition(scene, child).y, moved.y, 0.001)
    }

    function test_planeRingAndScaleHandles() {
        const scene = makeScene()
        const id = selectAt(scene, Qt.vector3d(0, 0, 0))
        const origin = gizmo.handles.origin
        const plane = gizmo.handles.planes.find(p => p.name === "xy")
        verify(plane.usable)
        verify(!gizmo.handles.planes.find(p => p.name === "xz").usable, "edge-on plane")
        const cx = (plane.corners[0].x + plane.corners[2].x) / 2
        const cy = (plane.corners[0].y + plane.corners[2].y) / 2
        compare(gizmo.handleAt(cx, cy), "xy")
        drag(cx, cy, cx + 10, cy - 20)
        let p = worldPosition(scene, id)
        fuzzyCompare(p.x, 10, 0.001)
        fuzzyCompare(p.y, 20, 0.001)
        fuzzyCompare(p.z, 0, 0.001)
        verify(scene.undo())

        gizmo.mode = "rotate"
        gizmo.relayout()
        verify(gizmo.handles.rings.find(r => r.name === "z").usable)
        verify(!gizmo.handles.rings.find(r => r.name === "x").usable, "edge-on ring")
        const r = gizmo.armLength * Math.SQRT1_2
        compare(gizmo.handleAt(origin.x + r, origin.y - r), "z")
        // A quarter turn counter-clockwise on screen is +90 degrees about +Z (toward the viewer).
        drag(origin.x + r, origin.y - r, origin.x - r, origin.y - r)
        fuzzyCompare(scene.entities[0].eulerDegrees.z, 90, 0.01)
        verify(scene.undo())

        gizmo.mode = "scale"
        gizmo.relayout()
        compare(gizmo.handleAt(origin.x + gizmo.armLength, origin.y), "uniform")
        drag(origin.x + gizmo.armLength, origin.y, origin.x + gizmo.armLength * 2, origin.y)
        fuzzyCompare(scene.worldTransform(id).scale, 2, 0.001)
        verify(scene.undo())
        fuzzyCompare(scene.worldTransform(id).scale, 1, 0.001)
    }

    function test_cancelAndPassThrough() {
        const scene = makeScene()
        const id = selectAt(scene, Qt.vector3d(0, 0, 0))
        const origin = gizmo.handles.origin
        const error = scene.lastError
        const revision = scene.revision
        mousePress(gizmo, origin.x + gizmo.armLength * 0.7, origin.y)
        compare(gizmo.activeHandle, "x")
        mouseMove(gizmo, origin.x + gizmo.armLength * 0.7 + 20, origin.y)
        verify(scene.editing)
        fuzzyCompare(worldPosition(scene, id).x, 20, 0.001)
        verify(scene.cancelTransform(), "the viewport's Escape path")
        mouseMove(gizmo, origin.x + gizmo.armLength * 0.7 + 60, origin.y)
        mouseRelease(gizmo, origin.x + gizmo.armLength * 0.7 + 60, origin.y)
        verify(!scene.editing)
        compare(gizmo.activeHandle, "")
        fuzzyCompare(worldPosition(scene, id).x, 0, 0.001)
        compare(scene.lastError, error, "moves after a cancel must not preview")
        compare(scene.revision, revision, "a cancelled drag records no history")
        mousePress(gizmo, origin.x, origin.y)
        compare(orthoView.presses, 1, "a press away from the handles reaches the viewport")
        verify(!scene.editing)
        mouseRelease(gizmo, origin.x, origin.y)
    }

    function test_accessibleHandlesNudge() {
        const scene = makeScene()
        const id = selectAt(scene, Qt.vector3d(0, 0, 0))
        const xTarget = findChild(gizmo, "transformGizmoHandle3D_x")
        verify(xTarget && xTarget.visible)
        verify(!findChild(gizmo, "transformGizmoHandle3D_z").visible)
        verify(gizmo.nudge("x", 1))
        fuzzyCompare(worldPosition(scene, id).x, 1, 0.001)
        gizmo.translationSnap = 5
        verify(gizmo.nudge("x", -1))
        fuzzyCompare(worldPosition(scene, id).x, -4, 0.001)
        verify(!gizmo.nudge("z", 1), "hidden handles take no action")
        gizmo.mode = "rotate"
        gizmo.relayout()
        verify(gizmo.nudge("z", 1))
        fuzzyCompare(scene.entities[0].eulerDegrees.z, 15, 0.01)
        gizmo.mode = "scale"
        gizmo.relayout()
        verify(findChild(gizmo, "transformGizmoHandle3D_uniform").visible)
        verify(gizmo.nudge("uniform", 1))
        fuzzyCompare(scene.worldTransform(id).scale, 1.25, 0.001)
    }
}
