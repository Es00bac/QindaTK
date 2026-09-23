// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    objectName: "installedCanvasRoot"
    width: 480
    height: 360

    Tk.CanvasDocument {
        id: document
        objectName: "installedCanvasDocument"
        pageRect: Qt.rect(0, 0, 256, 256)
    }
    Tk.ToolController {
        id: tools
        objectName: "installedToolController"
        document: document
        activeTool: "rectangle"
        fillColor: "red"
        strokeColor: "transparent"
        strokeWidth: 0
    }
    Tk.CanvasView {
        objectName: "installedCanvasView"
        anchors.fill: parent
        document: document
        toolController: tools
        viewId: "installed"
        backend: "software"
        fitMode: "page"
        accessibleName: qsTr("Installed canvas")
    }
}
