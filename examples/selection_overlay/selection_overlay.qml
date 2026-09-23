// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 760
    height: 520
    property int selectedId: 0

    Tk.CanvasDocument {
        id: document
        pageRect: Qt.rect(0, 0, 320, 240)
    }
    Tk.ToolController {
        id: tools
        document: document
    }

    Tk.CanvasView {
        id: view
        anchors.fill: parent
        document: document
        toolController: tools
        viewRotation: -12
        zoom: 1.3
        centerX: 160
        centerY: 120

        Tk.SelectionOverlay {
            anchors.fill: parent
            surface: view
            selectionBounds: document.objectInfo(selectedId).bounds
            onTransformRequested: (handle, start, current, modifiers) => {
                // Example consumer seam: a real geometry adapter owns previews and commits.
            }
        }
    }

    Component.onCompleted: {
        selectedId = document.addRect(72, 54, 136, 92, "#466f91", "#b8d9e9", 2)
        tools.selection = [selectedId]
    }
}
