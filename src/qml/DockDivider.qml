// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The draggable seam between two dock lanes, between a lane and the
// canvas, or between two slots of a lane (`share: true`). It reports
// incremental deltas; the host turns them into DockModel extents or
// shares, so a seam can never drag a lane below its declared minimum.
Rectangle {
    id: seam

    // A vertical seam separates things side by side and resizes widths.
    property bool vertical: true
    property bool share: false
    readonly property bool dragging: drag.active

    signal dragStarted()
    signal dragged(real delta)
    signal dragEnded()

    implicitWidth: vertical ? Tk.Theme.size.seam : 0
    implicitHeight: vertical ? 0 : Tk.Theme.size.seam
    color: drag.active || hover.hovered ? Tk.Theme.color.seamHover : Tk.Theme.color.seam
    Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }

    Tk.Flex.shrink: 0
    Tk.Flex.grow: 0
    readonly property real _hitMargin: Math.max(0, (Tk.Theme.size.seamTarget - (vertical ? width : height)) / 2)

    // AGENT-GUARD: expand hit testing without changing the Flex basis or
    // painted rectangle, so dock layout keeps its authored seam thickness.
    containmentMask: Item {
        readonly property real target: Math.max(Tk.Theme.size.seamTarget, Tk.Theme.size.seam)
        x: (seam.width - Math.max(seam.width, target)) / 2
        y: (seam.height - Math.max(seam.height, target)) / 2
        width: Math.max(seam.width, target)
        height: Math.max(seam.height, target)
    }

    HoverHandler {
        id: hover
        margin: seam._hitMargin
        cursorShape: seam.vertical ? Qt.SplitHCursor : Qt.SplitVCursor
    }

    DragHandler {
        id: drag
        objectName: "dockDividerDragHandler"
        target: null
        margin: seam._hitMargin
        dragThreshold: 1
        property real last: 0
        onActiveChanged: {
            if (active) {
                last = 0
                seam.dragStarted()
            } else {
                seam.dragEnded()
            }
        }
        onTranslationChanged: {
            if (!active) {
                return
            }
            const current = seam.vertical ? translation.x : translation.y
            seam.dragged(current - last)
            last = current
        }
    }
}
