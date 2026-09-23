// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Close-up of the canvas contracts: transformed shapes, a stroke, a locked
// object, a degenerate (singular-transform) object that is neither drawn nor
// picked, the selection outline and a live marquee preview. The view is
// turned 20 degrees; every document coordinate below is untouched by that.
//   qtk-preview examples/spatial-canvas/Detail.qml --grab detail.png
Rectangle {
    id: root
    width: 720
    height: 480
    color: Tk.Theme.color.bg

    // Demo data only.
    Tk.CanvasDocument {
        id: document
        pageRect: Qt.rect(0, 0, 320, 220)
        Component.onCompleted: {
            const a = addRect(20, 20, 120, 80, "#e0564f", "#3a1414", 2)
            const b = addEllipse(100, 60, 140, 90, "#4fa3d9", "#12303f", 3)
            addPolyline([Qt.point(30, 190), Qt.point(120, 130), Qt.point(200, 190), Qt.point(290, 120)], "#2a2a2a", 4)
            const turned = addRect(230, 20, 60, 60, "#7cc46f", "#1c3a17", 2)
            setObjectTransform(turned, [0.866, 0.5, -0.5, 0.866, 260, 10])
            const locked = addRect(180, 150, 100, 50, "#d9b44f")
            setObjectLocked(locked, true)
            const flat = addRect(60, 120, 80, 40, "#9b59b6")
            setObjectTransform(flat, [1, 0, 0, 0, 60, 120]) // singular: stays in the model only
            tools.selection = [a, b]
            // A Shift-marquee in progress (empty start point, additive so the
            // selection above stays), driven through the public API.
            tools.begin("detail", 5, 110, Tk.ToolController.Mouse, Qt.ShiftModifier)
            tools.update("detail", 100, 215)
        }
    }
    Tk.ToolController {
        id: tools
        document: document
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        Tk.CanvasView {
            id: view
            objectName: "detailCanvas"
            viewId: "detail"
            accessibleName: qsTr("Detail canvas")
            document: document
            toolController: tools
            fitMode: "page"
            viewRotation: 20
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
        }
        Tk.StatusBar {
            objectName: "detailStatus"
            width: parent.width
            Tk.StatusField {
                objectName: "detailPose"
                text: qsTr("%1% · %2°").arg(Math.round(view.zoom * 100)).arg(Math.round(view.viewRotation))
                tooltip: qsTr("View pose")
            }
            Tk.StatusField {
                objectName: "detailPreview"
                text: qsTr("Preview: %1 · %2 selected").arg(tools.preview.kind).arg(tools.selection.length)
                tooltip: qsTr("Transient tool state")
            }
            Tk.StatusField {
                objectName: "detailHit"
                // Probe the view centre; the answer is a document id and part.
                text: {
                    const hits = view.width > 0 && document.revision > 0 ? view.hitTest(view.width / 2, view.height / 2, "report") : []
                    return hits.length ? qsTr("Centre hit #%1 %2%3").arg(hits[0].objectId).arg(hits[0].part)
                                          .arg(hits[0].locked ? qsTr(" (locked)") : "")
                                       : qsTr("Centre hit: none")
                }
                tooltip: qsTr("Hit test at the view centre")
            }
        }
    }
}
