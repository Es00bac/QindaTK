// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a zoomable, scrollable document
// viewport. The single child is the document surface (a page, a slide, an
// artboard) sized by ITS implicit size in document units; the viewport
// scales it by `zoom`, centres it when it is narrower than the view, pads
// it by `contentPadding`, scrolls both axes, and paints the canvas role
// behind it. Ctrl+wheel zooms about the cursor, Shift+wheel scrolls
// horizontally, a middle-button drag pans. On a touchscreen two fingers
// pinch-zoom and pan together (a touchpad pinch zooms too); with
// `touchPan` one finger drags the view. `fitMode` "width" / "page"
// re-fits on every resize until the user zooms by hand. At a pan edge,
// ordinary wheel input reaches an ancestor Scroll (D-021).
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
    // One-finger touch drag pans. Off by default: a finger on a child that
    // handles touch itself (a drawing surface, a draggable shape) must not
    // be stolen. Children that grab the finger first keep it either way.
    property bool touchPan: false
    readonly property bool gestureActive: pinch.active || touchDrag.active
    property string accessibleName: qsTr("Document viewport")
    readonly property real contentWidth: contentHost.implicitWidth
    readonly property real contentHeight: contentHost.implicitHeight
    readonly property real viewportWidth: scroll.viewportWidth
    readonly property real viewportHeight: scroll.viewportHeight

    // Retain the tab stop until focus leaves if interaction is disabled
    // while focused; clearing it immediately is rejected by Qt.
    activeFocusOnTab: activeFocus || (enabled && interactive)
    Accessible.role: Accessible.Pane
    Accessible.name: accessibleName
    Accessible.description: qsTr("%1 percent zoom").arg(Math.round(zoom * 100))
    Accessible.focusable: enabled && interactive
    Accessible.focused: activeFocus
    Accessible.onIncreaseAction: if (enabled && interactive) viewport.zoomIn()
    Accessible.onDecreaseAction: if (enabled && interactive) viewport.zoomOut()

    // AGENT-CONTRACT (docs/control-interaction.md): the viewport takes a
    // keyboard turn only while it itself owns focus. Document children keep
    // their own shortcuts; an unconsumed pan key can reach an ancestor Scroll.
    Keys.onPressed: function(event) {
        if (!viewport.activeFocus || !viewport.enabled || !viewport.interactive) return
        if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
            viewport.zoomIn()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Minus) {
            viewport.zoomOut()
            event.accepted = true
            return
        }
        if (event.modifiers !== Qt.NoModifier) return
        const step = Tk.Theme.size.row
        const page = Math.max(step, scroll.viewportHeight - step)
        let dx = 0
        let dy = 0
        switch (event.key) {
        case Qt.Key_Left: dx = -step; break
        case Qt.Key_Right: dx = step; break
        case Qt.Key_Up: dy = -step; break
        case Qt.Key_Down: dy = step; break
        case Qt.Key_PageUp: dy = -page; break
        case Qt.Key_PageDown: dy = page; break
        case Qt.Key_Home: dy = -scroll.contentY; break
        case Qt.Key_End: dy = scroll.contentHeight - scroll.viewportHeight - scroll.contentY; break
        default: return
        }
        const maxX = Math.max(0, scroll.contentWidth - scroll.viewportWidth)
        const maxY = Math.max(0, scroll.contentHeight - scroll.viewportHeight)
        const nextX = Math.max(0, Math.min(maxX, scroll.contentX + dx))
        const nextY = Math.max(0, Math.min(maxY, scroll.contentY + dy))
        event.accepted = Math.abs(nextX - scroll.contentX) > 0.5
                      || Math.abs(nextY - scroll.contentY) > 0.5
        if (event.accepted) {
            scroll.contentX = nextX
            scroll.contentY = nextY
        }
    }

    // Accepted wheel, key and assistive zoom, plus explicit setZoom/fit
    // operations, emit this; owner assignment to `zoom` is silent.
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

    Rectangle {
        objectName: "viewportFocusRing"
        anchors.fill: parent
        color: "transparent"
        border.width: Tk.Theme.size.focusRing
        border.color: Tk.Theme.color.focus
        visible: viewport.activeFocus && viewport.enabled && viewport.interactive
        z: 1
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
        blocking: false
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
            const beforeX = scroll.contentX
            const beforeY = scroll.contentY
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
            // AGENT-GUARD: at a document edge, let a wheel event reach an
            // ancestor Scroll. Consuming it unconditionally traps nested
            // canvas views and breaks D-021's hard-bound handoff.
            event.accepted = Math.abs(scroll.contentX - beforeX) > 0.5
                          || Math.abs(scroll.contentY - beforeY) > 0.5
        }
    }

    // Two-finger pinch: the document point under the fingers' centroid at
    // the start stays under the centroid, so spreading zooms about the
    // fingers and moving them together pans — one formula for both.
    PinchHandler {
        id: pinch
        objectName: "viewportPinch"
        enabled: viewport.interactive
        target: null
        minimumPointCount: 2
        maximumPointCount: 5
        acceptedDevices: PointerDevice.TouchScreen | PointerDevice.TouchPad
        property real startZoom: 1
        property point anchorDoc: Qt.point(0, 0)
        onActiveChanged: {
            if (active) {
                startZoom = viewport.zoom
                anchorDoc = viewport.toContent(centroid.position)
                viewport.fitMode = "none"
            } else {
                viewport.zoomModified(viewport.zoom)
            }
        }
        onActiveScaleChanged: pinch.follow()
        onCentroidChanged: pinch.follow()
        function follow() {
            if (!active) return
            const next = viewport.clampZoom(startZoom * activeScale)
            viewport.zoom = next
            const origin = Qt.point(viewport.contentOriginX(), viewport.contentOriginY())
            scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.viewportWidth,
                                                   anchorDoc.x * next + origin.x - centroid.position.x))
            scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.viewportHeight,
                                                   anchorDoc.y * next + origin.y - centroid.position.y))
        }
    }

    DragHandler {
        id: touchDrag
        objectName: "viewportTouchPan"
        enabled: viewport.interactive && viewport.touchPan
        acceptedDevices: PointerDevice.TouchScreen
        maximumPointCount: 1
        target: null
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
