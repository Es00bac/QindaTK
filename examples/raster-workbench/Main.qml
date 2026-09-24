// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 1200
    height: 850
    property string beforeDataUrl: ""
    property string targetId: "paint"
    property string selectedPresetId: "soft"
    property string statusText: qsTr("Drag on the image to paint one undoable stroke.")
    property var channelRows: [
        {id: "paint", name: "Paint", kind: "layer", enabled: true, visible: true},
        {id: "paint-mask", name: "Coverage mask", kind: "mask", enabled: true, visible: true}
    ]
    property var presetRows: [
        {id: "soft", name: "Soft round", tags: ["paint", "pressure"], previewColor: "#40c9ca", available: true},
        {id: "hard", name: "Hard round", tags: ["ink", "edge"], previewColor: "#e9aa69", available: true}
    ]
    property var dynamicsRows: [
        {id: "diameter", label: "Diameter", value: 30, minimum: 8, maximum: 60, step: 1, unit: "px"},
        {id: "hardness", label: "Hardness", value: 0.8, minimum: 0, maximum: 1, step: 0.01},
        {id: "opacity", label: "Opacity", value: 0.95, minimum: 0, maximum: 1, step: 0.01},
        {id: "pressureSize", label: "Pressure size", value: 0.5, minimum: 0, maximum: 1, step: 0.01}
    ]

    Tk.TiledRasterDocument { id: document }

    function brush() {
        const values = {}
        for (const row of dynamicsRows) values[row.id] = row.value
        return Object.assign({spacing: 0.18, pressureOpacity: 0.3}, values)
    }
    function paintInitial() {
        document.resizeCanvas(640, 320)
        document.createLayer("paint", "Paint")
        document.addMask("paint")
        document.beginStroke("paint", false, "#40c9ca", brush())
        document.sampleStroke(90, 220, 0.7, -12, 6)
        document.sampleStroke(310, 90, 1, 20, -9)
        document.sampleStroke(560, 180, 0.8, 9, 12)
        document.endStroke()
        beforeDataUrl = document.imageDataUrl
        document.beginStroke("paint", false, "#e9aa69", brush())
        document.sampleStroke(135, 100, 0.7, 5, 0)
        document.sampleStroke(460, 238, 0.9, 19, 8)
        document.endStroke()
    }
    function updateChannel(channelId, propertyName, value) {
        channelRows = channelRows.map(function(row) {
            return row.id === channelId ? Object.assign({}, row, {[propertyName]: value}) : row
        })
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.md
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Raster painting components") }
                Tk.Caption { text: qsTr("Tiled pixels · live controls · synchronized comparison"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button { text: qsTr("Undo"); tooltip: qsTr("Undo the last stroke"); enabled: document.canUndo; onClicked: document.undo() }
            Tk.Button { text: qsTr("Redo"); tooltip: qsTr("Redo the last stroke"); enabled: document.canRedo; onClicked: document.redo() }
            Tk.Button {
                text: qsTr("Export PNG")
                tooltip: qsTr("Write the current raster composite")
                onClicked: {
                    const path = "/tmp/qindatk-raster-workbench-" + Date.now() + ".png"
                    root.statusText = document.exportPng(path) ? qsTr("Wrote %1").arg(path) : document.error
                }
            }
        }
        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1.3fr) minmax(0, 0.9fr)"
            columnGap: Tk.Theme.space.md
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.Panel {
                    title: qsTr("Paint surface")
                    iconName: "paintbrush"
                    Tk.Flex.grow: 1
                    Item {
                        anchors.fill: parent
                        Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
                        Image {
                            id: canvasImage
                            anchors.centerIn: parent
                            width: 640
                            height: 320
                            fillMode: Image.PreserveAspectFit
                            source: document.imageDataUrl
                            cache: false
                            Accessible.role: Accessible.Graphic
                            Accessible.name: qsTr("Editable tiled raster canvas")
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.CrossCursor
                                onPressed: function(mouse) {
                                    root.beforeDataUrl = document.imageDataUrl
                                    if (document.beginStroke("paint", root.targetId === "paint-mask",
                                                             root.selectedPresetId === "hard" ? "#e9aa69" : "#40c9ca", root.brush()))
                                        document.sampleStroke(mouse.x, mouse.y)
                                }
                                onPositionChanged: function(mouse) {
                                    if (pressed) document.sampleStroke(mouse.x, mouse.y)
                                }
                                onReleased: {
                                    if (!document.endStroke()) root.statusText = document.error
                                    else root.statusText = qsTr("Committed stroke · revision %1").arg(document.revision)
                                }
                                onCanceled: document.cancelStroke()
                            }
                        }
                    }
                }
                Tk.Panel {
                    title: qsTr("Before / after pixels")
                    iconName: "scan-eye"
                    height: 285
                    Tk.CompareView {
                        objectName: "rasterComparison"
                        anchors.fill: parent
                        beforeSource: root.beforeDataUrl
                        afterSource: document.imageDataUrl
                    }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.Panel {
                    title: qsTr("Layers and mask")
                    iconName: "layers"
                    height: 195
                    Tk.MaskChannelPanel {
                        objectName: "rasterMaskControls"
                        anchors.fill: parent
                        documentId: "raster-workbench"
                        channels: root.channelRows
                        activeTargetId: root.targetId
                        onTargetRequested: function(requestId, documentId, targetId) {
                            root.targetId = targetId
                            acknowledge(requestId, "accepted", qsTr("Paint target changed"))
                        }
                        onChannelStateRequested: function(requestId, documentId, channelId, propertyName, value) {
                            if (channelId !== "paint-mask" || propertyName !== "enabled") {
                                acknowledge(requestId, "rejected", qsTr("This document exposes mask enable only"))
                                return
                            }
                            if (document.setMaskEnabled("paint", value)) {
                                root.updateChannel(channelId, propertyName, value)
                                acknowledge(requestId, "accepted", qsTr("Mask state changed"))
                            } else acknowledge(requestId, "rejected", document.error)
                        }
                        onRelationshipChangeRequested: function(requestId) {
                            acknowledge(requestId, "rejected", qsTr("Clipping is not available in this raster document"))
                        }
                    }
                }
                Tk.Panel {
                    title: qsTr("Brush presets")
                    iconName: "brush"
                    height: 205
                    Tk.BrushPresetView {
                        anchors.fill: parent
                        presets: root.presetRows
                        selectedPresetId: root.selectedPresetId
                        onPresetRequested: function(requestId, presetId, action) {
                            if (action === "select") root.selectedPresetId = presetId
                            else if (action === "duplicate") {
                                const existing = root.presetRows.find(function(p) { return p.id === presetId })
                                root.presetRows = root.presetRows.concat([Object.assign({}, existing,
                                    {id: presetId + "-copy", name: existing.name + " copy"})])
                            }
                            acknowledge(requestId, "accepted", qsTr("Preset updated"))
                        }
                    }
                }
                Tk.Panel {
                    title: qsTr("Brush dynamics")
                    iconName: "sliders-horizontal"
                    Tk.Flex.grow: 1
                    Tk.BrushDynamicsEditor {
                        anchors.fill: parent
                        brushId: root.selectedPresetId
                        parameters: root.dynamicsRows
                        onDynamicsEditRequested: function(brushId, requestId, phase, parameters) {
                            if (phase === "commit") root.dynamicsRows = parameters
                            acknowledge(requestId, "accepted", qsTr("Brush %1 acknowledged").arg(phase))
                        }
                    }
                }
            }
        }
        Tk.Caption { objectName: "rasterWorkbenchStatus"; text: root.statusText; color: Tk.Theme.color.textMuted }
    }
    Component.onCompleted: paintInitial()
}
