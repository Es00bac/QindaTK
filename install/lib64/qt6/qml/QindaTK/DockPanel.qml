// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: a panel declaration inside a Tk.DockHost. Nothing here is
// drawn. The host registers `definition()` with the DockModel and moves
// `contentItem` (the Item holding the declared children) into whatever
// frame the arrangement puts the panel in — docked, tabbed, floating —
// so the content keeps its state (scroll position, selection, text) across
// every move. A hidden panel's content simply lives here, invisible.
//
//     Tk.DockPanel { panelId: "layers"; title: "Layers"; zone: "right"
//                    Tk.Scroll { ... } }
Item {
    id: declaration

    default property alias content: contentHost.data
    readonly property Item contentItem: contentHost
    readonly property bool isDockPanel: true

    // Identity and defaults (see DockModel::registerPanel). `order: -1`
    // takes the declaration index; `floatingRect`/`allowedZones` are
    // optional maps/lists.
    property string panelId: ""
    property string title: ""
    property string iconName: ""
    property string mode: "docked"
    property string zone: "right"
    property int lane: 0
    property int order: -1
    property real extent: 280
    property real share: 1
    property real minWidth: 160
    property real minHeight: 100
    property var floatingRect: undefined
    property var allowedZones: undefined
    property bool closable: true
    property bool fixedSize: false
    // "default" (grip + title + buttons), "compact" (no grip), "bare" (no header).
    property string chrome: "default"
    property string group: ""
    property bool groupActive: true
    property real padding: Tk.Theme.space.sm
    property bool collapsible: true

    visible: false
    width: 0
    height: 0

    function definition(index) {
        const d = {
            "title": declaration.title.length > 0 ? declaration.title : declaration.panelId,
            "mode": declaration.mode,
            "zone": declaration.zone,
            "lane": declaration.lane,
            "order": declaration.order >= 0 ? declaration.order : index,
            "extent": declaration.extent,
            "share": declaration.share,
            "minWidth": declaration.minWidth,
            "minHeight": declaration.minHeight,
            "closable": declaration.closable,
            "fixedSize": declaration.fixedSize,
            "chrome": declaration.chrome,
            "group": declaration.group,
            "groupActive": declaration.groupActive
        }
        if (declaration.floatingRect !== undefined && declaration.floatingRect !== null) {
            d["floatingRect"] = declaration.floatingRect
        }
        if (declaration.allowedZones !== undefined && declaration.allowedZones !== null) {
            d["allowedZones"] = declaration.allowedZones
        }
        return d
    }

    // Returns the content to this declaration (used by frames on teardown).
    function reclaim() {
        if (contentHost.parent !== declaration) {
            contentHost.anchors.fill = undefined
            contentHost.parent = declaration
        }
        contentHost.visible = true
    }

    // AGENT-CONTRACT: the same seating policy as Tk.Box — a single child
    // that neither anchors itself nor sets an explicit size fills the
    // frame's content area; several children position themselves.
    function seat() {
        const single = contentHost.children.length === 1 ? contentHost.children[0] : null
        if (single === null || Tk.LayoutInfo.hasAnchors(single)) {
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
    Component.onCompleted: seat()

    Item {
        id: contentHost
        objectName: "dockPanelContent_" + declaration.panelId
        implicitWidth: children.length === 1 ? children[0].implicitWidth : childrenRect.width
        implicitHeight: children.length === 1 ? children[0].implicitHeight : childrenRect.height
        onChildrenChanged: Qt.callLater(declaration.seat)
    }
}
