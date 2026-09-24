// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.AppWindow {
    id: window
    title: qsTr("Mask and brush controls · host fixture")
    width: 1180
    height: 760
    property string documentId: "document-canvas-demo"
    property string activeTargetId: "layer-paint"
    property string selectedPresetId: "soft-round"
    property var channelSnapshot: [
        { id: "layer-paint", name: "Paint layer", kind: "layer", enabled: true, visible: true },
        { id: "mask-shadows", name: "Shadows", kind: "mask", enabled: true, visible: true },
        { id: "channel-alpha", name: "Alpha", kind: "channel", enabled: true, visible: false }
    ]
    property var presetSnapshot: [
        { id: "soft-round", name: "Soft round", tags: ["paint", "soft", "pressure"], previewColor: "#63d6c8", shape: "round", available: true },
        { id: "ink-pen", name: "Ink pen", tags: ["ink", "hard", "line"], previewColor: "#9ab5ff", shape: "round", available: true },
        { id: "grain-glaze", name: "Grain glaze", tags: ["texture", "wash"], previewColor: "#e4b173", shape: "grain", available: true }
    ]
    property var dynamicsSnapshot: [
        { id: "size", label: "Size from pressure", value: 0.72, minimum: 0, maximum: 1, step: 0.01, unit: "%" },
        { id: "opacity", label: "Opacity from pressure", value: 0.45, minimum: 0, maximum: 1, step: 0.01, unit: "%" },
        { id: "tilt", label: "Tilt response", value: 0.2, minimum: 0, maximum: 1, step: 0.01, unit: "%" }
    ]
    property string hostStatus: qsTr("Host snapshot loaded · preview requests do not alter the document")

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Mask and brush") }
                Tk.Caption { text: qsTr("Stable host snapshots · requests are acknowledged explicitly"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Badge { text: qsTr("Local fixture"); variant: "info" }
        }
        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 0.9fr) minmax(0, 1.1fr)"
            columnGap: Tk.Theme.space.md
            rowGap: Tk.Theme.space.md
            Tk.Panel {
                title: qsTr("Paint target"); iconName: "layers"
                Tk.MaskChannelPanel {
                    anchors.fill: parent
                    documentId: window.documentId
                    channels: window.channelSnapshot
                    activeTargetId: window.activeTargetId
                    onTargetRequested: function(requestId, docId, targetId) {
                        if (docId !== window.documentId) return
                        window.activeTargetId = targetId
                        acknowledge(requestId, "accepted", qsTr("Active paint target updated by host"))
                    }
                    onChannelStateRequested: function(requestId, docId, channelId, propertyName, value) {
                        const next = window.channelSnapshot.map(function(row) {
                            return row.id === channelId ? Object.assign({}, row, { [propertyName]: value }) : row
                        })
                        window.channelSnapshot = next
                        acknowledge(requestId, "accepted", qsTr("Host snapshot updated · %1").arg(propertyName))
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Brush dynamics"); iconName: "sliders-horizontal"
                Tk.BrushDynamicsEditor {
                    id: dynamics
                    anchors.fill: parent
                    brushId: window.selectedPresetId
                    parameters: window.dynamicsSnapshot
                    onDynamicsEditRequested: function(brushId, requestId, phase, parameters) {
                        if (phase === "commit") window.dynamicsSnapshot = parameters
                        acknowledge(requestId, "accepted", qsTr("Host acknowledged %1 · %2").arg(phase).arg(brushId))
                        window.hostStatus = qsTr("Last request: %1").arg(phase)
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Available presets"); iconName: "paintbrush"
                Tk.BrushPresetView {
                    anchors.fill: parent
                    presets: window.presetSnapshot
                    selectedPresetId: window.selectedPresetId
                    onPresetRequested: function(requestId, presetId, action) {
                        if (action === "select") window.selectedPresetId = presetId
                        acknowledge(requestId, "accepted", qsTr("Host acknowledged %1 for %2").arg(action).arg(presetId))
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Host contract"); iconName: "info"
                Tk.Flex {
                    anchors.fill: parent
                    padding: Tk.Theme.space.md
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Tk.Label { text: qsTr("Target: %1").arg(window.activeTargetId) }
                    Tk.Label { text: qsTr("Brush: %1").arg(window.selectedPresetId) }
                    Tk.Caption { text: window.hostStatus; color: Tk.Theme.color.textMuted; wrapMode: Text.Wrap }
                    Tk.Notice { variant: "warning"; title: qsTr("Presentation only"); text: qsTr("The host owns mask coverage, channels, preset storage, stroke rendering, persistence and undo. These controls send state requests only.") }
                }
            }
        }
    }
}
