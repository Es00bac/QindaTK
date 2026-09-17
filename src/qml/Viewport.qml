// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a zoomable, scrollable document
// viewport. The single child is the document surface (a page, a slide, an
// artboard) sized by ITS implicit size in document units; the viewport
// scales it by `zoom`, centres it when it is narrower than the view, pads
// it by `contentPadding`, scrolls both axes, and paints the canvas role
// behind it. Ctrl+wheel zooms about the cursor, Shift+wheel scrolls
// horizontally, a middle-button drag pans. `fitMode` "width" / "page"
// re-fits on every resize until the user zooms by hand.
//
// AGENT-GUARD: zoom is a view transform only. The child keeps its own
// coordinate space; map pointer positions with `toContent(point)`.
Item {
    id: viewport

    default property alias content: contentHost.data
    readonly property Item contentItem: contentHost
    readonly property Item scrollItem: scroll

    property real zoom: 1.0
    property real minZoom: 0.1
    property real maxZoom: 8.0
    property string fitMode: "none"
    property real contentPadding: Tk.Theme.space.xl
    property real wheelZoomStep: 1.25
    property bool interactive: true
    readonly property real contentWidth: contentHost.implicitWidth
    readonly property real contentHeight: contentHost.implicitHeight
    readonly property real viewportWidth: scroll.viewportWidth
    readonly property real viewportHeight: scroll.viewportHeight

    // User-driven changes only (wheel, pan, fit); programmatic setZoom too.
    signal zoomModified(real zoom)

    implicitWidth: contentHost.implicitWidth * zoom + contentPadding * 2
    implicitHeight: contentHost.implicitHeight * zoom + contentPadding * 2

    function clampZoom(value) {
        return Math.max(viewport.minZoom, Math.min(viewport.maxZoom, value))
    }

    // Origin of the scaled content inside the scroll surface (document
    // units × zoom), centred when narrower than the viewport.
    function contentOriginX() {
        return Math.max(viewport.contentPadding,
                        (scroll.viewportWidth - contentHost.implicitWidth * viewport.zoom) / 2)
    }
    function contentOriginY() {
        return Math.max(viewport.contentPadding,
                        (scroll.viewportHeight - contentHost.implicitHeight * viewport.zoom) / 2)
    }

    // Viewport point → document coordinates of the child.
    function toContent(point) {
        return Qt.point((point.x + scroll.contentX - contentOriginX()) / viewport.zoom,
                        (point.y + scroll.contentY - contentOriginY()) / viewport.zoom)
    }
    // Document coordinates → viewport point.
    function fromContent(point) {
        return Qt.point(point.x * viewport.zoom + contentOriginX() - scroll.contentX,
                        point.y * viewport.zoom + contentOriginY() - scroll.contentY)
    }

    // Sets the zoom keeping the document point under `anchor` (a viewport
    // point) where it is; omit `anchor` to keep the viewport centre.
    function setZoom(value, anchor) {
        const next = viewport.clampZoom(value)
        if (Math.abs(next - viewport.zoom) < 1e-6) {
            return
        }
        const pivot = anchor !== undefined ? anchor
                    : Qt.point(scroll.viewportWidth / 2, scroll.viewportHeight / 2)
        const docPoint = viewport.toContent(pivot)
        viewport.fitMode = "none"
        viewport.zoom = next
        // The surface has re-laid out synchronously (bindings); put the
        // document point back under the pivot.
        scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.viewportWidth,
                                               docPoint.x * next + contentOriginX() - pivot.x))
        scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.viewportHeight,
                                               docPoint.y * next + contentOriginY() - pivot.y))
        viewport.zoomModified(next)
    }
    function zoomAbout(anchor, factor) {
        viewport.setZoom(viewport.zoom * factor, anchor)
    }
    function zoomIn() { viewport.zoomAbout(undefined, viewport.wheelZoomStep) }
    function zoomOut() { viewport.zoomAbout(undefined, 1 / viewport.wheelZoomStep) }
    function fitWidth() {
        viewport.fitMode = "width"
        viewport.applyFit()
    }
    function fitPage() {
        viewport.fitMode = "page"
        viewport.applyFit()
    }
    function applyFit() {
        if (contentHost.implicitWidth <= 0 || contentHost.implicitHeight <= 0
                || scroll.viewportWidth <= 0 || scroll.viewportHeight <= 0) {
            return
        }
        const availableW = scroll.viewportWidth - viewport.contentPadding * 2
        const availableH = scroll.viewportHeight - viewport.contentPadding * 2
        let next = viewport.zoom
        if (viewport.fitMode === "width") {
            next = availableW / contentHost.implicitWidth
        } else if (viewport.fitMode === "page") {
            next = Math.min(availableW / contentHost.implicitWidth,
                            availableH / contentHost.implicitHeight)
        } else {
            return
        }
        next = viewport.clampZoom(next)
        if (Math.abs(next - viewport.zoom) > 1e-6) {
            viewport.zoom = next
            viewport.zoomModified(next)
        }
    }
    function ensureVisible(rect) {
        // `rect` in document coordinates.
        const left = rect.x * viewport.zoom + contentOriginX()
        const top = rect.y * viewport.zoom + contentOriginY()
        const right = left + rect.width * viewport.zoom
        const bottom = top + rect.height * viewport.zoom
        if (left < scroll.contentX) scroll.contentX = Math.max(0, left - viewport.contentPadding)
        else if (right > scroll.contentX + scroll.viewportWidth)
            scroll.contentX = Math.min(scroll.contentWidth - scroll.viewportWidth, right - scroll.viewportWidth + viewport.contentPadding)
        if (top < scroll.contentY) scroll.contentY = Math.max(0, top - viewport.contentPadding)
        else if (bottom > scroll.contentY + scroll.viewportHeight)
            scroll.contentY = Math.min(scroll.contentHeight - scroll.viewportHeight, bottom - scroll.viewportHeight + viewport.contentPadding)
    }

    onWidthChanged: viewport.applyFit()
    onHeightChanged: viewport.applyFit()
    onFitModeChanged: viewport.applyFit()

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
    }

    Tk.Scroll {
        id: scroll
        objectName: "viewportScroll"
        anchors.fill: parent
        overflowX: Tk.Scroll.Auto
        overflowY: Tk.Scroll.Auto
        interactive: false   // the middle-drag pan below; a pen must not flick

        Item {
            id: surface
            objectName: "viewportSurface"
            implicitWidth: Math.max(scroll.viewportWidth,
                                    contentHost.implicitWidth * viewport.zoom + viewport.contentPadding * 2)
            implicitHeight: Math.max(scroll.viewportHeight,
                                     contentHost.implicitHeight * viewport.zoom + viewport.contentPadding * 2)

            Item {
                id: contentHost
                objectName: "viewportContent"
                x: viewport.contentOriginX()
                y: viewport.contentOriginY()
                width: implicitWidth
                height: implicitHeight
                scale: viewport.zoom
                transformOrigin: Item.TopLeft
                readonly property Item single: children.length === 1 ? children[0] : null
                implicitWidth: single ? single.implicitWidth : 0
                implicitHeight: single ? single.implicitHeight : 0
                onImplicitWidthChanged: viewport.applyFit()
                onImplicitHeightChanged: viewport.applyFit()
                onChildrenChanged: Qt.callLater(viewport.seat)
            }
        }
    }

    // The child keeps its implicit size; it is sized to it, never
    // stretched (a page is as big as the page).
    function seat() {
        const single = contentHost.single
        if (single === null || Tk.LayoutInfo.hasAnchors(single)) {
            return
        }
        single.width = Qt.binding(function() { return single.implicitWidth })
        single.height = Qt.binding(function() { return single.implicitHeight })
    }
    Component.onCompleted: {
        viewport.seat()
        viewport.applyFit()
    }

    WheelHandler {
        objectName: "viewportWheel"
        enabled: viewport.interactive
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function(event) {
            const steps = event.angleDelta.y / 120
            if (event.modifiers & Qt.ControlModifier) {
                if (steps !== 0) {
                    viewport.zoomAbout(Qt.point(event.x, event.y),
                                       Math.pow(viewport.wheelZoomStep, steps))
                }
                event.accepted = true
                return
            }
            const lineStep = Tk.Theme.size.row * 3
            if (event.modifiers & Qt.ShiftModifier) {
                scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.viewportWidth,
                                                       scroll.contentX - steps * lineStep))
            } else {
                const hSteps = event.angleDelta.x / 120
                scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.viewportHeight,
                                                       scroll.contentY - steps * lineStep))
                if (hSteps !== 0) {
                    scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.viewportWidth,
                                                           scroll.contentX - hSteps * lineStep))
                }
            }
            event.accepted = true
        }
    }

    DragHandler {
        id: pan
        objectName: "viewportPan"
        enabled: viewport.interactive
        acceptedButtons: Qt.MiddleButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus
        target: null
        cursorShape: Qt.ClosedHandCursor
        property real startX: 0
        property real startY: 0
        onActiveChanged: {
            if (active) {
                startX = scroll.contentX
                startY = scroll.contentY
            }
        }
        onTranslationChanged: {
            if (!active) return
            scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.viewportWidth,
                                                   startX - translation.x))
            scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.viewportHeight,
                                                   startY - translation.y))
        }
    }
}
