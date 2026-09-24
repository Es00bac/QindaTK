// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Reproducible WP-041 lasso preview: one selected shape and one open polygon.
Rectangle {
    id: root
    width: 900
    height: 600
    color: Tk.Theme.color.bg
    property var firstId: 0

    Tk.CanvasDocument {
        id: document
        pageRect: Qt.rect(0, 0, 600, 400)
        Component.onCompleted: {
            root.firstId = addRect(70, 55, 180, 125, "#4a81a1")
            addRect(300, 160, 195, 140, "#a98655")
        }
    }
    Tk.ToolController {
        id: tools
        document: document
        activeTool: "lasso"
    }
    Tk.CanvasView {
        id: view
        objectName: "lassoFixtureCanvas"
        anchors.fill: parent
        document: document
        toolController: tools
        backend: "software"
        fitMode: "page"
        viewRotation: 17
        accessibleName: qsTr("Lasso selection fixture")
    }
    Component.onCompleted: Qt.callLater(function() {
        tools.selection = [root.firstId]
        tools.begin("fixture", 280, 130, Tk.ToolController.Mouse, Qt.ShiftModifier)
        tools.update("fixture", 530, 145)
        tools.update("fixture", 510, 320)
        tools.update("fixture", 285, 320)
    })
}
