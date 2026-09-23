// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Axes and features are supplied by the shaping/provider layer. Unsupported
// metadata remains visible and disabled instead of being guessed.
T.Control {
    id: rootItem
    property var fontMetadata: null
    property var axisBindings: ({})
    property var featureBindings: ({})
    property string fontFamily: ""
    property string fontStyle: ""
    property string tooltip: ""
    hoverEnabled: true
    readonly property bool metadataAvailable: !!fontMetadata && fontMetadata.available === true
    readonly property bool axisMetadataAvailable: metadataAvailable && fontMetadata.axisMetadataAvailable === true
    readonly property bool featureMetadataAvailable: metadataAvailable && fontMetadata.featureMetadataAvailable === true
    readonly property var axes: axisMetadataAvailable && fontMetadata.axes ? fontMetadata.axes : []
    readonly property var features: featureMetadataAvailable && fontMetadata.features ? fontMetadata.features : []

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip || qsTr("Font features")
    Accessible.description: !metadataAvailable ? qsTr("Font metadata unavailable")
        : (!axisMetadataAvailable || !featureMetadataAvailable) ? qsTr("Font axes or feature support metadata unavailable")
        : fontFamily + " " + fontStyle

    function updateBinding(binding, value) {
        if (!binding || !binding.writable || !binding.begin()) return false
        if (!binding.update(value)) { binding.cancel(); return false }
        return binding.commit()
    }
    function bindingFor(entry, map) { return entry.binding || map[String(entry.tag || "")] || null }
    Tk.ToolTip { text: rootItem.tooltip; visible: text.length > 0 && rootItem.hovered }
    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Caption {
            objectName: "fontMetadataUnavailable"
            visible: !rootItem.axisMetadataAvailable && !rootItem.featureMetadataAvailable
            text: !rootItem.metadataAvailable ? qsTr("Font metadata unavailable") : qsTr("Font axes and feature support metadata unavailable")
            color: Tk.Theme.color.textMuted
        }
        Repeater {
            model: rootItem.axes
            delegate: Tk.PropertyRow {
                required property var modelData
                objectName: "fontAxis_" + (modelData.tag || index)
                label: modelData.name || modelData.tag || qsTr("Axis")
                hint: modelData.supported === false ? qsTr("Unsupported by selected font") : (modelData.tag || "")
                Tk.UnitField {
                    enabled: modelData.supported !== false
                    binding: rootItem.bindingFor(modelData, rootItem.axisBindings)
                    label: modelData.tag || modelData.name || ""
                    tooltip: (modelData.name || modelData.tag || qsTr("Font axis")) + (modelData.tag ? " · " + modelData.tag : "")
                    from: modelData.minimum === undefined ? -1e6 : Number(modelData.minimum)
                    to: modelData.maximum === undefined ? 1e6 : Number(modelData.maximum)
                    precision: modelData.precision === undefined ? 2 : Number(modelData.precision)
                    stepSize: modelData.step === undefined ? 1 : Number(modelData.step)
                }
            }
        }
        Repeater {
            model: rootItem.features
            delegate: Tk.CheckBox {
                required property var modelData
                objectName: "fontFeature_" + (modelData.tag || index)
                text: modelData.name || modelData.tag || qsTr("OpenType feature")
                tooltip: (modelData.tag || "") + (modelData.supported === false ? " · " + qsTr("Unsupported by selected font") : "")
                enabled: rootItem.metadataAvailable && modelData.supported !== false
                         && rootItem.bindingFor(modelData, rootItem.featureBindings)
                         && rootItem.bindingFor(modelData, rootItem.featureBindings).writable
                checked: rootItem.bindingFor(modelData, rootItem.featureBindings)
                         ? Boolean(rootItem.bindingFor(modelData, rootItem.featureBindings).value) : Boolean(modelData.value)
                onClicked: rootItem.updateBinding(rootItem.bindingFor(modelData, rootItem.featureBindings), checked)
            }
        }
    }
}
