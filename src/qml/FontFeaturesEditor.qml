// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// FontCatalog supplies face-level OpenType metadata; a host binding supplies
// editable formatting state. Glyph/script shaping support is not inferred.
T.Control {
    id: rootItem
    property var fontMetadata: null
    property var fontProvider: null
    property var axisBindings: ({})
    property var featureBindings: ({})
    property string fontFamily: ""
    property string fontStyle: ""
    property string tooltip: ""
    hoverEnabled: true
    readonly property var effectiveFontMetadata: fontMetadata || (fontProvider && fontProvider.families && fontProvider.faceMetadata
        ? fontProvider.faceMetadata(fontFamily, fontStyle) : null)
    readonly property bool metadataAvailable: !!effectiveFontMetadata && effectiveFontMetadata.available === true
    readonly property bool axisMetadataAvailable: metadataAvailable && effectiveFontMetadata.axisMetadataAvailable === true
    readonly property bool featureMetadataAvailable: metadataAvailable && effectiveFontMetadata.featureMetadataAvailable === true
    readonly property var axes: axisMetadataAvailable && effectiveFontMetadata.axes ? effectiveFontMetadata.axes : []
    readonly property var features: featureMetadataAvailable && effectiveFontMetadata.features ? effectiveFontMetadata.features : []

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
        if (!binding.commit()) { binding.cancel(); return false }
        return true
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
                id: axisRow
                required property var modelData
                readonly property var axisBinding: rootItem.bindingFor(modelData, rootItem.axisBindings)
                objectName: "fontAxis_" + (modelData.tag || index)
                label: modelData.name || modelData.tag || qsTr("Axis")
                hint: modelData.supported === false ? qsTr("Unsupported by selected font") : ""
                Tk.UnitField {
                    anchors.fill: parent
                    visible: !!axisRow.axisBinding
                    enabled: modelData.supported !== false
                    binding: axisRow.axisBinding
                    label: modelData.tag || modelData.name || ""
                    tooltip: (modelData.name || modelData.tag || qsTr("Font axis")) + (modelData.tag ? " · " + modelData.tag : "")
                    from: modelData.minimum === undefined ? -1e6 : Number(modelData.minimum)
                    to: modelData.maximum === undefined ? 1e6 : Number(modelData.maximum)
                    precision: modelData.precision === undefined ? 2 : Number(modelData.precision)
                    stepSize: modelData.step === undefined ? 1 : Number(modelData.step)
                }
                Tk.Caption {
                    anchors.fill: parent
                    visible: !axisRow.axisBinding
                    text: qsTr("Default %1 · range %2–%3")
                        .arg(Number(modelData.default)).arg(Number(modelData.minimum))
                        .arg(Number(modelData.maximum))
                    color: Tk.Theme.color.textMuted
                }
            }
        }
        Repeater {
            model: rootItem.features
            delegate: Tk.Flex {
                id: featureRow
                required property int index
                required property var modelData
                readonly property var featureBinding: rootItem.bindingFor(modelData, rootItem.featureBindings)
                objectName: "fontFeatureRow_" + (modelData.tag || index)
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.CheckBox {
                    objectName: "fontFeature_" + (featureRow.modelData.tag || featureRow.index)
                    text: featureRow.modelData.name || featureRow.modelData.tag || qsTr("OpenType feature")
                    tooltip: (featureRow.modelData.tag || "")
                        + (featureRow.modelData.supported === false ? " · " + qsTr("Unsupported by selected font") : "")
                        + (!featureRow.featureBinding ? " · " + qsTr("Current setting unavailable without a formatting target") : "")
                        + (featureRow.featureBinding && featureRow.featureBinding.mixed ? " · " + qsTr("Mixed values") : "")
                    enabled: rootItem.metadataAvailable && featureRow.modelData.supported !== false
                             && featureRow.featureBinding && featureRow.featureBinding.writable
                    tristate: !featureRow.featureBinding || featureRow.featureBinding.mixed
                    checkState: featureRow.featureBinding
                        ? featureRow.featureBinding.mixed ? Qt.PartiallyChecked
                          : Boolean(featureRow.featureBinding.value) ? Qt.Checked : Qt.Unchecked
                        : Qt.PartiallyChecked
                    Accessible.description: tooltip
                    onClicked: rootItem.updateBinding(featureRow.featureBinding, checkState === Qt.Checked)
                }
                Tk.Button {
                    objectName: "fontFeatureReset_" + (featureRow.modelData.tag || featureRow.index)
                    visible: featureRow.modelData.supported !== false && featureRow.featureBinding !== null
                             && featureRow.featureBinding.writable
                             && (featureRow.featureBinding.mixed
                                 || Boolean(featureRow.featureBinding.value) !== Boolean(featureRow.featureBinding.defaultValue))
                    text: qsTr("Reset")
                    small: true
                    variant: "ghost"
                    tooltip: qsTr("Reset %1 to its default").arg(featureRow.modelData.name || featureRow.modelData.tag)
                    Accessible.name: tooltip
                    onClicked: if (featureRow.featureBinding) featureRow.featureBinding.reset()
                }
            }
        }
    }
}
