// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 1080
    height: 720
    color: Tk.Theme.color.bg

    Tk.MeshRigDocument {
        id: mesh
        Component.onCompleted: {
            const a = addVertex(Qt.vector3d(-105, -105, 0))
            const b = addVertex(Qt.vector3d(105, -105, 0))
            const c = addVertex(Qt.vector3d(105, 105, 0))
            const d = addVertex(Qt.vector3d(-105, 105, 0))
            const first = addFace(a, b, c)
            const second = addFace(a, c, d)
            setUV(first, 0, Qt.point(0, 0))
            setUV(first, 1, Qt.point(1, 0))
            setUV(first, 2, Qt.point(1, 1))
            setUV(second, 0, Qt.point(0, 0))
            setUV(second, 1, Qt.point(1, 1))
            setUV(second, 2, Qt.point(0, 1))
            resizeTexture(256, 256)
            // Separate strokes: samples inside one stroke are joined by dabs.
            beginPaint()
            paintRay(Qt.vector3d(-45, 35, 100), Qt.vector3d(0, 0, -1), "#e9a358", 24, 1)
            endPaint()
            beginPaint()
            paintRay(Qt.vector3d(35, -25, 100), Qt.vector3d(0, 0, -1), "#3caec2", 30, 1)
            endPaint()
            beginPaint("metalness")
            paintRay(Qt.vector3d(35, -25, 100), Qt.vector3d(0, 0, -1), "#ffffff", 30, 1)
            endPaint()
        }
    }
    Tk.SceneModel3D {
        id: scene
        Component.onCompleted: addEntity("Depth marker", "sphere",
                                         Qt.vector3d(160, -80, -80), "#9575d8")
    }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.md
        Tk.Heading { text: qsTr("Posed mesh and painted texture"); level: 1 }
        Tk.Caption {
            text: qsTr("MeshRigDocument geometry uploaded to SceneView3D · real UV texture pixels · native 3D picking")
        }
        Tk.SceneView3D {
            id: viewport
            objectName: "meshRigViewport"
            Tk.Flex.grow: 1
            scene: scene
            meshDocument: mesh
            cameraPosition: Qt.vector3d(0, 80, 520)
            cameraEulerDegrees: Qt.vector3d(-8, 0, 0)
        }
        Tk.Caption {
            text: qsTr("%1 triangles · %2 texture pixels · %3")
                .arg(mesh.faces.length).arg(mesh.textureWidth * mesh.textureHeight)
                .arg(viewport.rendererAvailable ? qsTr("GPU renderer") : qsTr("3D renderer unavailable"))
        }
    }
}
