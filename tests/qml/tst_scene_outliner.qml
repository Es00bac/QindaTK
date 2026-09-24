// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "SceneOutliner"
    when: windowShown
    visible: true
    width: 520
    height: 440

    Tk.SceneModel3D { id: scene }
    Tk.SceneOutliner {
        id: outliner
        anchors.fill: parent
        scene: scene
        keepWorldOnDrop: true
    }
    SignalSpy { id: reparentSpy; target: outliner; signalName: "reparentRequested" }
    Connections {
        target: outliner
        function onReparentRequested(entityId, parentId, keepWorld) {
            scene.reparent(entityId, parentId, keepWorld)
        }
    }

    property string parentId: ""
    property string childId: ""
    property string targetId: ""

    function init() {
        scene.fromJson('{"format":"QindaTK.Scene3D","version":1,"entities":[]}')
        parentId = scene.addEntity("Collection", "cube", Qt.vector3d(10, 0, 0), "#35bbc4")
        childId = scene.addEntity("Camera", "sphere", Qt.vector3d(2, 3, 4), "#ed9d49")
        targetId = scene.addEntity("Target", "cylinder", Qt.vector3d(-20, 0, 0), "#ad84ea")
        scene.reparent(childId, parentId, false)
        scene.select(parentId)
        outliner.rebuildRows()
        reparentSpy.clear()
        waitForRendering(outliner)
    }

    function test_keyboardTraversalAndSelectionUseStableIds() {
        verify(findChild(outliner, "sceneOutlinerRow_" + childId) !== null)
        outliner.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(scene.selectedId, childId)
        keyClick(Qt.Key_Up)
        compare(scene.selectedId, parentId)
        keyClick(Qt.Key_Left)
        verify(findChild(outliner, "sceneOutlinerRow_" + childId) === null)
        keyClick(Qt.Key_Right)
        verify(findChild(outliner, "sceneOutlinerRow_" + childId) !== null)
    }

    function test_localVisibilityTogglesUnderHiddenAncestor() {
        scene.setAppearance(parentId, "#35bbc4", false)
        const child = outliner.entityFor(childId)
        verify(child.visible === false)
        verify(child.localVisible === true)
        outliner.toggleVisibility(childId)
        verify(outliner.entityFor(childId).localVisible === false)
        scene.setAppearance(parentId, "#35bbc4", true)
        verify(outliner.entityFor(childId).visible === false)
        outliner.toggleVisibility(childId)
        verify(outliner.entityFor(childId).visible === true)
    }

    function test_dropRequestsExplicitKeepWorldReparent() {
        const before = scene.worldTransform(childId).position
        verify(outliner.reparentEntity(childId, targetId))
        compare(reparentSpy.count, 1)
        compare(reparentSpy.signalArguments[0][0], childId)
        compare(reparentSpy.signalArguments[0][1], targetId)
        compare(reparentSpy.signalArguments[0][2], true)
        compare(outliner.entityFor(childId).parentId, targetId)
        compare(scene.worldTransform(childId).position, before)
    }
}
