// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

Item {
    width: 640
    height: 480
    Tk.SceneModel3D { id: sceneData }
    Tk.SceneModel3D { id: gizmoScene }
    Tk.SceneView3D {
        id: viewport
        anchors.fill: parent
        scene: sceneData
        cameraPosition: Qt.vector3d(0, 0, 600)
        cameraEulerDegrees: Qt.vector3d(0, 0, 0)
    }
    TestCase {
        name: "SceneView3DInteraction"
        when: windowShown
        function test_cameraNavigation() {
            viewport.cameraPosition = Qt.vector3d(0, 0, 600)
            viewport.cameraEulerDegrees = Qt.vector3d(0, 0, 0)
            viewport.cameraTarget = Qt.vector3d(0, 0, 0)
            mousePress(viewport, 320, 240, Qt.RightButton)
            mouseMove(viewport, 360, 220)
            mouseRelease(viewport, 360, 220, Qt.RightButton)
            verify(viewport.cameraPosition.x > 0, "right drag must orbit")
            verify(viewport.cameraPosition.y > 0, "vertical drag must orbit")
            fuzzyCompare(viewport.cameraPosition.minus(viewport.cameraTarget).length(), 600, 0.01)
            const beforePanX = viewport.cameraTarget.x
            const beforePanY = viewport.cameraTarget.y
            mousePress(viewport, 320, 240, Qt.MiddleButton)
            mouseMove(viewport, 340, 250)
            mouseRelease(viewport, 340, 250, Qt.MiddleButton)
            verify(Math.hypot(viewport.cameraTarget.x - beforePanX,
                              viewport.cameraTarget.y - beforePanY) > 0, "middle drag must pan")
            const distance = viewport.cameraPosition.minus(viewport.cameraTarget).length()
            viewport.zoomCamera(1)
            verify(viewport.cameraPosition.minus(viewport.cameraTarget).length() < distance,
                   "positive wheel steps must zoom in")
            verify(!sceneData.editing, "camera navigation must not open a scene edit")
            viewport.cameraPosition = Qt.vector3d(0, 0, 600)
            viewport.cameraEulerDegrees = Qt.vector3d(0, 0, 0)
            viewport.cameraTarget = Qt.vector3d(0, 0, 0)
        }
        function test_pickDragCancel() {
            const id = sceneData.addEntity("Cube", "cube", Qt.vector3d(0, 0, 0), "#35bbc4")
            tryVerify(function() { return viewport.pickEntity(320, 240).entityId === id })
            const projected = viewport.projectWorldPosition(Qt.vector3d(0, 0, 0))
            fuzzyCompare(projected.x, 320, 0.1)
            fuzzyCompare(projected.y, 240, 0.1)
            mousePress(viewport, 320, 240)
            mouseMove(viewport, 360, 240)
            mouseRelease(viewport, 360, 240)
            verify(sceneData.worldTransform(id).position.x > 0)
            verify(sceneData.undo())
            fuzzyCompare(sceneData.worldTransform(id).position.x, 0, 0.001)
            tryVerify(function() { return viewport.pickEntity(320, 240).entityId === id })
            mousePress(viewport, 320, 240)
            mouseMove(viewport, 360, 240)
            keyClick(Qt.Key_Escape)
            mouseMove(viewport, 390, 240)
            mouseRelease(viewport, 390, 240)
            verify(!sceneData.editing)
            fuzzyCompare(sceneData.worldTransform(id).position.x, 0, 0.001)
            const rotate = findChild(viewport, "sceneMode3D_rotate")
            verify(rotate !== null)
            mouseClick(rotate)
            compare(viewport.transformMode, "rotate")
            viewport.transformConstraint = "z"
            tryVerify(function() { return viewport.pickEntity(320, 240).entityId === id })
            mousePress(viewport, 320, 240)
            verify(sceneData.editing, "rotation press did not start edit")
            mouseMove(viewport, 380, 240)
            mouseRelease(viewport, 380, 240)
            verify(sceneData.entities[0].eulerDegrees.z > 20,
                   "rotation=" + sceneData.entities[0].eulerDegrees.z + ", error=" + sceneData.lastError)
            verify(sceneData.undo())
            fuzzyCompare(sceneData.entities[0].eulerDegrees.z, 0, 0.01)
            viewport.transformMode = "scale"
            tryVerify(function() { return viewport.pickEntity(320, 240).entityId === id })
            mousePress(viewport, 320, 240)
            mouseMove(viewport, 380, 240)
            mouseRelease(viewport, 380, 240)
            verify(sceneData.worldTransform(id).scale > 1.5)
            verify(sceneData.undo())
            fuzzyCompare(sceneData.worldTransform(id).scale, 1, 0.001)
            viewport.transformMode = "translate"
            viewport.transformConstraint = "x"
            tryVerify(function() { return viewport.pickEntity(320, 240).entityId === id })
            mousePress(viewport, 320, 240)
            mouseMove(viewport, 360, 260)
            mouseRelease(viewport, 360, 260)
            verify(sceneData.worldTransform(id).position.x > 0)
            fuzzyCompare(sceneData.worldTransform(id).position.y, 0, 0.001)
        }
        // Sorted after test_pickDragCancel, which reads sceneData.entities[0].
        function test_transformGizmoHandles() {
            viewport.scene = gizmoScene
            viewport.transformMode = "translate"
            viewport.transformConstraint = "free"
            viewport.transformSpace = "world"
            viewport.cameraPosition = Qt.vector3d(0, 0, 600)
            viewport.cameraEulerDegrees = Qt.vector3d(0, 0, 0)
            viewport.cameraTarget = Qt.vector3d(0, 0, 0)
            const id = gizmoScene.addEntity("Gizmo cube", "cube", Qt.vector3d(0, 0, 0), "#35bbc4")
            verify(gizmoScene.select(id))
            const gizmo = findChild(viewport, "transformGizmo3D")
            verify(gizmo)
            // Relayout is deferred: wait until the handles sit on the new selection.
            tryVerify(function() { return gizmo.handles.visible && gizmo.handles.mode === "translate"
                                          && Math.abs(gizmo.handles.origin.x - 320) < 0.5 })
            fuzzyCompare(gizmo.handles.axes[0].end.x - gizmo.handles.origin.x, gizmo.armLength, 0.5)
            // Twice the camera distance doubles the world length, not the screen length.
            const nearLength = gizmo.handles.length
            viewport.zoomCamera(-4)
            tryVerify(function() { return Math.abs(gizmo.handles.length / nearLength - 2) < 0.02 })
            let origin = gizmo.handles.origin
            const arrow = gizmo.handles.axes[0]
            fuzzyCompare(arrow.end.x - origin.x, gizmo.armLength, 0.5)
            const px = origin.x + (arrow.end.x - origin.x) * 0.7
            mousePress(viewport, px, origin.y)
            compare(gizmo.activeHandle, "x")
            mouseMove(viewport, px + 36, origin.y + 20)
            mouseRelease(viewport, px + 36, origin.y + 20)
            const moved = gizmoScene.worldTransform(id).position
            verify(moved.x > 0)
            fuzzyCompare(moved.y, 0, 0.001)
            fuzzyCompare(moved.z, 0, 0.001)
            // The object followed the pointer along the arrow; the off-axis motion was ignored.
            fuzzyCompare(viewport.projectWorldPosition(moved).x, origin.x + 36, 1)
            verify(gizmoScene.undo())
            fuzzyCompare(gizmoScene.worldTransform(id).position.x, 0, 0.001)

            viewport.transformMode = "rotate"
            tryVerify(function() { return gizmo.handles.mode === "rotate" })
            origin = gizmo.handles.origin
            const r = gizmo.armLength * Math.SQRT1_2
            mousePress(viewport, origin.x + r, origin.y - r)
            compare(gizmo.activeHandle, "z")
            mouseMove(viewport, origin.x - r, origin.y - r)
            mouseRelease(viewport, origin.x - r, origin.y - r)
            fuzzyCompare(gizmoScene.entities[0].eulerDegrees.z, 90, 0.5)
            verify(gizmoScene.undo())

            viewport.transformMode = "translate"
            tryVerify(function() { return gizmo.handles.mode === "translate" })
            origin = gizmo.handles.origin
            mousePress(viewport, origin.x + gizmo.armLength * 0.7, origin.y)
            mouseMove(viewport, origin.x + gizmo.armLength * 0.7 + 30, origin.y)
            verify(gizmoScene.editing)
            keyClick(Qt.Key_Escape)
            mouseMove(viewport, origin.x + gizmo.armLength * 0.7 + 60, origin.y)
            mouseRelease(viewport, origin.x + gizmo.armLength * 0.7 + 60, origin.y)
            verify(!gizmoScene.editing)
            fuzzyCompare(gizmoScene.worldTransform(id).position.x, 0, 0.001)
            viewport.scene = sceneData
            viewport.cameraPosition = Qt.vector3d(0, 0, 600)
        }
    }
}
