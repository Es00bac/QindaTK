// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Spatial canvas reference (WP-040): one CanvasDocument with 10,000 shapes
// over a 2048x1536 tiled image, shown in two CanvasViews that share one
// CanvasViewState. The left view renders through the scene-graph provider
// when the window has a GPU backend; the right one is forced to software.
//   qtk-preview examples/spatial-canvas/Main.qml
//   qtk-preview examples/spatial-canvas/Main.qml --gpu --grab out.png
Rectangle {
    id: root
    width: 1280
    height: 800
    color: Tk.Theme.color.bg

    // Demo data only: the fixture and the drawing style are document values.
    Tk.CanvasDocument {
        id: document
        Component.onCompleted: populateFixture(10000, 7, true)
    }
    Tk.ToolController {
        id: tools
        document: document
    }
    Tk.CanvasViewState {
        id: sharedPose
        minZoom: 0.02
        maxZoom: 64
        // Fit keeps the pose's rotation, so the page is fitted turned.
        rotation: -12
    }

    function describe(s) {
        return qsTr("%1 · lod %2 · %3 tiles · %4 pending")
            .arg(s.backend).arg(s.lod).arg(s.visibleTiles).arg(s.pendingTiles)
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column

        Tk.ToolBar {
            objectName: "canvasToolbar"
            width: parent.width
            Tk.ToolShelf {
                objectName: "canvasTools"
                orientation: "horizontal"
                // A C++ QVariantList is a sequence, not a JS Array; ToolShelf checks Array.isArray.
                tools: Array.from(tools.tools)
                activeToolId: tools.activeTool
                temporaryToolId: tools.effectiveTool !== tools.activeTool ? tools.effectiveTool : ""
                tooltip: qsTr("Canvas tools")
                onToolRequested: function(id) { tools.activeTool = id }
            }
            Tk.ToolSeparator {}
            Tk.IconButton { objectName: "zoomOutButton"; iconName: "zoom-out"; tooltip: qsTr("Zoom out"); onClicked: left.zoomOut() }
            Tk.IconButton { objectName: "zoomInButton"; iconName: "zoom-in"; tooltip: qsTr("Zoom in"); onClicked: left.zoomIn() }
            Tk.IconButton { objectName: "rotateLeftButton"; iconName: "rotate-ccw"; tooltip: qsTr("Rotate view left"); onClicked: left.rotateBy(-15) }
            Tk.IconButton { objectName: "rotateRightButton"; iconName: "rotate-cw"; tooltip: qsTr("Rotate view right"); onClicked: left.rotateBy(15) }
            Tk.IconButton { objectName: "fitButton"; iconName: "maximize"; tooltip: qsTr("Fit page"); onClicked: left.resetView() }
            Tk.ToolSeparator {}
            Tk.IconButton { objectName: "undoButton"; iconName: "undo-2"; tooltip: qsTr("Undo %1").arg(document.undoText); enabled: document.canUndo; onClicked: document.undo() }
            Tk.IconButton { objectName: "redoButton"; iconName: "redo-2"; tooltip: qsTr("Redo"); enabled: document.canRedo; onClicked: document.redo() }
            Tk.ToolSeparator {}
            Tk.Label {
                objectName: "poseLabel"
                text: qsTr("%1% · %2° · %3 objects")
                    .arg(Math.round(sharedPose.zoom * 100)).arg(Math.round(sharedPose.rotation))
                    .arg(document.objectCount)
            }
        }

        Tk.Flex {
            objectName: "canvasPair"
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            direction: Tk.Flex.Row
            gap: Tk.Theme.size.border

            Tk.CanvasView {
                id: left
                objectName: "canvasLeft"
                viewId: "left"
                accessibleName: qsTr("Main canvas")
                document: document
                toolController: tools
                viewState: sharedPose
                backend: "auto"
                fitMode: "page"
                Tk.Flex.flex: 3
                height: parent.height
            }
            Tk.CanvasView {
                id: right
                objectName: "canvasRight"
                viewId: "right"
                accessibleName: qsTr("Synchronized canvas")
                document: document
                toolController: tools
                viewState: sharedPose
                backend: "software"
                Tk.Flex.flex: 2
                height: parent.height
            }
        }

        Tk.StatusBar {
            objectName: "canvasStatus"
            width: parent.width
            Tk.StatusField {
                objectName: "toolStatus"
                text: qsTr("Tool: %1 (%2)").arg(tools.effectiveTool).arg(tools.lastOutcome)
                tooltip: qsTr("Active tool and last gesture outcome")
            }
            Tk.StatusField {
                objectName: "selectionStatus"
                text: qsTr("%1 selected").arg(tools.selection.length)
                tooltip: qsTr("Selected objects")
            }
            Tk.StatusField {
                objectName: "hoverStatus"
                text: left.hoverHit.objectId !== undefined
                      ? qsTr("#%1 %2").arg(left.hoverHit.objectId).arg(left.hoverHit.part) : qsTr("No hit")
                tooltip: qsTr("Object under the pointer")
            }
            Tk.StatusField {
                objectName: "leftRenderStatus"
                text: root.describe(left.renderSurface.stats)
                tooltip: qsTr("Main canvas renderer")
            }
            Tk.StatusField {
                objectName: "rightRenderStatus"
                text: root.describe(right.renderSurface.stats)
                tooltip: qsTr("Synchronized canvas renderer")
            }
        }
    }
}
