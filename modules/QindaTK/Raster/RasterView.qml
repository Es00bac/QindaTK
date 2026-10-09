// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/raster.md): documentSize describes editing coordinates,
// not the resolution of the display proxy. Stylus samples and selectionRect
// use those document coordinates even when the image is reduced or zoomed.
Tk.Viewport {
    id: view
    property url source
    property size documentSize: Qt.size(0, 0)
    property rect displayRect: Qt.rect(0, 0, documentSize.width, documentSize.height)
    property rect selectionRect: Qt.rect(0, 0, 0, 0)
    property bool paintingEnabled: true
    property bool smooth: zoom < 2
    readonly property alias imageStatus: image.status
    readonly property alias surfaceItem: surface
    readonly property alias inputHandler: stylus
    property point _panStart: Qt.point(0, 0)
    signal strokeBegan(var sample)
    signal strokeMoved(var sample)
    signal strokeEnded(var sample)
    signal strokeCanceled()
    minZoom: 0.0001
    maxZoom: 64
    accessibleName: qsTr("Raster document viewport")

    Item {
        id: surface
        objectName: "rasterSurface"
        implicitWidth: view.documentSize.width
        implicitHeight: view.documentSize.height
        Checkerboard { anchors.fill: parent }
        Image {
            id: image
            objectName: "rasterImage"
            x: view.displayRect.x
            y: view.displayRect.y
            width: view.displayRect.width
            height: view.displayRect.height
            source: view.source
            cache: false
            smooth: view.smooth
            fillMode: Image.Stretch
        }
        Rectangle {
            objectName: "rasterSelection"
            x: view.selectionRect.x
            y: view.selectionRect.y
            width: view.selectionRect.width
            height: view.selectionRect.height
            visible: width > 0 && height > 0
            color: Tk.Theme.alpha(Tk.Theme.color.accent, Tk.Theme.opacity.tint)
            border.color: Tk.Theme.color.accent
            border.width: Tk.Theme.size.border / view.zoom
        }
        Tk.StylusHandler {
            id: stylus
            objectName: "rasterStylus"
            acceptedButtons: Qt.LeftButton
            enabled: view.paintingEnabled && view.interactive
            mousePressure: 1
            onBegan: sample => view.strokeBegan(sample)
            onMoved: sample => view.strokeMoved(sample)
            onEnded: sample => view.strokeEnded(sample)
            onCanceled: view.strokeCanceled()
        }
        DragHandler {
            target: null
            acceptedButtons: Qt.LeftButton
            enabled: view.interactive && !view.paintingEnabled
            onActiveChanged: if (active) view._panStart = Qt.point(view.scrollItem.contentX, view.scrollItem.contentY)
            onTranslationChanged: {
                if (!active) return
                view.scrollItem.contentX = Math.max(0, Math.min(view.scrollItem.contentWidth - view.viewportWidth,
                    view._panStart.x - translation.x * view.zoom))
                view.scrollItem.contentY = Math.max(0, Math.min(view.scrollItem.contentHeight - view.viewportHeight,
                    view._panStart.y - translation.y * view.zoom))
            }
        }
    }
}
