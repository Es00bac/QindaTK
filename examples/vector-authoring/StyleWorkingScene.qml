// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: scene
    width: 1120
    height: 640
    color: Tk.Theme.color.bg
    property var selectedIds: []
    property var strokeStyle: ({ width: 4, unit: "document", color: "#e6a55a",
                                  alignment: "center", cap: "butt", join: "miter",
                                  miterLimit: 4, dash: [], dashMode: "solid" })
    property var fillStyle: ({ kind: "solid", settings: {
        solid: { color: "#468fb8" }, gradient: { gradient: {} }, pattern: { patternId: "" }
    } })

    Tk.CanvasDocument {
        id: document
        pageRect: Qt.rect(0, 0, 420, 300)
        Component.onCompleted: {
            const a = addRect(52, 52, 110, 90, "#468fb8", "#e6a55a", 4)
            const b = addEllipse(250, 126, 100, 100, "#468fb8", "#e6a55a", 4)
            scene.selectedIds = [String(a), String(b)]
        }
    }
    Tk.CanvasStyleModel { id: styles; document: document }
    Tk.ToolController { id: tools; document: document }
    Tk.CanvasViewState { id: pose }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.md

        Tk.CanvasView {
            objectName: "styleWorkingCanvas"
            viewId: "style-working"
            document: document
            toolController: tools
            viewState: pose
            fitMode: "page"
            accessibleName: qsTr("Vector shapes with editable style")
            Tk.Flex.flex: 1
            height: parent.height
        }
        Item {
            implicitWidth: 390
            height: parent.height
            Tk.Scroll {
                anchors.fill: parent
                overflowY: Tk.Scroll.Auto
                accessibleName: qsTr("Style inspector")
                Tk.Flex {
                    width: parent.width
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    Tk.StrokeStyleEditor {
                        objectName: "styleWorkingStroke"
                        width: parent.width
                        style: scene.strokeStyle
                        capabilities: ({ units: ["document"], alignments: ["center"],
                                         caps: ["butt"], joins: ["miter"] })
                        onStyleChangeRequested: function(next) {
                            if (styles.apply(scene.selectedIds,
                                             { stroke: next.color, strokeWidth: next.width }))
                                scene.strokeStyle = next
                        }
                    }
                    Tk.FillStyleEditor {
                        objectName: "styleWorkingFill"
                        width: parent.width
                        fill: scene.fillStyle
                        capabilities: ({ kinds: ["none", "solid"] })
                        onFillChangeRequested: function(next) {
                            if (next.kind !== "none" && next.kind !== "solid") return
                            const value = next.kind === "none" ? null : next.settings.solid.color
                            if (styles.apply(scene.selectedIds, { fill: value }))
                                scene.fillStyle = next
                        }
                    }
                    Tk.Button {
                        objectName: "styleWorkingUndo"
                        text: qsTr("Undo style change")
                        tooltip: qsTr("Restore the previous style of both shapes")
                        enabled: document.canUndo
                        onClicked: document.undo()
                    }
                    Tk.Caption { text: styles.lastError; visible: text.length > 0 }
                }
            }
        }
    }
}
