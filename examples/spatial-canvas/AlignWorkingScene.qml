// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: scene
    width: 900
    height: 520
    color: Tk.Theme.color.bg
    property var selectedObjectIds: []

    Tk.CanvasDocument {
        id: document
        pageRect: Qt.rect(0, 0, 400, 260)
        Component.onCompleted: {
            const first = addRect(35, 30, 64, 60, "#43a6ce")
            const second = addRect(166, 105, 88, 45, "#d99361")
            const third = addRect(305, 170, 54, 52, "#9bbf68")
            scene.selectedObjectIds = [String(first), String(second), String(third)]
        }
    }
    Tk.ToolController { id: tools; document: document }
    Tk.CanvasAlignmentModel { id: alignment; document: document }
    Tk.CanvasViewState { id: pose }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.md

        Tk.CanvasView {
            id: canvas
            objectName: "alignmentWorkingCanvas"
            viewId: "alignment-working"
            document: document
            toolController: tools
            viewState: pose
            fitMode: "page"
            accessibleName: qsTr("Shapes to align")
            Tk.Flex.flex: 1
            height: parent.height
        }
        Item {
            implicitWidth: Tk.Theme.size.panelMinWidth * 2
            height: parent.height
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.AlignDistributePanel {
                    objectName: "alignmentWorkingPanel"
                    width: parent.width
                    selectedIds: scene.selectedObjectIds
                    referenceMode: "page"
                    onOperationRequested: function(operation, ids, referenceMode, keyId, spacing) {
                        alignment.apply(operation, ids, referenceMode, keyId, spacing)
                    }
                }
                Tk.Button {
                    objectName: "alignmentUndo"
                    text: qsTr("Undo alignment")
                    tooltip: qsTr("Restore the shapes before the last alignment")
                    enabled: document.canUndo
                    onClicked: document.undo()
                }
                Tk.Caption { text: alignment.lastError; visible: text.length > 0 }
            }
        }
    }
}
