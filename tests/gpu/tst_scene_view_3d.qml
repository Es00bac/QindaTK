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
        }
    }
}
