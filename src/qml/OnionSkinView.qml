// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: host supplies exact adjacent/current times; renderer owns evaluated pixels and this view retains tinted display snapshots only.
T.Control {
    id: control
    objectName: "onionSkinView"
    property Tk.MotionFrameRenderer renderer: null
    property string currentTicks: ""
    property string previousTicks: ""
    property string nextTicks: ""
    property int frameWidth: 640
    property int frameHeight: 360
    property real neighborOpacity: 0.32
    property string tooltip: qsTr("Adjacent rendered frames over current frame")
    readonly property string lastError: onion.lastError
    function refresh() { return onion.refresh() }
    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.waveformHeight * 4
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.Graphic
    Accessible.name: tooltip
    Accessible.description: lastError.length ? lastError : qsTr("Previous and next exact-time composites tint the current frame")
    HoverHandler { id: hover }
    Tk.ToolTip { visible: hover.hovered && control.tooltip.length > 0; text: control.tooltip }
    background: Rectangle { color: Tk.Theme.color.canvas; border.color: Tk.Theme.color.border; border.width: Tk.Theme.size.border; radius: Tk.Theme.radius.sm }
    contentItem: Tk.MotionOnionSkinItem {
        id: onion
        objectName: "onionSkinComposite"
        renderer: control.renderer
        currentTicks: control.currentTicks
        previousTicks: control.previousTicks
        nextTicks: control.nextTicks
        frameWidth: control.frameWidth
        frameHeight: control.frameHeight
        neighborOpacity: control.neighborOpacity
        previousTint: Tk.Theme.color.warning
        nextTint: Tk.Theme.color.accent
        Component.onCompleted: refresh()
    }
}
