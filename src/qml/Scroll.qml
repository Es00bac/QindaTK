// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: CSS `overflow: auto`. The single content child is sized
// to its implicit size on a scrolling axis and to the viewport on a hidden
// axis, so `overflowX: Scroll.Hidden` (the default) makes a Column wrap to
// the width and scroll vertically. Scrollbars are overlays and take no
// layout space. Wheel, drag-flick and keyboard (when focused) all scroll.
Item {
    id: scroll

    enum Overflow { Auto, Hidden, Always }

    default property alias content: contentHost.data
    readonly property Item contentItem: contentHost
    readonly property alias flickable: flick
    property int overflowX: Scroll.Hidden
    property int overflowY: Scroll.Auto
    property real padding: 0
    property alias contentX: flick.contentX
    property alias contentY: flick.contentY
    readonly property alias contentWidth: flick.contentWidth
    readonly property alias contentHeight: flick.contentHeight
    readonly property real viewportWidth: flick.width
    readonly property real viewportHeight: flick.height
    readonly property bool scrollableX: flick.contentWidth > flick.width + 0.5
    readonly property bool scrollableY: flick.contentHeight > flick.height + 0.5
    property bool interactive: true

    implicitWidth: contentHost.implicitWidth + padding * 2
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
                    : Math.max(width, contentHost.implicitWidth)
        contentHeight: scroll.overflowY === Scroll.Hidden ? height
                     : Math.max(height, contentHost.implicitHeight)
        pixelAligned: true

        Item {
            id: contentHost
            width: flick.contentWidth
            height: flick.contentHeight
            readonly property Item single: children.length === 1 ? children[0] : null
            // Implicit sizes only (see Box.qml): childrenRect would depend on
            // the viewport width and loop.
            implicitWidth: single && single.visible ? single.implicitWidth : scroll.maxImplicit(children, true)
            implicitHeight: single && single.visible ? single.implicitHeight : scroll.maxImplicit(children, false)
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

    // AGENT-GUARD: invisible children take no slot (docs/layout.md) — the
    // rule Flex, Grid and Stack enforce in C++ (takesSlot); the single-child
    // branch above falls through to here when its child is hidden.
    function maxImplicit(items, horizontal) {
        let size = 0
        for (let i = 0; i < items.length; ++i) {
            if (!items[i].visible) continue
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
