// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: scene
    width: 1100
    height: 640
    color: Tk.Theme.color.bg
    property string pathId: ""
    property int contourIndex: 0
    property var strokeStyle: ({ width: 7, unit: "document", color: "#f4b858",
        alignment: "center", cap: "round", join: "bevel", dash: [3, 2], dashMode: "custom" })
    property var fillStyle: ({ kind: "solid", settings: { solid: { color: "#329eaa" } } })

    Tk.CanvasDocument {
        id: document
        pageRect: Qt.rect(0, 0, 520, 400)
        Component.onCompleted: {
            const nodes = [
                {x: 55, y: 95, outX: 150, outY: 12},
                {x: 395, y: 80, inX: 300, inY: 12},
                {x: 430, y: 315}, {x: 75, y: 340}
            ]
            scene.pathId = String(addPath(nodes, true, "#329eaa", "#f4b858", 7))
            addPathContour(scene.pathId, [
                {x: 175, y: 140}, {x: 310, y: 140},
                {x: 310, y: 250}, {x: 175, y: 250}
            ], true)
            styles.apply([scene.pathId], { dash: [3,2], cap: "round", join: "bevel" })
        }
    }
    Tk.CanvasStyleModel { id: styles; document: document }
    Tk.ClipboardService { id: clipboard }
    Tk.ToolController { id: tools; document: document }
    Tk.CanvasViewState { id: pose }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Row
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.md
        Tk.CanvasView {
            id: canvas
            objectName: "pathWorkingCanvas"
            viewId: "path-working"
            document: document
            toolController: tools
            viewState: pose
            fitMode: "page"
            accessibleName: qsTr("Editable cubic vector path")
            Tk.Flex.flex: 1
            height: parent.height
            Tk.PathEditor {
                objectName: "pathWorkingEditor"
                anchors.fill: parent
                document: document
                surface: canvas
                objectId: scene.pathId
                contourIndex: scene.contourIndex
            }
        }
        Tk.Flex {
            width: 340
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            Tk.Label { text: qsTr("PATH AUTHORING") }
            Tk.Caption { text: qsTr("Drag the square nodes or round Bézier handles.") }
            Tk.Button {
                text: scene.contourIndex === 0 ? qsTr("Edit inner contour") : qsTr("Edit outer contour")
                tooltip: qsTr("Switch the contour whose nodes can be dragged")
                onClicked: scene.contourIndex = scene.contourIndex === 0 ? 1 : 0
            }
            Tk.StrokeStyleEditor {
                objectName: "pathWorkingStroke"
                width: parent.width
                style: scene.strokeStyle
                capabilities: ({units: ["document"], alignments: ["center"]})
                onStyleChangeRequested: function(next) {
                    if (styles.apply([scene.pathId], {stroke: next.color, strokeWidth: next.width,
                                                      dash: next.dash, cap: next.cap, join: next.join}))
                        scene.strokeStyle = next
                }
            }
            Tk.FillStyleEditor {
                objectName: "pathWorkingFill"
                width: parent.width
                fill: scene.fillStyle
                capabilities: ({kinds: ["none", "solid"]})
                onFillChangeRequested: function(next) {
                    const color = next.kind === "none" ? null : next.settings.solid.color
                    if (styles.apply([scene.pathId], {fill: color})) scene.fillStyle = next
                }
            }
            Tk.Button {
                objectName: "pathWorkingUndo"
                text: qsTr("Undo edit")
                tooltip: qsTr("Undo the latest path or appearance edit")
                enabled: document.canUndo
                onClicked: document.undo()
            }
            Tk.Button {
                text: qsTr("Copy path")
                tooltip: qsTr("Copy the selected path with its hole")
                onClicked: clipboard.copyCanvasPaths(document, [scene.pathId])
            }
            Tk.Button {
                text: qsTr("Paste path")
                tooltip: qsTr("Paste a new path object with its hole")
                enabled: clipboard.canPaste
                onClicked: {
                    const ids = clipboard.pasteCanvasPaths(document, 24, 24)
                    if (ids.length > 0) scene.pathId = ids[0]
                }
            }
            Tk.Caption { text: styles.lastError; visible: text.length > 0 }
        }
    }
}
