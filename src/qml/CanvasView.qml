// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#canvasview, docs/canvas.md): the spatial
// document surface. Bind `document` (a Tk.CanvasDocument or any
// CanvasDocumentAdapter), `toolController` (a Tk.ToolController) and,
// for synchronized views, one shared `viewState` (a Tk.CanvasViewState).
// Navigation changes only the view pose; every document change goes through
// the tool controller's one-commit-per-gesture path.
//
// AGENT-NOTE: the C++ Tk.CanvasSurface is the root so its API needs no
// passthroughs (same pattern as Sparkline over Graph). This file supplies
// what C++ must not: Theme roles, accessibility and the overlay slot.
// Tk.Viewport is unchanged and remains the right choice for a scrolled page
// that needs no rotation, tools or picking (docs/canvas.md#viewport).
Tk.CanvasSurface {
    id: view

    property string accessibleName: qsTr("Document canvas")
    // Items placed here draw above the document in view coordinates.
    default property alias overlayData: overlaySlot.data
    readonly property Item overlay: overlayItem

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    canvasColor: Tk.Theme.color.canvas
    hitTolerance: Tk.Theme.space.xs
    fitPadding: Tk.Theme.space.xl
    keyboardPanStep: Tk.Theme.size.control * 2

    Accessible.role: Accessible.Canvas
    Accessible.name: accessibleName
    Accessible.description: qsTr("%1 percent zoom, rotated %2 degrees")
        .arg(Math.round(view.zoom * 100)).arg(Math.round(view.viewRotation))
    Accessible.focusable: true
    Accessible.onIncreaseAction: view.zoomIn()
    Accessible.onDecreaseAction: view.zoomOut()

    Tk.CanvasOverlay {
        id: overlayItem
        objectName: "canvasOverlay"
        anchors.fill: parent
        surface: view
        selectionColor: Tk.Theme.color.accent
        hoverColor: Tk.Theme.color.focus
        previewColor: Tk.Theme.color.accent
        previewFill: Tk.Theme.color.hover
        lineWidth: Tk.Theme.size.border
    }

    Item {
        id: overlaySlot
        objectName: "canvasOverlaySlot"
        anchors.fill: parent
    }

    Rectangle {
        objectName: "canvasFocusRing"
        anchors.fill: parent
        color: "transparent"
        border.color: Tk.Theme.color.focus
        border.width: Tk.Theme.size.focusRing
        visible: view.activeFocus
    }
}
