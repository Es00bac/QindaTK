// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Drop targets while a panel is being dragged: translucent strips at the
// host edges (dock into that zone), a preview of the rectangle the panel
// would take, and a ghost that follows the pointer. Hit testing lives in
// DockHost (it knows the frames); this only draws `host.dropTarget`.
Item {
    id: overlay

    required property var host
    visible: host.dragging

    Repeater {
        model: overlay.host.edgeTargets
        delegate: Rectangle {
            required property var modelData
            readonly property bool hot: overlay.host.dropTarget !== null
                                        && overlay.host.dropTarget.kind === "zone"
                                        && overlay.host.dropTarget.zone === modelData.zone
            x: modelData.rect.x
            y: modelData.rect.y
            width: modelData.rect.width
            height: modelData.rect.height
            color: hot ? Tk.Theme.color.dropZone : Tk.Theme.alpha(Tk.Theme.color.accent, 0.08)
            border.width: 1
            border.color: hot ? Tk.Theme.color.accent : Tk.Theme.alpha(Tk.Theme.color.accent, 0.35)

            Tk.Icon {
                anchors.centerIn: parent
                name: modelData.zone === "left" ? "panel-left"
                    : modelData.zone === "right" ? "panel-right"
                    : modelData.zone === "top" ? "panel-top" : "panel-bottom"
                size: Tk.Theme.size.iconLg
                color: Tk.Theme.color.accent
                opacity: parent.hot ? 1.0 : 0.6
            }
        }
    }

    Rectangle {
        id: preview
        visible: overlay.host.dropTarget !== null && overlay.host.dropTarget.preview !== undefined
        x: visible ? overlay.host.dropTarget.preview.x : 0
        y: visible ? overlay.host.dropTarget.preview.y : 0
        width: visible ? overlay.host.dropTarget.preview.width : 0
        height: visible ? overlay.host.dropTarget.preview.height : 0
        color: Tk.Theme.color.dropZone
        border.width: 2
        border.color: Tk.Theme.color.accent
        radius: Tk.Theme.radius.sm

        Tk.Overline {
            anchors.centerIn: parent
            visible: overlay.host.dropTarget !== null && overlay.host.dropTarget.kind === "beside"
                     && overlay.host.dropTarget.placement === "tab"
            title: qsTr("Add as tab")
            color: Tk.Theme.color.accent
        }
    }

    Rectangle {
        id: ghost
        x: overlay.host.dragPointer.x + Tk.Theme.space.lg
        y: overlay.host.dragPointer.y + Tk.Theme.space.lg
        width: ghostLabel.implicitWidth + Tk.Theme.space.md * 2
        height: Tk.Theme.size.header
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.color.panel
        border.width: 1
        border.color: Tk.Theme.color.accent
        opacity: 0.92

        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            paddingLeft: Tk.Theme.space.sm
            paddingRight: Tk.Theme.space.sm
            Tk.Icon { name: "grip-vertical"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
            Tk.Overline {
                id: ghostLabel
                title: overlay.host.panelTitle(overlay.host.dragId)
                color: Tk.Theme.color.text
            }
        }
    }
}
