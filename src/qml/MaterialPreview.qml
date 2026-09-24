// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (WP-076): SceneModel3D owns material values, undo and save.
// This inspector invokes one scene command per committed user edit.
T.Control {
    id: root
    objectName: "materialPreview"
    property Tk.SceneModel3D scene: null
    property string entityId: scene ? scene.selectedId : ""
    property string tooltip: qsTr("3D material preview and editor")
    property int sceneRevision: 0
    readonly property var materialData: {
        if (sceneRevision < 0 || !scene || !entityId) return ({})
        return scene.material(entityId)
    }
    function syncFields() {
        if (!materialData.entityId || !baseColorField || !roughnessField
            || !metalnessField || !emissiveField) return
        // ColorField can lose its initial TextInput binding while its read-only
        // state settles. Reapply the public hex value after the model arrives.
        baseColorField.applyHex(materialData.baseColor.toString())
        emissiveField.applyHex(materialData.emissiveColor.toString())
        roughnessField.value = materialData.roughness
        metalnessField.value = materialData.metalness
    }
    onMaterialDataChanged: { syncFields(); Qt.callLater(syncFields) }
    Component.onCompleted: { syncFields(); Qt.callLater(syncFields) }
    readonly property bool rendererAvailable: GraphicsInfo.api !== GraphicsInfo.Software
                                               && previewLoader.status === Loader.Ready
    Connections { target: root.scene; function onChanged() { ++root.sceneRevision } }

    function edit(baseColor, roughness, metalness, emissiveColor) {
        if (scene && entityId)
            scene.setMaterial(entityId, baseColor, roughness, metalness, emissiveColor)
    }

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    background: Rectangle {
        color: Tk.Theme.color.panel
        border.color: Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            Tk.Overline { title: qsTr("MATERIAL"); Tk.Flex.grow: 1 }
            Tk.Caption { text: root.entityId ? root.entityId.slice(0, 8) : qsTr("No selection") }
        }
        Item {
            objectName: "materialPreviewSurface"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: Tk.Theme.size.panelMinHeight
            implicitHeight: Tk.Theme.size.panelMinHeight
            Loader {
                id: previewLoader
                anchors.fill: parent
                active: root.GraphicsInfo.api !== GraphicsInfo.Software
                source: "MaterialPreviewRenderer.qml"
                onLoaded: item.control = root
            }
            Tk.Label {
                anchors.centerIn: parent
                visible: !root.rendererAvailable
                text: qsTr("3D material preview requires a GPU backend")
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Base color"); width: Tk.Theme.size.panelMinWidth / 2 }
            Tk.ColorField {
                id: baseColorField
                objectName: "materialBaseColor"
                Tk.Flex.grow: 1
                tooltip: qsTr("Material base color")
                enabled: !!root.materialData.entityId
                onColorEdited: function(color) {
                    root.edit(color, root.materialData.roughness,
                              root.materialData.metalness, root.materialData.emissiveColor)
                }
            }
            Binding {
                target: baseColorField
                property: "color"
                value: root.materialData.baseColor || Tk.Theme.color.accent
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Roughness"); width: Tk.Theme.size.panelMinWidth / 2 }
            Tk.NumberField {
                id: roughnessField
                objectName: "materialRoughness"
                Tk.Flex.grow: 1
                from: 0; to: 1; stepSize: 0.05; decimals: 2
                value: 0.6
                tooltip: qsTr("Material roughness from smooth to matte")
                enabled: !!root.materialData.entityId
                onValueModified: function(value) {
                    root.edit(root.materialData.baseColor, value,
                              root.materialData.metalness, root.materialData.emissiveColor)
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Metalness"); width: Tk.Theme.size.panelMinWidth / 2 }
            Tk.NumberField {
                id: metalnessField
                objectName: "materialMetalness"
                Tk.Flex.grow: 1
                from: 0; to: 1; stepSize: 0.05; decimals: 2
                value: 0
                tooltip: qsTr("Material metalness from dielectric to metal")
                enabled: !!root.materialData.entityId
                onValueModified: function(value) {
                    root.edit(root.materialData.baseColor, root.materialData.roughness,
                              value, root.materialData.emissiveColor)
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Emission"); width: Tk.Theme.size.panelMinWidth / 2 }
            Tk.ColorField {
                id: emissiveField
                objectName: "materialEmissiveColor"
                Tk.Flex.grow: 1
                tooltip: qsTr("Material emitted light color")
                enabled: !!root.materialData.entityId
                onColorEdited: function(color) {
                    root.edit(root.materialData.baseColor, root.materialData.roughness,
                              root.materialData.metalness, color)
                }
            }
            Binding {
                target: emissiveField
                property: "color"
                value: root.materialData.emissiveColor || Tk.Theme.color.bg
            }
        }
    }
}
