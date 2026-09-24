// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Combined graphics slice (ENG-03, docs/raster-document.md#canvas-placement):
// a tiled raster with a seam-crossing stroke and a mask is placed on a
// CanvasDocument above a vector star, rotated as a canvas object, painted
// through the canvas view and published as one canvas step per stroke. The
// comparison pairs the placement-time raster with the current one by revision.
Item {
    id: root
    width: 1180
    height: 760
    property real rasterObjectId: 0
    property real angle: -12
    property string baselineDataUrl: ""
    property int baselineRevision: 0
    property string statusText: ""
    readonly property var brush: ({diameter: 18, hardness: 0.85, spacing: 0.15, opacity: 1,
                                  pressureSize: 0.5, pressureOpacity: 0.2})
    readonly property var maskBrush: ({diameter: 26, hardness: 1, spacing: 0.2, opacity: 1,
                                      pressureSize: 0, pressureOpacity: 0})

    Tk.TiledRasterDocument { id: raster }
    Tk.CanvasDocument { id: canvas; objectName: "rasterCanvasDocument"; pageRect: Qt.rect(0, 0, 720, 460) }

    function stroke(color, points) {
        if (!raster.beginStroke("ink", false, color, root.brush)) return false
        for (const p of points) raster.sampleStroke(p[0], p[1], p[2], p[3], p[4])
        return raster.endStroke()
    }
    // Rotation about the raster centre; the canvas edit is one undo step.
    function rotate(degrees) {
        const radians = degrees * Math.PI / 180, c = Math.cos(radians), s = Math.sin(radians)
        const cx = raster.width / 2, cy = raster.height / 2, centerX = 60 + cx, centerY = 40 + cy
        if (canvas.setObjectTransform(root.rasterObjectId,
                [c, s, -s, c, centerX - (c * cx - s * cy), centerY - (s * cx + c * cy)]))
            root.angle = degrees
    }
    function publish() {
        if (!raster.publishToCanvas(canvas, root.rasterObjectId)) { root.statusText = raster.error; return false }
        const key = root.baselineRevision + ":" + raster.revision
        comparison.revisionKey = key
        comparison.setSourcePair(root.baselineDataUrl, raster.imageDataUrl, key)
        root.statusText = qsTr("Stroke published as one canvas step · canvas revision %1").arg(canvas.revision)
        return true
    }
    function setup() {
        raster.resizeCanvas(360, 200)
        raster.createLayer("ink", "Ink")
        raster.addMask("ink")
        raster.updateBrushPreset(root.brush)
        stroke("#44d9d0", [[30, 150, 0.5, -10, 4], [200, 60, 1, 24, -12], [340, 120, 0.8, 8, 16]])
        raster.beginStroke("ink", true, "#000000", root.maskBrush)
        raster.sampleStroke(248, 86); raster.sampleStroke(270, 98)
        raster.endStroke()
        canvas.addParametricShape("star", 380, 150, 260, 260, {points: 5, innerRadius: 0.45},
                                  "#c52d61", "#f5c869", 3)
        root.rasterObjectId = raster.placeOnCanvas(canvas, 60, 40)
        root.baselineDataUrl = raster.imageDataUrl
        root.baselineRevision = raster.revision
        rotate(root.angle)
        stroke("#e9aa69", [[40, 40, 0.7, 0, 0], [320, 170, 1, 0, 0]])
        if (publish()) root.statusText = qsTr("Placed, rotated and published one stroke · canvas revision %1").arg(canvas.revision)
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.md
        Tk.Flex {
            width: parent.width
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Raster on the canvas") }
                Tk.Caption { text: qsTr("Tiled strokes and mask · vector star · object transform · ICC-ready canvas output"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button { objectName: "rasterCanvasRotate"; text: qsTr("Rotate"); iconName: "rotate-cw"; tooltip: qsTr("Rotate the raster object 15 degrees as one canvas step"); onClicked: root.rotate(root.angle + 15) }
            Tk.Button { objectName: "rasterCanvasUndo"; text: qsTr("Undo"); iconName: "undo-2"; enabled: canvas.canUndo; tooltip: qsTr("Undo the last canvas step: placement, rotation or published stroke"); onClicked: canvas.undo() }
            Tk.Button { objectName: "rasterCanvasRedo"; text: qsTr("Redo"); iconName: "redo-2"; enabled: canvas.canRedo; tooltip: qsTr("Redo the next canvas step"); onClicked: canvas.redo() }
            Tk.Button {
                objectName: "rasterCanvasExport"
                text: qsTr("Export PNG")
                iconName: "download"
                tooltip: qsTr("Write the raster composite as an sRGB-tagged PNG")
                onClicked: {
                    const path = "/tmp/qindatk-raster-canvas-" + Date.now() + ".png"
                    root.statusText = raster.exportPng(path) ? qsTr("Wrote %1").arg(path) : raster.error
                }
            }
        }
        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1.35fr) minmax(0, 1fr)"
            columnGap: Tk.Theme.space.md
            Tk.Panel {
                title: qsTr("Canvas · paint on the rotated raster")
                iconName: "paintbrush"
                Tk.CanvasView {
                    id: canvasView
                    objectName: "rasterCanvasView"
                    anchors.fill: parent
                    viewId: "raster-canvas"
                    document: canvas
                    fitMode: "page"
                    accessibleName: qsTr("Canvas with a placed raster and a vector star")
                    Tk.RasterStrokeInput {
                        objectName: "rasterCanvasInput"
                        anchors.fill: parent
                        document: raster
                        layerId: "ink"
                        color: "#7fb4ff"
                        brushPreset: root.brush
                        pointMapper: function(point) {
                            const d = canvasView.mapToDocument(point)
                            return raster.canvasToRaster(canvas, root.rasterObjectId, d.x, d.y)
                        }
                        onStrokeCommitted: root.publish()
                        onStrokeFailed: function(message) { root.statusText = message }
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Placement-time / current raster")
                iconName: "columns-2"
                Tk.CompareView {
                    id: comparison
                    objectName: "rasterCanvasComparison"
                    anchors.fill: parent
                }
            }
        }
        Tk.Caption { objectName: "rasterCanvasStatus"; text: root.statusText; color: Tk.Theme.color.textMuted }
    }
    Component.onCompleted: setup()
}
