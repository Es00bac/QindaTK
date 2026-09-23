// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The resizable seam between two dock lanes, between a lane and the
// canvas, or between two slots of a lane (`share: true`). Pointer, keyboard
// and accessible operations report deltas; the host applies them to the
// DockModel, so this item never owns placement or extent state.
// Left/Right and Up/Down follow the seam axis; focused seams show a themed
// outline so keyboard users can tell which boundary they will move.
Rectangle {
    id: seam

    // A vertical seam separates things side by side and resizes widths.
    property bool vertical: true
    property bool share: false
    // The seam is an owner-controlled boundary: this is the increment sent
    // through `dragged`, never local layout or placement state.
    readonly property real keyboardStep: Tk.Theme.space.lg
    property string tooltip: ""
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
    activeFocusOnTab: enabled

    Accessible.role: Accessible.Splitter
    Accessible.name: seam.tooltip.length > 0 ? seam.tooltip : qsTr("Resize dock panels")
    Accessible.description: seam.vertical
                             ? qsTr("Use Left and Right to move this divider in %1 pixel steps. Increase moves it right; decrease moves it left.")
                               .arg(seam.keyboardStep)
                             : qsTr("Use Up and Down to move this divider in %1 pixel steps. Increase moves it down; decrease moves it up.")
                               .arg(seam.keyboardStep)
    Accessible.focusable: seam.enabled
    Accessible.focused: seam.activeFocus
    Accessible.onIncreaseAction: seam.moveBy(seam.keyboardStep)
    Accessible.onDecreaseAction: seam.moveBy(-seam.keyboardStep)

    function moveBy(delta) {
        if (!seam.enabled || !isFinite(delta) || delta === 0) {
            return
        }
        seam.dragged(delta)
    }

    Keys.onPressed: function(event) {
        if (!seam.enabled) {
            return
        }
        if (seam.vertical && event.key === Qt.Key_Left) {
            seam.moveBy(-seam.keyboardStep)
            event.accepted = true
        } else if (seam.vertical && event.key === Qt.Key_Right) {
            seam.moveBy(seam.keyboardStep)
            event.accepted = true
        } else if (!seam.vertical && event.key === Qt.Key_Up) {
            seam.moveBy(-seam.keyboardStep)
            event.accepted = true
        } else if (!seam.vertical && event.key === Qt.Key_Down) {
            seam.moveBy(seam.keyboardStep)
            event.accepted = true
        }
    }

    Rectangle {
        objectName: seam.objectName + "_focus"
        z: 2
        anchors.fill: parent
        anchors.margins: -Tk.Theme.size.focusRing
        color: "transparent"
        border.width: Tk.Theme.size.focusRing
        border.color: Tk.Theme.color.focus
        visible: seam.activeFocus && seam.enabled
    }

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
        enabled: seam.enabled
        margin: seam._hitMargin
        cursorShape: seam.vertical ? Qt.SplitHCursor : Qt.SplitVCursor
    }

    DragHandler {
        id: drag
        objectName: "dockDividerDragHandler"
        enabled: seam.enabled
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
