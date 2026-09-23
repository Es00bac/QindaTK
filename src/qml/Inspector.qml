// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Schema driven property view. Each descriptor supplies label, binding,
// editorType (number/vector/text), units and optional group metadata.
Item {
    id: inspector
    property var properties: []
    property string filterText: ""
    property string tooltip: ""
    property real labelWidth: Tk.Theme.size.labelWidth
    readonly property int visibleCount: repeater.count
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip || qsTr("Inspector")
    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Repeater {
            id: repeater
            model: inspector.properties
            delegate: Tk.PropertyRow {
                objectName: "inspectorRow_" + (modelData.id || index)
                width: inspector.width
                visible: !inspector.filterText.length || String(modelData.label || "").toLowerCase().includes(inspector.filterText.toLowerCase())
                label: modelData.label || modelData.id || ""
                hint: modelData.helpText || ""
                Tk.MixedValueField {
                    visible: modelData.editorType === "number" || !modelData.editorType
                    binding: modelData.binding || null
                    label: modelData.label || ""
                    tooltip: modelData.tooltip || modelData.label || ""
                    Tk.UnitField {
                        displayUnit: modelData.displayUnit || ""
                        units: modelData.units || []
                        unitScale: modelData.unitScale === undefined ? 1 : modelData.unitScale
                        unitOffset: modelData.unitOffset === undefined ? 0 : modelData.unitOffset
                        precision: modelData.precision === undefined ? 2 : modelData.precision
                        stepSize: modelData.stepSize === undefined ? 1 : modelData.stepSize
                        binding: modelData.binding || null
                        label: modelData.label || ""
                        tooltip: modelData.tooltip || modelData.label || ""
                    }
                }
                Tk.MixedValueField {
                    visible: modelData.editorType === "vector"
                    binding: modelData.binding || null
                    label: modelData.label || ""
                    tooltip: modelData.tooltip || modelData.label || ""
                    Tk.VectorField {
                    componentLabels: modelData.componentLabels || ["X", "Y", "Z"]
                    componentUnits: modelData.componentUnits || []
                    tooltip: modelData.tooltip || modelData.label || ""
                    binding: modelData.binding || null
                    }
                }
                Tk.MixedValueField {
                    visible: modelData.editorType === "text"
                    binding: modelData.binding || null
                    label: modelData.label || ""
                    tooltip: modelData.tooltip || modelData.label || ""
                    Tk.TextField {
                        enabled: modelData.binding ? modelData.binding.writable : false
                        text: modelData.binding && !modelData.binding.mixed ? String(modelData.binding.value) : ""
                        placeholderText: modelData.binding && modelData.binding.mixed ? qsTr("Multiple values") : ""
                        tooltip: modelData.tooltip || modelData.label || ""
                        onEditingFinished: {
                            if (modelData.binding && modelData.binding.begin()) {
                                if (modelData.binding.update(text)) modelData.binding.commit()
                                else modelData.binding.cancel()
                            }
                        }
                    }
                }
            }
        }
    }
}
