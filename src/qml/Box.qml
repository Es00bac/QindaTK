// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: the CSS box. Background colour, uniform or per-side
// border, radius, padding, and a padding box (`contentItem`) that one
// child fills automatically (`fill: true`, the default) or several children
// anchor inside. Implicit size is the content's implicit size plus padding
// and border, so a Box in a Flex or Grid asks for exactly what it holds.
// Per-side borders (borderLeft/Top/Right/Bottom >= 0) are drawn as edge
// rectangles and ignore `radius`; a uniform border honours it.
Item {
    id: box

    default property alias content: contentHost.data
    readonly property Item contentItem: contentHost

    property color color: "transparent"
    property color borderColor: Tk.Theme.color.border
    property real borderWidth: 0
    // -1 inherits borderWidth. Any side >= 0 switches to per-side drawing.
    property real borderLeft: -1
    property real borderTop: -1
    property real borderRight: -1
    property real borderBottom: -1
    property real radius: 0
    property real padding: 0
    property real paddingLeft: -1
    property real paddingTop: -1
    property real paddingRight: -1
    property real paddingBottom: -1
    // When true and the box holds exactly one Item, that Item fills the
    // padding box (its own anchors are replaced).
    property bool fill: true
    property bool clipContent: false
    // Optional hover/pressed tinting, so a Box can be a list row or a card.
    property bool interactive: false
    property bool hovered: hoverHandler.hovered
    readonly property alias pressed: tapHandler.pressed
    signal clicked(var eventPoint)
    signal rightClicked(var eventPoint)
    signal doubleClicked(var eventPoint)

    readonly property bool perSide: borderLeft >= 0 || borderTop >= 0 || borderRight >= 0 || borderBottom >= 0
    readonly property real edgeLeft: (borderLeft >= 0 ? borderLeft : borderWidth)
    readonly property real edgeTop: (borderTop >= 0 ? borderTop : borderWidth)
    readonly property real edgeRight: (borderRight >= 0 ? borderRight : borderWidth)
    readonly property real edgeBottom: (borderBottom >= 0 ? borderBottom : borderWidth)
    readonly property real insetLeft: edgeLeft + (paddingLeft >= 0 ? paddingLeft : padding)
    readonly property real insetTop: edgeTop + (paddingTop >= 0 ? paddingTop : padding)
    readonly property real insetRight: edgeRight + (paddingRight >= 0 ? paddingRight : padding)
    readonly property real insetBottom: edgeBottom + (paddingBottom >= 0 ? paddingBottom : padding)

    implicitWidth: contentHost.implicitWidth + insetLeft + insetRight
    implicitHeight: contentHost.implicitHeight + insetTop + insetBottom

    Rectangle {
        id: background
        anchors.fill: parent
        color: box.color
        radius: box.radius
        border.width: box.perSide ? 0 : box.borderWidth
        border.color: box.borderColor
        antialiasing: box.radius > 0
    }
    Rectangle { visible: box.perSide && box.edgeLeft > 0; color: box.borderColor; width: box.edgeLeft
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom } }
    Rectangle { visible: box.perSide && box.edgeRight > 0; color: box.borderColor; width: box.edgeRight
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom } }
    Rectangle { visible: box.perSide && box.edgeTop > 0; color: box.borderColor; height: box.edgeTop
        anchors { left: parent.left; right: parent.right; top: parent.top } }
    Rectangle { visible: box.perSide && box.edgeBottom > 0; color: box.borderColor; height: box.edgeBottom
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom } }

    Item {
        id: contentHost
        anchors.fill: parent
        anchors.leftMargin: box.insetLeft
        anchors.topMargin: box.insetTop
        anchors.rightMargin: box.insetRight
        anchors.bottomMargin: box.insetBottom
        clip: box.clipContent
        readonly property Item single: children.length === 1 ? children[0] : null
        // AGENT-GUARD: implicit size comes from the children's *implicit*
        // sizes, never from childrenRect: geometry of anchored children
        // depends on this box's width, and an owner that binds
        // `width: implicitWidth` would otherwise form a binding loop.
        implicitWidth: single && single.visible ? single.implicitWidth : box.maxImplicit(children, true)
        implicitHeight: single && single.visible ? single.implicitHeight : box.maxImplicit(children, false)

        // AGENT-GUARD: deferred so a freshly created child has its own
        // anchors and size assigned before seat() inspects them.
        onChildrenChanged: Qt.callLater(box.seat)
    }

    // AGENT-GUARD: a child that anchors itself (an Island pinned to a
    // corner, a centred overlay) keeps its anchors; only an unanchored
    // single child is stretched to the padding box.
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
        if (single === null || !box.fill || Tk.LayoutInfo.hasAnchors(single)) {
            return
        }
        const fixedW = Tk.LayoutInfo.hasExplicitWidth(single)
        const fixedH = Tk.LayoutInfo.hasExplicitHeight(single)
        if (!fixedW && !fixedH) {
            single.anchors.fill = contentHost
        } else if (!fixedW) {
            single.anchors.left = contentHost.left
            single.anchors.right = contentHost.right
        } else if (!fixedH) {
            single.anchors.top = contentHost.top
            single.anchors.bottom = contentHost.bottom
        }
    }
    onFillChanged: seat()
    Component.onCompleted: seat()

    HoverHandler {
        id: hoverHandler
        enabled: box.interactive
    }
    TapHandler {
        id: tapHandler
        enabled: box.interactive
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onTapped: function(eventPoint, button) {
            if (button === Qt.RightButton) {
                box.rightClicked(eventPoint)
            } else {
                box.clicked(eventPoint)
            }
        }
        onDoubleTapped: function(eventPoint, button) { box.doubleClicked(eventPoint) }
    }
}
