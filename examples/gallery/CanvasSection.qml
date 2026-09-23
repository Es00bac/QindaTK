// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    id: section
    title: "Canvas"
    note: "CanvasView · CanvasDocument · ToolController · CanvasViewState (docs/canvas.md)"

    // Demo data only: shapes are document values, not chrome.
    Tk.CanvasDocument {
        id: sketch
        pageRect: Qt.rect(0, 0, 240, 140)
        Component.onCompleted: {
            addRect(16, 16, 90, 60, "#e0564f", "#3a1414", 2)
            addEllipse(80, 40, 110, 70, "#4fa3d9", "#12303f", 2)
            addPolyline([Qt.point(20, 120), Qt.point(90, 90), Qt.point(150, 125), Qt.point(220, 80)], "#2a2a2a", 3)
        }
    }
    Tk.ToolController { id: sketchTools; document: sketch }
    Tk.CanvasViewState { id: sketchPose; rotation: -8 }

    GalleryRow {
        label: "CanvasView"
        Tk.CanvasView {
            objectName: "galleryCanvas"
            implicitWidth: 300
            implicitHeight: 180
            document: sketch
            toolController: sketchTools
            viewState: sketchPose
            fitMode: "page"
            accessibleName: qsTr("Gallery canvas")
        }
        Tk.CanvasView {
            objectName: "galleryCanvasTwin"
            implicitWidth: 180
            implicitHeight: 180
            document: sketch
            toolController: sketchTools
            viewState: sketchPose
            accessibleName: qsTr("Synchronized gallery canvas")
        }
        Tk.ToolShelf {
            orientation: "vertical"
            tools: Array.from(sketchTools.tools)
            activeToolId: sketchTools.activeTool
            tooltip: qsTr("Canvas tools")
            onToolRequested: function(id) { sketchTools.activeTool = id }
        }
    }
}
