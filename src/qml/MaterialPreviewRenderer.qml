// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick3D as Q3D
import QindaTK as Tk

// AGENT-CONTRACT (WP-076): this is the optional GPU projection of the
// SceneModel3D material snapshot; the model owns editing and persistence.
Q3D.View3D {
    id: view
    objectName: "materialPreviewRenderer"
    property var control: null
    readonly property var materialData: control ? control.materialData : ({})
    environment: Q3D.SceneEnvironment {
        backgroundMode: Q3D.SceneEnvironment.Color
        clearColor: Tk.Theme.color.canvas
    }
    camera: Q3D.PerspectiveCamera { position: Qt.vector3d(0, 0, 250) }
    Q3D.DirectionalLight { eulerRotation: Qt.vector3d(-35, -35, 0); brightness: 1.3 }
    Q3D.DirectionalLight { eulerRotation: Qt.vector3d(25, 135, 0); brightness: 0.6 }
    Q3D.Model {
        objectName: "materialPreviewSphere"
        source: "#Sphere"
        materials: Q3D.PrincipledMaterial {
            baseColor: view.materialData.baseColor || Tk.Theme.color.accent
            roughness: view.materialData.roughness === undefined ? 0.6 : view.materialData.roughness
            metalness: view.materialData.metalness === undefined ? 0 : view.materialData.metalness
            emissiveFactor: Qt.vector3d(view.materialData.emissiveColor ? view.materialData.emissiveColor.r : 0,
                                        view.materialData.emissiveColor ? view.materialData.emissiveColor.g : 0,
                                        view.materialData.emissiveColor ? view.materialData.emissiveColor.b : 0)
        }
    }
}
