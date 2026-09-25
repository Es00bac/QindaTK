// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (D-179): this surface paints in texture space. The pointer
// maps to the UV of the texel it shows (the fitted texture rectangle below),
// and one requested stroke goes to MeshRigDocument.beginPaint(channel) /
// paintUv / endPaint. Painting from the camera view happens in SceneView3D
// (meshTool "paint"), whose rays pass through the pixel under the pointer;
// both share this view's brush and channel (docs/mesh-rig.md#painting).
T.Control {
    id: control
    objectName: "texturePaintView"
    property string textureDataUrl: ""
    property int textureWidth: 1
    property int textureHeight: 1
    // Document channel names; MeshRigDocument.textureChannels is the source.
    property var channels: ["baseColor", "roughness", "metalness"]
    property string channel: "baseColor"
    property color brushColor: Tk.Theme.color.accent
    property int brushRadius: Tk.Theme.space.md
    property real brushOpacity: 1.0
    property real brushHardness: 1.0
    property bool paintEnabled: false
    property string tooltip: qsTr("Texture paint surface: drag to paint the texel under the pointer")
    // Where the texture is drawn inside the surface (aspect-fitted).
    readonly property rect textureRect: {
        const inset = Tk.Theme.space.sm
        const w = Math.max(0, surface.width - inset * 2), h = Math.max(0, surface.height - inset * 2)
        const aspect = Math.max(1, textureWidth) / Math.max(1, textureHeight)
        const fitW = Math.min(w, h * aspect), fitH = fitW / aspect
        return Qt.rect(inset + (w - fitW) / 2, inset + (h - fitH) / 2, fitW, fitH)
    }
    signal strokeBegan(string channel)
    signal paintUvRequested(point uv, color color, int radiusPixels, real opacity, real hardness)
    signal strokeFinished()
    signal strokeCancelled()
    signal channelSelected(string channel)

    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.waveformHeight * 6
    padding: Tk.Theme.space.md
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Canvas
    Accessible.name: tooltip
    Accessible.description: paintEnabled
        ? qsTr("Texture of the %1 channel; dragging paints the texel under the pointer").arg(channel)
        : qsTr("Texture preview only")

    function channelLabel(name) {
        return name === "baseColor" ? qsTr("Color") : name === "roughness" ? qsTr("Roughness")
            : name === "metalness" ? qsTr("Metal") : name
    }
    function selectChannel(name) {
        if ((channels || []).indexOf(name) < 0) return false
        channel = name // view state; the stroke names its channel in strokeBegan
        channelSelected(name)
        return true
    }
    // UV (v up) of the surface point, or null outside the drawn texture.
    function uvAt(point) {
        const r = textureRect
        if (r.width <= 0 || r.height <= 0 || point.x < r.x || point.y < r.y
                || point.x > r.x + r.width || point.y > r.y + r.height) return null
        return Qt.point((point.x - r.x) / r.width, 1 - (point.y - r.y) / r.height)
    }
    function sendUv(point) {
        const uv = paintEnabled ? uvAt(point) : null
        if (!uv) return false
        paintUvRequested(uv, brushColor, brushRadius, brushOpacity, brushHardness)
        return true
    }
    function beginAt(point) {
        if (!paintEnabled) return false
        strokeBegan(channel)
        sendUv(point)
        return true
    }

    background: Rectangle { color: Tk.Theme.color.panel; border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border; border.width: Tk.Theme.size.border; radius: Tk.Theme.radius.md }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.Overline { text: qsTr("Texture paint") }
            Tk.Badge { text: control.paintEnabled ? qsTr("UV PAINT") : qsTr("PREVIEW"); variant: control.paintEnabled ? "success" : "warning" }
            Tk.Spacer { size: Tk.Theme.space.sm }
            Tk.Segmented {
                objectName: "texturePaintChannels"
                small: true
                tooltip: qsTr("Material channel to paint")
                model: (control.channels || []).map(name => ({ text: control.channelLabel(name), value: name }))
                currentIndex: Math.max(0, (control.channels || []).indexOf(control.channel))
                onActivated: function(index) { control.selectChannel(control.channels[index]) }
            }
            Tk.Caption { text: qsTr("%1 px · hardness %2").arg(control.brushRadius).arg(control.brushHardness.toFixed(2)); mono: true }
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
                x: control.textureRect.x
                y: control.textureRect.y
                width: control.textureRect.width
                height: control.textureRect.height
                source: control.textureDataUrl
                fillMode: Image.Stretch
                smooth: false
                cache: false
                asynchronous: true
            }
            Rectangle {
                objectName: "texturePaintBrush"
                width: control.brushRadius * 2 * control.textureRect.width / Math.max(1, control.textureWidth)
                height: width
                radius: width / 2
                x: hover.point.position.x - width / 2
                y: hover.point.position.y - height / 2
                color: "transparent"
                border.color: control.brushColor
                border.width: Tk.Theme.size.border
                visible: control.paintEnabled && hover.hovered && control.uvAt(hover.point.position) !== null
            }
            HoverHandler { id: hover }
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
                    else if (control.paintEnabled) control.strokeFinished()
                }
                onTranslationChanged: control.sendUv(centroid.position)
                onCanceled: control.strokeCancelled()
            }
        }
        Tk.Caption {
            text: control.paintEnabled
                ? qsTr("Drag to paint the texel under the pointer; each dab is a sphere on the posed mesh, so it continues across UV seams. Paint from the camera with the 3D view's Paint tool.")
                : qsTr("Texture preview only. The host enables painting when it routes strokes to a mesh document.")
            wrapMode: Text.Wrap
        }
    }
    Keys.onEscapePressed: strokeCancelled()
}
