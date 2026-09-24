// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    id: section
    title: "Raster"
    note: "Mask and brush controls · CompareView (docs/raster-document.md, docs/compare.md)"
    property string selectedPreset: "round"
    property string targetId: "paint"
    property var brushParameters: [
        {id: "diameter", label: "Diameter", value: 24, minimum: 4, maximum: 64, step: 1, unit: "px"},
        {id: "hardness", label: "Hardness", value: 0.8, minimum: 0, maximum: 1, step: 0.01}
    ]

    GalleryRow {
        label: "Mask and brush"
        Tk.MaskChannelPanel {
            width: 310
            height: 155
            documentId: "gallery-raster"
            channels: [
                {id: "paint", name: "Paint", kind: "layer", enabled: true, visible: true},
                {id: "mask", name: "Mask", kind: "mask", enabled: true, visible: true}
            ]
            activeTargetId: section.targetId
            onTargetRequested: function(requestId, documentId, targetId) {
                section.targetId = targetId
                acknowledge(requestId, "accepted")
            }
            onChannelStateRequested: function(requestId) { acknowledge(requestId, "rejected", qsTr("Gallery snapshot")) }
            onRelationshipChangeRequested: function(requestId) { acknowledge(requestId, "rejected", qsTr("Gallery snapshot")) }
        }
        Tk.BrushPresetView {
            width: 310
            height: 155
            presets: [
                {id: "round", name: "Round", tags: ["paint"], available: true},
                {id: "ink", name: "Ink", tags: ["hard"], available: true}
            ]
            selectedPresetId: section.selectedPreset
            onPresetRequested: function(requestId, presetId, action) {
                if (action === "select") section.selectedPreset = presetId
                acknowledge(requestId, action === "select" ? "accepted" : "rejected")
            }
        }
        Tk.BrushDynamicsEditor {
            width: 310
            height: 155
            brushId: section.selectedPreset
            parameters: section.brushParameters
            onDynamicsEditRequested: function(brushId, requestId, phase, parameters) {
                if (phase === "commit") section.brushParameters = parameters
                acknowledge(requestId, "accepted")
            }
        }
    }
    GalleryRow {
        label: "Image compare"
        Tk.CompareView {
            width: 430
            height: 260
            beforeSource: Qt.resolvedUrl("../image-compare/before.svg")
            afterSource: Qt.resolvedUrl("../image-compare/after.svg")
        }
    }
}
