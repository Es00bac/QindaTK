// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 1080
    height: 680
    property string statusText: qsTr("A deterministic pressure stroke spans the 256 px tile seam.")
    readonly property string statePath: "/tmp/qindatk-tiled-raster-document.qtraster.json"
    readonly property var brush: ({diameter: 34, hardness: 0.84, spacing: 0.16, opacity: 0.96,
                                  pressureSize: 0.48, pressureOpacity: 0.3})
    readonly property var maskBrush: ({diameter: 30, hardness: 1, spacing: 0.2, opacity: 1,
                                      pressureSize: 0, pressureOpacity: 0})

    Tk.TiledRasterDocument { id: document }

    function paintDemoStroke() {
        if (!document.beginStroke("ink", false, "#44d9d0", root.brush)) {
            root.statusText = document.error
            return
        }
        document.sampleStroke(120, 168, 0.45, -8, 3)
        document.sampleStroke(300, 145, 0.95, 32, -18)
        document.sampleStroke(498, 187, 0.72, 14, 24)
        if (!document.endStroke()) { root.statusText = document.error; return }
        if (!document.beginStroke("ink", true, "#000000", root.maskBrush)) { root.statusText = document.error; return }
        document.sampleStroke(298, 145, 1, 0, 0)
        document.sampleStroke(320, 145, 1, 0, 0)
        if (document.endStroke()) root.statusText = qsTr("Painted across tile seams and masked a segment · revision %1").arg(document.revision)
        else root.statusText = document.error
    }
    function exportDemo() {
        const path = "/tmp/qindatk-tiled-raster-" + Date.now() + ".png"
        root.statusText = document.exportPng(path) ? qsTr("PNG written · %1").arg(path) : document.error
    }
    function saveDemo() {
        root.statusText = document.saveToFile(root.statePath)
            ? qsTr("Saved tiled pixels and mask state · %1").arg(root.statePath) : document.error
    }
    function reopenDemo() {
        root.statusText = document.loadFromFile(root.statePath)
            ? qsTr("Reopened %1 · history is clean: %2").arg(root.statePath).arg(!document.canUndo) : document.error
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.md
        Tk.Flex {
            width: parent.width
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Tiled raster paint") }
                Tk.Caption { text: qsTr("256 px tiles · deterministic brush dynamics · layer mask · one-stroke undo"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button { text: qsTr("Paint stroke"); iconName: "brush"; tooltip: qsTr("Commit a deterministic pressure and tilt stroke"); onClicked: root.paintDemoStroke() }
            Tk.Button { text: qsTr("Undo"); iconName: "undo-2"; enabled: document.canUndo; tooltip: qsTr("Undo the most recent stroke as one edit"); onClicked: { document.undo(); root.statusText = qsTr("Stroke undone") } }
            Tk.Button { text: qsTr("Export PNG"); iconName: "download"; tooltip: qsTr("Atomically export the composited image"); onClicked: root.exportDemo() }
            Tk.Button { text: qsTr("Save State"); iconName: "save"; tooltip: qsTr("Atomically save tiled pixels, masks and brush preset"); onClicked: root.saveDemo() }
            Tk.Button { text: qsTr("Reopen"); iconName: "folder-open"; tooltip: qsTr("Reload the saved raster document and begin a clean undo history"); onClicked: root.reopenDemo() }
        }
        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1fr) minmax(240px, 0.45fr)"
            columnGap: Tk.Theme.space.md
            Tk.Panel {
                title: qsTr("Raster canvas")
                iconName: "image"
                Item {
                    anchors.fill: parent
                    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas; radius: Tk.Theme.radius.sm }
                    Item {
                        id: canvasSurface
                        anchors.centerIn: parent
                        width: Math.min(parent.width - Tk.Theme.space.lg, 640)
                        height: Math.min(parent.height - Tk.Theme.space.lg, 320)
                        Image {
                            anchors.fill: parent
                            fillMode: Image.Stretch
                            source: document.imageDataUrl
                            cache: false
                            Accessible.role: Accessible.Graphic
                            Accessible.name: qsTr("Tiled raster image with a teal brush stroke and a transparent masked segment")
                        }
                        Tk.RasterStrokeInput {
                            anchors.fill: parent
                            document: document
                            layerId: "ink"
                            color: "#44d9d0"
                            brushPreset: root.brush
                            onStrokeCommitted: root.statusText = qsTr("Canvas stroke committed · revision %1").arg(document.revision)
                            onStrokeCanceled: root.statusText = qsTr("Canvas stroke canceled")
                            onStrokeFailed: function(message) { root.statusText = message }
                        }
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Document")
                iconName: "layers"
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    padding: Tk.Theme.space.md
                    gap: Tk.Theme.space.sm
                    Tk.KeyValue { key: qsTr("Canvas"); value: qsTr("%1 × %2 px").arg(document.width).arg(document.height) }
                    Tk.KeyValue { key: qsTr("Tile grid"); value: qsTr("3 × 2 · 256 px") }
                    Tk.KeyValue { key: qsTr("Stable layer"); value: "ink" }
                    Tk.KeyValue { key: qsTr("Mask"); value: qsTr("8-bit coverage") }
                    Tk.KeyValue { key: qsTr("Saved brush"); value: qsTr("%1 px · %2% opacity").arg(document.brushPreset.diameter.toFixed(0)).arg((document.brushPreset.opacity * 100).toFixed(0)) }
                    Tk.Divider {}
                    Tk.Caption { text: qsTr("Layer pixels use premultiplied RGBA. The optional grayscale mask attenuates alpha during compositing."); wrapMode: Text.Wrap; color: Tk.Theme.color.textMuted }
                    Tk.Spacer { Tk.Flex.grow: 1 }
                    Tk.Caption { objectName: "rasterStatus"; text: root.statusText; wrapMode: Text.Wrap; color: document.error.length ? Tk.Theme.color.warning : Tk.Theme.color.text }
                }
            }
        }
    }
    Component.onCompleted: {
        document.resizeCanvas(640, 320)
        document.createLayer("ink", "Ink")
        document.addMask("ink")
        root.paintDemoStroke()
        root.exportDemo()
        root.saveDemo()
    }
}
