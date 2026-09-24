// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    id: control
    objectName: "compositionView"
    property Tk.MotionFrameRenderer renderer: null
    property string tooltip: qsTr("Composited motion frame")
    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.waveformHeight * 6
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.Graphic
    Accessible.name: tooltip
    Accessible.description: renderer && renderer.lastError.length
        ? renderer.lastError : qsTr("Rendered composition frame")
    HoverHandler { id: hover }
    Tk.ToolTip { visible: hover.hovered && control.tooltip.length > 0; text: control.tooltip }
    background: Rectangle {
        color: Tk.Theme.color.canvas
        border.color: Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.MotionFrameItem {
        objectName: "compositionFrame"
        renderer: control.renderer
    }
}
