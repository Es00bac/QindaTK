// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "MaterialPreview"
    when: windowShown
    visible: true
    width: 440
    height: 460
    Tk.SceneModel3D { id: sceneData }
    Component {
        id: previewComponent
        Tk.MaterialPreview { width: 420; height: 440; scene: sceneData }
    }
    function test_inspectorEditsPersistentScene() {
        const preview = createTemporaryObject(previewComponent, root)
        verify(preview)
        const entity = sceneData.addEntity("Sphere", "sphere", Qt.vector3d(0, 0, 0), "#4488cc")
        verify(sceneData.select(entity))
        compare(preview.materialData.entityId, entity)
        compare(preview.materialData.baseColor.toString(), "#4488cc")
        compare(findChild(preview, "materialBaseColor").color.toString(), "#4488cc")
        const roughness = findChild(preview, "materialRoughness")
        const metalness = findChild(preview, "materialMetalness")
        verify(roughness && metalness)
        roughness.commit(0.25)
        metalness.commit(0.8)
        compare(sceneData.material(entity).roughness, 0.25)
        compare(sceneData.material(entity).metalness, 0.8)
        verify(sceneData.toJson().includes("\"roughness\":0.25"))
        verify(sceneData.undo())
        compare(sceneData.material(entity).metalness, 0)
        compare(metalness.value, 0)
    }
    function test_existingMaterialColors() {
        const cube = sceneData.addEntity("Cube", "cube", Qt.vector3d(0, 0, 0), "#35bbc4")
        verify(sceneData.setMaterial(cube, "#35bbc4", 0.22, 0.75, "#080d10"))
        verify(sceneData.select(cube))
        const preview = createTemporaryObject(previewComponent, root)
        verify(preview)
        compare(preview.materialData.baseColor.toString(), "#35bbc4")
        compare(findChild(preview, "materialBaseColor").color.toString(), "#35bbc4")
        compare(findChild(preview, "materialEmissiveColor").color.toString(), "#080d10")
    }
    function test_sceneFixtureMaterialColors() {
        const fixtureComponent = Qt.createComponent(Qt.resolvedUrl("../../examples/scene-3d/Main.qml"))
        compare(fixtureComponent.status, Component.Ready, fixtureComponent.errorString())
        const fixture = fixtureComponent.createObject(root)
        verify(fixture)
        const preview = findChild(fixture, "sceneMaterialInspector")
        verify(preview)
        compare(preview.materialData.baseColor.toString(), "#35bbc4")
        compare(findChild(preview, "materialBaseColor").color.toString(), "#35bbc4")
        compare(findChild(preview, "materialEmissiveColor").color.toString(), "#080d10")
        fixture.destroy()
    }
    function test_unparentedSceneFixtureMaterialColors() {
        const fixtureComponent = Qt.createComponent(Qt.resolvedUrl("../../examples/scene-3d/Main.qml"))
        compare(fixtureComponent.status, Component.Ready, fixtureComponent.errorString())
        const fixture = fixtureComponent.createObject(null)
        verify(fixture)
        fixture.parent = root
        const preview = findChild(fixture, "sceneMaterialInspector")
        verify(preview)
        tryCompare(findChild(findChild(preview, "materialBaseColor"), "colorHex"),
                   "text", "#35bbc4")
        tryCompare(findChild(findChild(preview, "materialEmissiveColor"), "colorHex"),
                   "text", "#080d10")
        fixture.destroy()
    }
}
