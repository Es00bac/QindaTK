// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the host supplies camera picking through rayProvider and
// routes one requested stroke to MeshRigDocument.beginPaint/paintRay/endPaint.
T.Control {
    id: control
    objectName: "texturePaintView"
    property string textureDataUrl: ""
    property color brushColor: Tk.Theme.color.accent
    property int brushRadius: Tk.Theme.space.md
    property real brushOpacity: 1.0
    property var rayProvider: null // function(normalizedPosition) -> {origin, direction}
    property string tooltip: qsTr("Texture paint surface: drag to send camera rays to the host")
    readonly property bool paintingAvailable: typeof rayProvider === "function"
    signal strokeBegan()
    signal paintRayRequested(vector3d origin, vector3d direction, color color, int radiusPixels, real opacity)
    signal strokeFinished()
    signal strokeCancelled()

    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.waveformHeight * 6
    padding: Tk.Theme.space.md
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Canvas
    Accessible.name: tooltip
    Accessible.description: paintingAvailable
        ? qsTr("Texture preview with host-provided camera ray projection")
        : qsTr("Texture preview only; configure a camera ray provider to paint")

    function sendRay(point) {
        if (!paintingAvailable) return false
        const normalized = Qt.point(Math.max(0, Math.min(1, point.x / Math.max(1, surface.width))),
                                    Math.max(0, Math.min(1, point.y / Math.max(1, surface.height))))
        const ray = rayProvider(normalized)
        if (!ray || !ray.origin || !ray.direction) return false
        paintRayRequested(ray.origin, ray.direction, brushColor, brushRadius, brushOpacity)
        return true
    }
    function beginAt(point) {
        if (!paintingAvailable) return false
        strokeBegan()
        sendRay(point)
        return true
    }

    background: Rectangle { color: Tk.Theme.color.panel; border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border; border.width: Tk.Theme.size.border; radius: Tk.Theme.radius.md }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Overline { text: qsTr("Texture paint") }
            Tk.Badge { text: control.paintingAvailable ? qsTr("CAMERA READY") : qsTr("PREVIEW"); variant: control.paintingAvailable ? "success" : "warning" }
            Tk.Spacer { size: Tk.Theme.space.sm }
            Tk.Caption { text: qsTr("%1 px").arg(control.brushRadius); mono: true }
        }
        Item {
            id: surface
            objectName: "texturePaintSurface"
            Tk.Flex.flex: 1
            implicitWidth: Tk.Theme.size.panelMinWidth * 2
            implicitHeight: Tk.Theme.size.panelMinWidth * 2
            Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas; border.color: Tk.Theme.color.border; border.width: Tk.Theme.size.border }
            Image {
                objectName: "texturePreview"
                anchors.fill: parent
                anchors.margins: Tk.Theme.space.sm
                source: control.textureDataUrl
                fillMode: Image.PreserveAspectFit
                cache: false
                asynchronous: true
            }
            Rectangle {
                anchors.centerIn: parent
                width: Tk.Theme.size.iconLg
                height: width
                radius: Tk.Theme.radius.full
                color: control.brushColor
                opacity: control.brushOpacity
                border.color: Tk.Theme.color.text
                border.width: Tk.Theme.size.border
                visible: !control.paintingAvailable
            }
            TapHandler {
                onTapped: function(point) {
                    if (!control.beginAt(point.position)) return
                    control.strokeFinished()
                }
            }
            DragHandler {
                target: null
                onActiveChanged: {
                    if (active) control.beginAt(centroid.pressPosition)
                    else if (control.paintingAvailable) control.strokeFinished()
                }
                onTranslationChanged: control.sendRay(centroid.position)
                onCanceled: control.strokeCancelled()
            }
        }
        Tk.Caption {
            text: control.paintingAvailable
                ? qsTr("The host camera projects each stroke sample onto the posed mesh. Texture updates arrive from the document snapshot.")
                : qsTr("Texture preview only. Connect a camera ray provider to enable mesh painting.")
            wrapMode: Text.Wrap
        }
    }
    Keys.onEscapePressed: strokeCancelled()
}
