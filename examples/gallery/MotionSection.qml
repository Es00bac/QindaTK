// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    id: section
    title: "Motion and keyframes"
    note: "AutomationModel · MotionComposition · KeyframeTimeline · DopeSheet · EffectBrowser"
    property string message: qsTr("Host-owned motion sample")
    property var effectRows: [
        {effectId: "position-ease", label: "Position Ease", categoryId: "motion", category: "Motion", description: "Ease animated transforms.", compatible: true},
        {effectId: "depth-blur", label: "Depth Blur", categoryId: "blur", category: "Blur", description: "Requires a depth map.", compatible: false, reason: "No depth map is present."}]
    Tk.AutomationModel {
        id: model
        Component.onCompleted: {
            model.addParameterMap({id: "gallery/position/x", name: "Position X", dimension: "length", unitId: "px",
                minimum: 0, maximum: 320, default: 24, interpolation: "linear"})
            model.applyEdit({kind: "addPoint", parameterId: "gallery/position/x"}, {ticks: "0", value: 24})
            model.applyEdit({kind: "addPoint", parameterId: "gallery/position/x"}, {ticks: "141120000", value: 240})
        }
    }
    GalleryRow {
        label: "Keyframe keys"
        Item {
            implicitWidth: 580
            implicitHeight: keys.implicitHeight
            Tk.KeyframeTimeline {
                id: keys
                anchors.fill: parent
                model: model
                parameterIds: ["gallery/position/x"]
                selectedParameterId: "gallery/position/x"
                timeBoundaries: ["0", "70560000", "141120000"]
                playheadTicks: "70560000"
                onEditCommitted: function(id) { section.message = qsTr("Key transaction %1").arg(id) }
            }
        }
    }
    GalleryRow {
        label: "Exposure holds"
        Item {
            implicitWidth: 520
            implicitHeight: sheet.implicitHeight
            Tk.DopeSheet {
                id: sheet
                anchors.fill: parent
                layers: [{layerId: "drawings", label: "Drawings", exposures: [
                    {exposureId: "exposure-1", startTicks: "0", endTicks: "70560000", artworkId: "art-a", label: "A"},
                    {exposureId: "exposure-2", startTicks: "70560000", endTicks: "141120000", artworkId: "art-a", label: "HOLD", held: true}]}]
                frameBoundaries: ["0", "70560000", "141120000"]
                selectedLayerId: "drawings"
                playheadTicks: "0"
                statusText: section.message
                onExposureEditRequested: function(id, action, layerId) { section.message = qsTr("Host exposure request %1 · %2 · %3").arg(id).arg(action).arg(layerId) }
            }
        }
    }
    GalleryRow {
        label: "Effect registry"
        Item {
            implicitWidth: 420
            implicitHeight: browser.implicitHeight
            Tk.EffectBrowser {
                id: browser
                anchors.fill: parent
                effects: section.effectRows
                categories: [{label: "All", value: "all"}, {label: "Motion", value: "motion"}, {label: "Blur", value: "blur"}]
                targetLayerId: "layer-1"
                documentId: "gallery-document"
                revision: "4"
                selectedEffectId: "position-ease"
                statusText: section.message
                onEffectInsertRequested: function(id, effectId, targetId) { section.message = qsTr("Host insertion request %1 · %2 → %3").arg(id).arg(effectId).arg(targetId) }
            }
        }
    }
    Tk.Caption { text: section.message; color: Tk.Theme.color.textMuted; wrapMode: Text.WordWrap }
}
