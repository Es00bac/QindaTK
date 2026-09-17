// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: a non-modal popup placed next to `anchorItem` on the
// side `placement` names ("bottom", "top", "left", "right"), kept inside
// the window. It lives in the Overlay, so it is never clipped by a panel
// or a Scroll. open()/close() as any Popup; `opened`/`closed` are the
// template's signals.
T.Popup {
    id: popover

    property Item anchorItem: null
    property string placement: "bottom"
    property bool arrow: false
    property real gap: Tk.Theme.space.xs

    parent: T.Overlay.overlay
    modal: false
    dim: false
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    padding: Tk.Theme.space.sm
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    function reposition() {
        if (popover.anchorItem === null || popover.parent === null) {
            return
        }
        const host = popover.parent
        const origin = popover.anchorItem.mapToItem(host, 0, 0)
        const aw = popover.anchorItem.width
        const ah = popover.anchorItem.height
        const extra = popover.arrow ? Tk.Theme.space.xs : 0
        let px = origin.x
        let py = origin.y + ah + popover.gap + extra
        if (popover.placement === "top") {
            py = origin.y - popover.height - popover.gap - extra
        } else if (popover.placement === "left") {
            px = origin.x - popover.width - popover.gap - extra
            py = origin.y
        } else if (popover.placement === "right") {
            px = origin.x + aw + popover.gap + extra
            py = origin.y
        }
        const margin = Tk.Theme.space.xs
        popover.x = Math.max(margin, Math.min(px, host.width - popover.width - margin))
        popover.y = Math.max(margin, Math.min(py, host.height - popover.height - margin))
    }

    onAboutToShow: reposition()
    onWidthChanged: if (visible) reposition()
    onHeightChanged: if (visible) reposition()
    onAnchorItemChanged: if (visible) reposition()

    background: Item {
        Rectangle {
            id: surface
            anchors.fill: parent
            color: Tk.Theme.color.popoverBg
            border.width: 1
            border.color: Tk.Theme.color.popoverBorder
            radius: Tk.Theme.radius.md
        }
        // The arrow is the same surface rotated, tucked under the border.
        Rectangle {
            visible: popover.arrow
            width: Tk.Theme.space.md
            height: width
            rotation: 45
            color: Tk.Theme.color.popoverBg
            border.width: 1
            border.color: Tk.Theme.color.popoverBorder
            x: popover.placement === "left" ? parent.width - width / 2 - 1
             : popover.placement === "right" ? -width / 2 + 1
             : Math.min(parent.width - width - Tk.Theme.space.md, Tk.Theme.space.lg)
            y: popover.placement === "top" ? parent.height - height / 2 - 1
             : popover.placement === "bottom" ? -height / 2 + 1
             : Tk.Theme.space.lg
            z: -1
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Tk.Theme.motion.fast }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Tk.Theme.motion.fast }
    }
}
