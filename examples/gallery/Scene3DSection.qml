// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "3D scene"
    note: "SceneView3D · persistent hierarchy · Qt Quick 3D (GPU backend required)"
    Tk.SceneModel3D {
        id: sceneModel
        Component.onCompleted: {
            const id = addEntity("Cube", "cube", Qt.vector3d(0, 0, 0), "#35bbc4")
            setTransform(id, Qt.vector3d(0, 0, 0), Qt.vector3d(0, 25, 0), 1)
        }
    }
    GalleryRow {
        label: "Pick and translate"
        Item {
            implicitWidth: 520
            implicitHeight: 280
            Tk.SceneView3D { anchors.fill: parent; scene: sceneModel }
        }
    }
}
