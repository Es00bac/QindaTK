// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

Item {
    width: 640
    height: 480
    Tk.SceneModel3D { id: sceneData }
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
    }
}
