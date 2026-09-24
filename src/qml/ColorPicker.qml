// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Host-owned colour editor. `color` is the host's current sRGB preview.
// User input requests previews during a pointer/key gesture, followed by one
// finish or cancel request. This control does not persist or transform colour.
Item {
    id: picker

    required property color color
    property string tooltip: qsTr("Choose an sRGB colour")
    property bool alphaEnabled: true
    readonly property color previewColor: _gestureActive ? _workingColor : color
    readonly property real hue: normalizedHue(previewColor)
    readonly property real saturation: previewColor.hsvSaturation
    readonly property real brightness: previewColor.hsvValue
    readonly property real alpha: previewColor.a

    property bool _gestureActive: false
    property color _gestureStartColor: color
    property color _workingColor: color
    property string gesturePart: ""

    signal previewRequested(color color)
    signal editFinished(color color)
    signal editCanceled(color restoredColor)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: implicitWidth + Tk.Theme.size.controlSm * 2 + Tk.Theme.space.sm * 2
    Accessible.role: Accessible.Grouping
    Accessible.name: picker.tooltip
    Accessible.description: qsTr("sRGB preview colour %1").arg(picker.previewColor)
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    function hsv(h, s, v, a) {
        h = ((h % 1) + 1) % 1
        s = Math.max(0, Math.min(1, s))
        v = Math.max(0, Math.min(1, v))
        a = Math.max(0, Math.min(1, a))
        const i = Math.floor(h * 6)
        const f = h * 6 - i
        const p = v * (1 - s)
        const q = v * (1 - f * s)
        const t = v * (1 - (1 - f) * s)
        let r = v, g = t, b = p
        switch (i % 6) {
        case 1: r = q; g = v; b = p; break
        case 2: r = p; g = v; b = t; break
        case 3: r = p; g = q; b = v; break
        case 4: r = t; g = p; b = v; break
        case 5: r = v; g = p; b = q; break
        }
        return Qt.rgba(r, g, b, a)
    }

    // Qt's QML color hsvHue is normalized to 0..1 (or -1 if achromatic).
    function normalizedHue(value) {
        return Math.max(0, Math.min(1, value.hsvHue))
    }

    function begin(part) {
        if (!enabled || _gestureActive)
            return
        _gestureActive = true
        gesturePart = part
        _gestureStartColor = color
        _workingColor = color
    }

    function preview(next) {
        if (!_gestureActive || !enabled)
            return
        _workingColor = next
        previewRequested(next)
    }

    function finish() {
        if (!_gestureActive)
            return
        _gestureActive = false
        gesturePart = ""
        if (!Qt.colorEqual(_workingColor, _gestureStartColor))
            editFinished(_workingColor)
        else
            editCanceled(_gestureStartColor)
    }

    function cancel() {
        if (!_gestureActive)
            return
        const restore = _gestureStartColor
        _workingColor = restore
        _gestureActive = false
        gesturePart = ""
        editCanceled(restore)
    }

    function updateSv(x, y, w, h) {
        const s = Math.max(0, Math.min(1, x / Math.max(1, w)))
        const v = 1 - Math.max(0, Math.min(1, y / Math.max(1, h)))
        const source = _gestureStartColor
        preview(hsv(normalizedHue(source),
                    s, v, source.a))
    }

    function updateHue(x, w) {
        const h = Math.max(0, Math.min(1, x / Math.max(1, w)))
        preview(hsv(h, _gestureStartColor.hsvSaturation,
                    _gestureStartColor.hsvValue, _gestureStartColor.a))
    }

    function updateAlpha(x, w) {
        const a = Math.max(0, Math.min(1, x / Math.max(1, w)))
        preview(hsv(normalizedHue(_gestureStartColor),
                    _gestureStartColor.hsvSaturation,
                    _gestureStartColor.hsvValue, a))
    }

    onColorChanged: if (!_gestureActive) _workingColor = color
    Keys.onEscapePressed: function(event) { cancel(); event.accepted = true }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Item {
            id: sv
            objectName: "colorPickerSaturationValue"
            width: picker.width
            height: picker.width
            implicitHeight: picker.width
            activeFocusOnTab: picker.enabled
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("Saturation and brightness")
            Accessible.description: qsTr("%1 percent saturation, %2 percent brightness")
                              .arg(Math.round(picker.previewColor.hsvSaturation * 100))
                              .arg(Math.round(picker.previewColor.hsvValue * 100))
            Accessible.focusable: picker.enabled
            Accessible.focused: activeFocus
            Accessible.onIncreaseAction: {
                picker.begin("sv")
                picker.updateSv((picker.previewColor.hsvSaturation + 0.02) * width,
                                (1 - picker.previewColor.hsvValue) * height, width, height)
                picker.finish()
            }
            Accessible.onDecreaseAction: {
                picker.begin("sv")
                picker.updateSv((picker.previewColor.hsvSaturation - 0.02) * width,
                                (1 - picker.previewColor.hsvValue) * height, width, height)
                picker.finish()
            }
            Canvas {
                id: svPaint
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    const hue = picker.normalizedHue(picker.previewColor)
                    ctx.fillStyle = picker.hsv(hue, 1, 1, 1).toString()
                    ctx.fillRect(0, 0, width, height)
                    let gradient = ctx.createLinearGradient(0, 0, width, 0)
                    gradient.addColorStop(0, "white")
                    gradient.addColorStop(1, "transparent")
                    ctx.fillStyle = gradient
                    ctx.fillRect(0, 0, width, height)
                    gradient = ctx.createLinearGradient(0, 0, 0, height)
                    gradient.addColorStop(0, "transparent")
                    gradient.addColorStop(1, "black")
                    ctx.fillStyle = gradient
                    ctx.fillRect(0, 0, width, height)
                }
            }
            Rectangle {
                objectName: "colorPickerSvMarker"
                x: Math.max(0, Math.min(parent.width - width, picker.previewColor.hsvSaturation * parent.width - width / 2))
                y: Math.max(0, Math.min(parent.height - height, (1 - picker.previewColor.hsvValue) * parent.height - height / 2))
                width: Tk.Theme.size.icon
                height: width
                radius: Tk.Theme.radius.full
                color: "transparent"
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.text
                Rectangle {
                    anchors.centerIn: parent
                    width: Tk.Theme.size.iconSm
                    height: width
                    radius: Tk.Theme.radius.full
                    color: "transparent"
                    border.width: Tk.Theme.size.border
                    border.color: Tk.Theme.color.textDisabled
                }
            }
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.focus
                visible: sv.activeFocus
            }
            MouseArea {
                anchors.fill: parent
                enabled: picker.enabled
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                onPressed: function(mouse) { picker.begin("sv"); picker.updateSv(mouse.x, mouse.y, width, height) }
                onPositionChanged: function(mouse) { if (pressed) picker.updateSv(mouse.x, mouse.y, width, height) }
                onReleased: picker.finish()
                onCanceled: picker.cancel()
            }
            Keys.onLeftPressed: function(event) { picker.begin("sv"); picker.updateSv((picker.previewColor.hsvSaturation - 0.01) * width, (1-picker.previewColor.hsvValue)*height, width, height); picker.finish(); event.accepted = true }
            Keys.onRightPressed: function(event) { picker.begin("sv"); picker.updateSv((picker.previewColor.hsvSaturation + 0.01) * width, (1-picker.previewColor.hsvValue)*height, width, height); picker.finish(); event.accepted = true }
            Keys.onUpPressed: function(event) { picker.begin("sv"); picker.updateSv(picker.previewColor.hsvSaturation * width, (1-picker.previewColor.hsvValue-0.01)*height, width, height); picker.finish(); event.accepted = true }
            Keys.onDownPressed: function(event) { picker.begin("sv"); picker.updateSv(picker.previewColor.hsvSaturation * width, (1-picker.previewColor.hsvValue+0.01)*height, width, height); picker.finish(); event.accepted = true }
            Keys.onEscapePressed: function(event) { picker.cancel(); event.accepted = true }
        }

        Item {
            id: hueRail
            objectName: "colorPickerHue"
            width: picker.width
            height: Tk.Theme.size.controlSm
            implicitHeight: Tk.Theme.size.controlSm
            activeFocusOnTab: picker.enabled
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("Hue")
            Accessible.description: qsTr("%1 degrees").arg(Math.round(picker.hue * 360))
            Accessible.focusable: picker.enabled
            Accessible.focused: activeFocus
            Accessible.onIncreaseAction: { picker.begin("hue"); picker.updateHue((picker.hue+0.01)*width,width); picker.finish() }
            Accessible.onDecreaseAction: { picker.begin("hue"); picker.updateHue((picker.hue-0.01)*width,width); picker.finish() }
            Rectangle {
                anchors.fill: parent
                radius: Tk.Theme.radius.sm
                // AGENT-NOTE: These spectrum stops are the editable sRGB hue
                // domain, not application chrome; theme roles would distort it.
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop {
                        position: 0
                        color: "#ff0000"
                    }
                    GradientStop {
                        position: 0.17
                        color: "#ffff00"
                    }
                    GradientStop {
                        position: 0.33
                        color: "#00ff00"
                    }
                    GradientStop {
                        position: 0.5
                        color: "#00ffff"
                    }
                    GradientStop {
                        position: 0.67
                        color: "#0000ff"
                    }
                    GradientStop {
                        position: 0.83
                        color: "#ff00ff"
                    }
                    GradientStop {
                        position: 1
                        color: "#ff0000"
                    }
                }
            }
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.focus
                visible: hueRail.activeFocus
            }
            Rectangle {
                objectName: "colorPickerHueMarker"
                x: Math.max(0, Math.min(parent.width - width, picker.hue * parent.width - width / 2))
                y: -Tk.Theme.space.xs
                width: Tk.Theme.size.iconSm
                height: parent.height + Tk.Theme.space.xs * 2
                radius: Tk.Theme.radius.sm
                color: "transparent"
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.text
            }
            MouseArea {
                anchors.fill: parent
                enabled: picker.enabled
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                onPressed: function(mouse) { picker.begin("hue"); picker.updateHue(mouse.x, width) }
                onPositionChanged: function(mouse) { if (pressed) picker.updateHue(mouse.x, width) }
                onReleased: picker.finish()
                onCanceled: picker.cancel()
            }
            Keys.onLeftPressed: function(event) { picker.begin("hue"); picker.updateHue((picker.hue-0.01)*width,width); picker.finish(); event.accepted=true }
            Keys.onRightPressed: function(event) { picker.begin("hue"); picker.updateHue((picker.hue+0.01)*width,width); picker.finish(); event.accepted=true }
            Keys.onEscapePressed: function(event) { picker.cancel(); event.accepted = true }
        }

        Item {
            id: alphaRail
            objectName: "colorPickerAlpha"
            width: picker.width
            height: picker.alphaEnabled ? Tk.Theme.size.controlSm : 0
            implicitHeight: picker.alphaEnabled ? Tk.Theme.size.controlSm : 0
            visible: picker.alphaEnabled
            activeFocusOnTab: picker.enabled && picker.alphaEnabled
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("Opacity")
            Accessible.description: qsTr("%1 percent").arg(Math.round(picker.previewColor.a*100))
            Accessible.focusable: picker.enabled && picker.alphaEnabled
            Accessible.focused: activeFocus
            Accessible.onIncreaseAction: { picker.begin("alpha"); picker.updateAlpha((picker.previewColor.a+0.01)*width,width); picker.finish() }
            Accessible.onDecreaseAction: { picker.begin("alpha"); picker.updateAlpha((picker.previewColor.a-0.01)*width,width); picker.finish() }
            Rectangle {
                anchors.fill: parent
                radius: Tk.Theme.radius.sm
                color: Tk.Theme.color.panel
                border.color: Tk.Theme.color.borderStrong
                border.width: Tk.Theme.size.border
                Canvas {
                    id: alphaChecker
                    anchors.fill: parent
                    onPaint: {
                        const ctx = getContext("2d")
                        const cellSize = Math.max(2, Math.floor(width / 12))
                        ctx.fillStyle = Tk.Theme.color.panel
                        ctx.fillRect(0, 0, width, height)
                        ctx.fillStyle = Tk.Theme.color.borderStrong
                        for (let y = 0; y < height; y += cellSize) {
                            for (let x = 0; x < width; x += cellSize) {
                                if (((x / cellSize) + (y / cellSize)) % 2 === 0)
                                    ctx.fillRect(x, y, cellSize, cellSize)
                            }
                        }
                    }
                    Connections {
                        target: Tk.Theme
                        function onChanged() { alphaChecker.requestPaint() }
                    }
                }
            }
            Rectangle {
                anchors.fill: parent
                radius: Tk.Theme.radius.sm
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop {
                        position: 0
                        color: Qt.rgba(picker.previewColor.r, picker.previewColor.g, picker.previewColor.b, 0)
                    }
                    GradientStop {
                        position: 1
                        color: Qt.rgba(picker.previewColor.r, picker.previewColor.g, picker.previewColor.b, 1)
                    }
                }
            }
            Rectangle {
                x: Math.max(0, Math.min(parent.width - width, picker.previewColor.a * parent.width - width / 2))
                y: -Tk.Theme.space.xs
                width: Tk.Theme.size.iconSm
                height: parent.height + Tk.Theme.space.xs * 2
                radius: Tk.Theme.radius.sm
                color: "transparent"
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.text
            }
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.focus
                visible: alphaRail.activeFocus
            }
            MouseArea {
                anchors.fill: parent
                enabled: picker.enabled && picker.alphaEnabled
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                onPressed: function(mouse) { picker.begin("alpha"); picker.updateAlpha(mouse.x, width) }
                onPositionChanged: function(mouse) { if (pressed) picker.updateAlpha(mouse.x, width) }
                onReleased: picker.finish()
                onCanceled: picker.cancel()
            }
            Keys.onLeftPressed: function(event) { picker.begin("alpha"); picker.updateAlpha((picker.previewColor.a-0.01)*width,width); picker.finish(); event.accepted=true }
            Keys.onRightPressed: function(event) { picker.begin("alpha"); picker.updateAlpha((picker.previewColor.a+0.01)*width,width); picker.finish(); event.accepted=true }
            Keys.onEscapePressed: function(event) { picker.cancel(); event.accepted = true }
        }
    }

    Connections {
        target: picker
        function onPreviewColorChanged() { svPaint.requestPaint() }
    }
    HoverHandler {
        id: pickerHover
        enabled: picker.enabled
    }
    Tk.ToolTip {
        text: picker.tooltip
        visible: picker.tooltip.length > 0 && pickerHover.hovered
        delay: Tk.Theme.motion.tooltipDelay
    }
}
