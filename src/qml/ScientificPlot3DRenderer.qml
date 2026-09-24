// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D as Q3D
import QindaTK as Tk

// Qt Quick 3D view of a ScientificScene3D. Loaded lazily by ScientificPlot3D
// on RHI backends; excluded from the module when Qt Quick 3D is missing.
// AGENT-CONTRACT: the camera binds to the scene's cameraPosition,
// cameraRotation, fieldOfView and clip planes exactly, so pixels here match
// ScientificScene3D.project() and the software/export painter.
Q3D.View3D {
    id: view
    objectName: "scientificRenderer3D"
    property Tk.ScientificScene3D plotScene: null
    environment: Q3D.SceneEnvironment {
        backgroundMode: Q3D.SceneEnvironment.Color
        clearColor: Tk.Theme.color.canvas
        antialiasingMode: Q3D.SceneEnvironment.MSAA
    }
    camera: Q3D.PerspectiveCamera {
        id: plotCamera
        position: view.plotScene ? view.plotScene.cameraPosition : Qt.vector3d(0, 0, 0)
        rotation: view.plotScene ? view.plotScene.cameraRotation : Qt.quaternion(1, 0, 0, 0)
        fieldOfView: view.plotScene ? view.plotScene.fieldOfView : 45
        clipNear: view.plotScene ? view.plotScene.clipNear : 1
        clipFar: view.plotScene ? view.plotScene.clipFar : 100000
    }
    // Headlight: the light follows the camera, like the software shading.
    Q3D.DirectionalLight { rotation: plotCamera.rotation }
    Q3D.Model {
        objectName: "scientificSurface3D"
        geometry: Tk.ScientificGeometry3D { scene: view.plotScene; part: Tk.ScientificGeometry3D.Surface }
        materials: Q3D.DefaultMaterial {
            vertexColorsEnabled: true
            cullMode: Q3D.Material.NoCulling
        }
    }
    Q3D.Model {
        objectName: "scientificPoints3D"
        geometry: Tk.ScientificGeometry3D { scene: view.plotScene; part: Tk.ScientificGeometry3D.Points }
        materials: Q3D.DefaultMaterial { vertexColorsEnabled: true }
    }
    Q3D.Model {
        objectName: "scientificFrame3D"
        geometry: Tk.ScientificGeometry3D { scene: view.plotScene; part: Tk.ScientificGeometry3D.Frame }
        materials: Q3D.DefaultMaterial {
            vertexColorsEnabled: true
            lighting: Q3D.DefaultMaterial.NoLighting
        }
    }
}
