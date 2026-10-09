// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: CSS `overflow: auto`. The single content child is sized
// to its implicit size on a scrolling axis and to the viewport on a hidden
// axis, so `overflowX: Scroll.Hidden` (the default) makes a Column wrap to
// the width and scroll vertically. Scrollbars are overlays and take no
// layout space. D-021 defines nested wheel/key handoff, hard bounds and
// focus reveal through inner and outer viewports.
Item {
    id: scroll

    enum Overflow { Auto, Hidden, Always }

    default property alias content: contentHost.data
    readonly property Item contentItem: contentHost
    readonly property alias flickable: flick
    property int overflowX: Scroll.Hidden
    property int overflowY: Scroll.Auto
    property real padding: 0
    // Reserve content width beside an overlay vertical scrollbar when the
    // owner has right-edge controls (for example a dialog form).
    property real contentRightInset: 0
    property alias contentX: flick.contentX
    property alias contentY: flick.contentY
    readonly property alias contentWidth: flick.contentWidth
    readonly property alias contentHeight: flick.contentHeight
    readonly property real viewportWidth: flick.width
    readonly property real viewportHeight: flick.height
    readonly property bool scrollableX: flick.contentWidth > flick.width + 0.5
    readonly property bool scrollableY: flick.contentHeight > flick.height + 0.5
    property bool interactive: true
    property string accessibleName: ""
    // AGENT-NOTE: ancestor Scrolls reveal this viewport, while this Scroll
    // reveals the focused descendant inside it. The marker prevents an outer
    // viewport from scrolling to a deep item before inner scrolling settles.
    readonly property bool _qindaScrollViewport: true

    // AGENT-GUARD: Qt rejects clearing activeFocusOnTab while this viewport
    // owns focus. Retain the tab stop until focus leaves after overflow ends
    // or interaction is disabled; otherwise the old true value can stick.
    activeFocusOnTab: activeFocus || (interactive && (scrollableX || scrollableY))
    Accessible.role: Accessible.Pane
    Accessible.name: accessibleName
    Accessible.focusable: activeFocusOnTab

    function _revealFocusedItem() {
        const window = scroll.Window.window
        if (!window || !window.activeFocusItem) return
        let ancestor = window.activeFocusItem
        let target = ancestor
        while (ancestor && ancestor !== contentHost) {
            if (ancestor !== scroll && ancestor._qindaScrollViewport === true)
                target = ancestor
            ancestor = ancestor.parent
        }
        if (ancestor === contentHost) scroll._reveal(target)
    }
    // AGENT-GUARD (D-309): a focused item larger than the viewport on an
    // axis (a document page in a Viewport) that already shows on that axis
    // keeps its scroll position. ensureVisible() would align its far edge
    // and jump a long document to its top or bottom whenever focus returns
    // to it from a field, a popup or a dialog.
    function _reveal(item) {
        const p = item.mapToItem(contentHost, 0, 0)
        const keepY = item.height > flick.height && p.y < flick.contentY + flick.height
                      && p.y + item.height > flick.contentY
        const keepX = item.width > flick.width && p.x < flick.contentX + flick.width
                      && p.x + item.width > flick.contentX
        if (keepX && keepY) return
        const x = flick.contentX
        const y = flick.contentY
        scroll.ensureVisible(item)
        if (keepX) flick.contentX = x
        if (keepY) flick.contentY = y
    }
    Connections {
        target: scroll.Window.window
        function onActiveFocusItemChanged() { Qt.callLater(scroll._revealFocusedItem) }
    }

    function _moveBy(dx, dy) {
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        const nextX = Math.max(0, Math.min(maxX, flick.contentX + dx))
        const nextY = Math.max(0, Math.min(maxY, flick.contentY + dy))
        const moved = Math.abs(nextX - flick.contentX) > 0.5
                   || Math.abs(nextY - flick.contentY) > 0.5
        if (moved) {
            flick.contentX = nextX
            flick.contentY = nextY
        }
        return moved
    }
    Keys.onPressed: function(event) {
        if (!scroll.interactive || event.modifiers !== Qt.NoModifier) return
        const step = Tk.Theme.size.row
        const page = Math.max(step, flick.height - step)
        let dx = 0
        let dy = 0
        switch (event.key) {
        case Qt.Key_Up: dy = -step; break
        case Qt.Key_Down: dy = step; break
        case Qt.Key_Left: dx = -step; break
        case Qt.Key_Right: dx = step; break
        case Qt.Key_PageUp: dy = -page; break
        case Qt.Key_PageDown: dy = page; break
        case Qt.Key_Home: dy = -flick.contentY; break
        case Qt.Key_End: dy = flick.contentHeight - flick.height - flick.contentY; break
        default: return
        }
        event.accepted = scroll._moveBy(dx, dy)
    }

    implicitWidth: contentHost.implicitWidth + padding * 2 + contentRightInset
    implicitHeight: contentHost.implicitHeight + padding * 2

    function ensureVisible(item) {
        if (!item) return
        const p = item.mapToItem(contentHost, 0, 0)
        if (p.y < flick.contentY) flick.contentY = Math.max(0, p.y)
        else if (p.y + item.height > flick.contentY + flick.height)
            flick.contentY = Math.min(flick.contentHeight - flick.height, p.y + item.height - flick.height)
        if (p.x < flick.contentX) flick.contentX = Math.max(0, p.x)
        else if (p.x + item.width > flick.contentX + flick.width)
            flick.contentX = Math.min(flick.contentWidth - flick.width, p.x + item.width - flick.width)
    }
    function scrollToTop() { flick.contentY = 0 }
    function scrollToBottom() { flick.contentY = Math.max(0, flick.contentHeight - flick.height) }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: scroll.padding
        clip: true
        interactive: scroll.interactive
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: scroll.overflowX === Scroll.Hidden ? Flickable.VerticalFlick
                          : scroll.overflowY === Scroll.Hidden ? Flickable.HorizontalFlick
                          : Flickable.AutoFlickDirection
        contentWidth: scroll.overflowX === Scroll.Hidden ? width
                    : Math.max(width, contentHost.implicitWidth + scroll.contentRightInset)
        contentHeight: scroll.overflowY === Scroll.Hidden ? height
                     : Math.max(height, contentHost.implicitHeight)
        pixelAligned: true

        Item {
            id: contentHost
            width: Math.max(0, flick.contentWidth - scroll.contentRightInset)
            height: flick.contentHeight
            readonly property Item single: children.length === 1 ? children[0] : null
            // Implicit sizes only (see Box.qml): childrenRect would depend on
            // the viewport width and loop.
            implicitWidth: single ? single.implicitWidth : scroll.maxImplicit(children, true)
            implicitHeight: single ? single.implicitHeight : scroll.maxImplicit(children, false)
            // AGENT-GUARD: deferred so a freshly created child has its own
            // anchors and size assigned before seat() inspects them.
            onChildrenChanged: Qt.callLater(scroll.seat)
        }

        T.ScrollBar.vertical: Tk.ScrollBar {
            policy: scroll.overflowY === Scroll.Always ? T.ScrollBar.AlwaysOn
                  : scroll.overflowY === Scroll.Hidden ? T.ScrollBar.AlwaysOff : T.ScrollBar.AsNeeded
        }
        T.ScrollBar.horizontal: Tk.ScrollBar {
            policy: scroll.overflowX === Scroll.Always ? T.ScrollBar.AlwaysOn
                  : scroll.overflowX === Scroll.Hidden ? T.ScrollBar.AlwaysOff : T.ScrollBar.AsNeeded
        }
    }

    function maxImplicit(items, horizontal) {
        let size = 0
        for (let i = 0; i < items.length; ++i) {
            const value = horizontal ? items[i].implicitWidth : items[i].implicitHeight
            if (value > size) size = value
        }
        return size
    }
    function seat() {
        const single = contentHost.single
        if (single === null || Tk.LayoutInfo.hasAnchors(single)) return
        single.anchors.fill = contentHost
    }
    Component.onCompleted: seat()
}
