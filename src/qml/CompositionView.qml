// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: with timeTicks and a frame size set, the view requests that
// exact frame from MotionFrameRenderer and re-requests it after every
// contentInvalidated; a failed re-render keeps the previous frame, flagged by
// frameStale. Without timeTicks the host drives render() itself.
T.Control {
    id: control
    objectName: "compositionView"
    property Tk.MotionFrameRenderer renderer: null
    property string timeTicks: ""
    property int frameWidth: 0
    property int frameHeight: 0
    readonly property bool frameStale: renderer !== null && renderer.frameTicks.length > 0
        && renderer.frameRevision !== renderer.contentRevision
    property string tooltip: qsTr("Composited motion frame")
    function requestFrame() {
        if (!renderer || timeTicks.length === 0 || frameWidth < 1 || frameHeight < 1) return false
        return renderer.render(timeTicks, frameWidth, frameHeight).ok === true
    }
    onRendererChanged: Qt.callLater(control.requestFrame)
    onTimeTicksChanged: Qt.callLater(control.requestFrame)
    onFrameWidthChanged: Qt.callLater(control.requestFrame)
    onFrameHeightChanged: Qt.callLater(control.requestFrame)
    Connections {
        target: control.renderer
        function onContentInvalidated() { Qt.callLater(control.requestFrame) }
    }
    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.waveformHeight * 6
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.Graphic
    Accessible.name: tooltip
    Accessible.description: renderer && renderer.lastError.length
        ? renderer.lastError : frameStale ? qsTr("Showing an outdated frame")
        : qsTr("Rendered composition frame")
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
