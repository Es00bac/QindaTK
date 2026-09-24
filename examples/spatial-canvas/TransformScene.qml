// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-041 fixture: the frame previews a group transform while canonical objects
// remain in place until the gesture is finished.
Rectangle {
    id: root
    width: 900
    height: 600
    color: Tk.Theme.color.bg
    property var firstId: 0
    property var secondId: 0
    property bool rotatePreview: false

    Tk.CanvasDocument {
        id: document
        pageRect: Qt.rect(0, 0, 600, 400)
        Component.onCompleted: {
            root.firstId = addRect(80, 70, 160, 100, "#4a81a1")
            root.secondId = addRect(300, 170, 160, 110, "#a98655")
        }
    }
    Tk.ToolController { id: tools; document: document }
    Tk.CanvasViewState { id: pose; rotation: 15 }
    Tk.SpatialTransform {
        id: transform
        objectName: "groupTransform"
        document: document
        tools: tools
        viewState: pose
    }
    Tk.CanvasView {
        id: view
        objectName: "transformFixtureCanvas"
        anchors.fill: parent
        document: document
        toolController: tools
        viewState: pose
        backend: "software"
        fitMode: "page"
        accessibleName: qsTr("Group transform fixture")
        Tk.SelectionOverlay {
            objectName: "transformFixtureHandles"
            anchors.fill: parent
            surface: view
            selectionBounds: transform.previewBounds
            selectionCorners: transform.previewCorners
            onTransformRequested: function(handle, start, current, modifiers) {
                transform.update(handle, start, current, modifiers)
            }
            onTransformFinished: function(handle) { transform.finish(handle) }
            onTransformCanceled: function(handle) { transform.cancel(handle) }
            onTransformNudgeRequested: function(handle, dx, dy, modifiers) {
                transform.nudge(handle, dx, dy, modifiers)
            }
            onRotateStepRequested: function(degrees, modifiers) {
                transform.rotateBy(degrees, modifiers)
            }
        }
    }
    Component.onCompleted: Qt.callLater(function() {
        tools.selection = [root.firstId, root.secondId]
        if (root.rotatePreview)
            transform.update("rotate", Qt.point(270, 70), Qt.point(310, 82))
        else
            transform.update("bottomRight", Qt.point(460, 280), Qt.point(505, 315))
    })
}
