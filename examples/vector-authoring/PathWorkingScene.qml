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
    property string pathError: ""
    readonly property var selectedNodes: editor.selectedNodeIds
    function nodeSummary() {
        if (selectedNodes.length === 0) return qsTr("Click a square node to select it; Shift-click adds one.")
        const first = editor.model.node(selectedNodes[0])
        return selectedNodes.length === 1 ? qsTr("Node %1 selected (%2)").arg(first.id).arg(first.type)
                                          : qsTr("%n nodes selected", "", selectedNodes.length)
    }

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
                id: editor
                objectName: "pathWorkingEditor"
                anchors.fill: parent
                document: document
                surface: canvas
                objectId: scene.pathId
                contourIndex: scene.contourIndex
                onEditRejected: function(message) { scene.pathError = message }
            }
        }
        Tk.Flex {
            width: 340
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            Tk.Label { text: qsTr("PATH AUTHORING") }
            Tk.Caption { text: qsTr("Drag the square nodes or round Bézier handles.") }
            Tk.Caption {
                objectName: "pathWorkingNodeInfo"
                width: parent.width
                text: { editor.nodes; return scene.nodeSummary() } // node types change with edits
                wrapMode: Text.WordWrap
            }
            Tk.Flex {
                width: parent.width
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.sm
                Repeater {
                    model: [
                        { name: "Insert", label: qsTr("Insert"), tip: qsTr("Add a node halfway along the segment after the selected node"), run: "insert" },
                        { name: "Delete", label: qsTr("Delete"), tip: qsTr("Delete the selected nodes"), run: "delete" },
                        { name: "Split", label: qsTr("Split"), tip: qsTr("Open the contour at the selected node"), run: "split" },
                        { name: "Join", label: qsTr("Join"), tip: qsTr("Join the two selected end nodes"), run: "join" },
                        { name: "Corner", label: qsTr("Corner"), tip: qsTr("Retract the selected nodes' handles"), run: "corner" },
                        { name: "Smooth", label: qsTr("Smooth"), tip: qsTr("Align the selected nodes' handles"), run: "smooth" },
                        { name: "Symmetric", label: qsTr("Symmetric"), tip: qsTr("Align the handles and make them equal"), run: "symmetric" }
                    ]
                    delegate: Tk.Button {
                        required property var modelData
                        objectName: "pathWorking" + modelData.name
                        text: modelData.label
                        tooltip: modelData.tip
                        enabled: scene.selectedNodes.length > 0
                        onClicked: {
                            scene.pathError = ""
                            const run = modelData.run
                            if (run === "insert") editor.insertAfterSelection()
                            else if (run === "delete") editor.deleteSelection()
                            else if (run === "split") editor.splitSelection()
                            else if (run === "join") editor.joinSelection()
                            else editor.setSelectionType(run)
                        }
                    }
                }
            }
            Tk.Button {
                objectName: "pathWorkingNextContour"
                text: qsTr("Edit contour %1 of %2").arg(scene.contourIndex + 1).arg(editor.model.contours.length)
                tooltip: qsTr("Switch the contour whose nodes can be dragged")
                onClicked: scene.contourIndex = (scene.contourIndex + 1) % Math.max(1, editor.model.contours.length)
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
            Tk.Caption {
                objectName: "pathWorkingError"
                width: parent.width
                text: scene.pathError || styles.lastError
                visible: text.length > 0
                color: Tk.Theme.color.warning
                wrapMode: Text.WordWrap
            }
        }
    }
}
