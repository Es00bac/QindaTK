// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 1080
    height: 720
    Tk.SceneModel3D {
        id: sceneModel
        Component.onCompleted: {
            const cube = addEntity("Parent cube", "cube", Qt.vector3d(-100, 0, 0), "#35bbc4")
            setTransform(cube, Qt.vector3d(-100, 0, 0), Qt.vector3d(0, 25, 0), 1.2)
            const sphere = addEntity("Orbiting sphere", "sphere", Qt.vector3d(80, 30, 0), "#ed9d49")
            reparent(sphere, cube, true)
            const cylinder = addEntity("Cylinder", "cylinder", Qt.vector3d(0, -130, -70), "#ad84ea")
            setTransform(cylinder, Qt.vector3d(0, -130, -70), Qt.vector3d(0, 0, 75), 0.8)
            select(cube)
        }
    }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.md
        Tk.Heading { text: qsTr("3D scene primitives"); level: 1 }
        Tk.Caption { text: qsTr("Real Qt Quick 3D rendering · persistent scene hierarchy · move, rotate and scale") }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Button { text: qsTr("Undo"); enabled: sceneModel.canUndo; onClicked: sceneModel.undo() }
            Tk.Button { text: qsTr("Redo"); enabled: sceneModel.canRedo; onClicked: sceneModel.redo() }
            Tk.CheckBox { id: keepWorld; text: qsTr("Keep world transform on drop"); checked: true }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Badge { text: viewport.rendererAvailable ? qsTr("Qt RHI renderer") : qsTr("Renderer unavailable") }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            Tk.SceneOutliner {
                id: outliner
                Tk.Flex.basis: 270
                Tk.Flex.shrink: 0
                listHeight: 560
                scene: sceneModel
                keepWorldOnDrop: keepWorld.checked
                onReparentRequested: function(entityId, parentId, keepWorldTransform) {
                    sceneModel.reparent(entityId, parentId, keepWorldTransform)
                }
            }
            Tk.SceneView3D {
                id: viewport
                Tk.Flex.grow: 1
                scene: sceneModel
                translationSnap: 10
                rotationSnap: 15
            }
        }
        Tk.Caption { text: sceneModel.lastError.length ? sceneModel.lastError : qsTr("Select in the outliner or viewport · Drag a row to reparent · Choose transform mode and axis, then drag an object · Escape cancels") }
    }
}
