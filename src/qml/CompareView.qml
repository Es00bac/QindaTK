// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: both sources use one image transform. The host supplies
// local images or bounded PNG data URLs; encoded-sRGB difference is visual
// inspection, not proofing.
T.Control {
    id: control
    objectName: "compareView"
    property alias beforeSource: image.beforeSource
    property alias afterSource: image.afterSource
    property alias mode: image.mode
    property alias split: image.split
    property alias showAfter: image.showAfter
    property alias zoom: image.zoom
    property alias panX: image.panX
    property alias panY: image.panY
    readonly property string lastError: image.lastError
    property string tooltip: qsTr("Compare aligned before and after images")
    implicitWidth: Tk.Theme.size.fieldWidth * 6
    implicitHeight: Tk.Theme.size.waveformHeight * 6
    padding: Tk.Theme.space.md
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: lastError || mode
    HoverHandler { id: hover }
    Tk.ToolTip { visible: hover.hovered; text: control.tooltip }

    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Item {
        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                align: Tk.Flex.Center
                Tk.Button { objectName: "compareSplitMode"; text: qsTr("Split"); tooltip: qsTr("Compare across a draggable divider"); variant: control.mode === "split" ? "accent" : "default"; onClicked: control.mode = "split" }
                Tk.Button { objectName: "compareBlinkMode"; text: qsTr("Blink"); tooltip: qsTr("Switch between aligned sources"); variant: control.mode === "blink" ? "accent" : "default"; onClicked: control.mode = "blink" }
                Tk.Button { objectName: "compareDifferenceMode"; text: qsTr("Difference"); tooltip: qsTr("Show absolute encoded-sRGB pixel differences"); variant: control.mode === "difference" ? "accent" : "default"; onClicked: control.mode = "difference" }
                Tk.Spacer { Tk.Flex.grow: 1 }
                Tk.Button { objectName: "compareBlinkSource"; visible: control.mode === "blink"; text: control.showAfter ? qsTr("After") : qsTr("Before"); tooltip: qsTr("Switch visible comparison source"); onClicked: control.showAfter = !control.showAfter }
            }
            Item {
                id: viewport
                objectName: "compareViewport"
                width: parent.width
                Tk.Flex.grow: 1
                clip: true
                Tk.ImageCompareItem { id: image; objectName: "compareImage"; anchors.fill: parent }
                Rectangle {
                    visible: control.mode === "split"
                    x: viewport.width * control.split
                    width: Tk.Theme.size.border
                    height: viewport.height
                    color: Tk.Theme.color.accent
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    cursorShape: control.mode === "split" ? Qt.SplitHCursor
                                 : control.mode === "difference" ? Qt.OpenHandCursor : Qt.PointingHandCursor
                    property real lastX: 0
                    property real lastY: 0
                    onPressed: function(mouse) {
                        control.forceActiveFocus()
                        lastX = mouse.x; lastY = mouse.y
                        if (mouse.button === Qt.LeftButton && control.mode === "split")
                            control.split = mouse.x / viewport.width
                        else if (mouse.button === Qt.LeftButton && control.mode === "blink")
                            control.showAfter = !control.showAfter
                    }
                    onPositionChanged: function(mouse) {
                        if (mouse.buttons & Qt.MiddleButton
                                || (mouse.buttons & Qt.LeftButton && control.mode === "difference")) {
                            control.panX += mouse.x - lastX
                            control.panY += mouse.y - lastY
                        } else if (mouse.buttons & Qt.LeftButton && control.mode === "split")
                            control.split = mouse.x / viewport.width
                        lastX = mouse.x; lastY = mouse.y
                    }
                    onWheel: function(wheel) {
                        control.zoom *= Math.pow(1.2, wheel.angleDelta.y / 120)
                        wheel.accepted = true
                    }
                }
            }
            Tk.Flex {
                width: parent.width
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.md
                align: Tk.Flex.Center
                Tk.Caption { text: qsTr("Split"); visible: control.mode === "split" }
                Tk.Slider { objectName: "compareSplit"; visible: control.mode === "split"; from: 0; to: 1; value: control.split; tooltip: qsTr("Move comparison split"); Tk.Flex.grow: 1; onValueModified: control.split = value }
                Tk.Caption { text: qsTr("Zoom") }
                Tk.Slider { objectName: "compareZoom"; from: 0.25; to: 4; value: control.zoom; tooltip: qsTr("Zoom both comparison images together"); Tk.Flex.grow: 1; onValueModified: control.zoom = value }
                Tk.Mono { text: Number(control.zoom).toFixed(2) + "×" }
            }
            Tk.Caption { objectName: "compareStatus"; text: control.lastError || qsTr("Aligned %1 comparison · encoded sRGB").arg(control.mode); color: control.lastError ? Tk.Theme.color.warning : Tk.Theme.color.textMuted }
        }
    }
}
