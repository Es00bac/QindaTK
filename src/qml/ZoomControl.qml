// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The zoom readout of a canvas toolbar: a minus button, the mono percent
// (click to reset to 100%), a plus button. Stepping walks `presets` when
// they are given, else adds `step`. Fits inside an Island.
Item {
    id: zoom
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property real value: 1.0
    property real from: 0.1
    property real to: 8.0
    property real step: 0.25
    property var presets: [0.1, 0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4, 8]

    signal valueModified(real value)
    signal resetRequested()

    implicitWidth: row.implicitWidth
    implicitHeight: Tk.Theme.size.control

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Zoom %1%").arg(Math.round(zoom.value * 100))

    function clampValue(v) {
        return Math.max(zoom.from, Math.min(zoom.to, v))
    }
    function apply(v) {
        const next = zoom.clampValue(v)
        if (Math.abs(next - zoom.value) < 1e-6) {
            return
        }
        zoom.value = next
        zoom.valueModified(next)
    }
    function zoomIn() {
        if (!zoom.enabled) return
        if (Array.isArray(zoom.presets) && zoom.presets.length > 0) {
            for (let i = 0; i < zoom.presets.length; ++i) {
                if (zoom.presets[i] > zoom.value + 1e-6) {
                    zoom.apply(zoom.presets[i])
                    return
                }
            }
            zoom.apply(zoom.to)
            return
        }
        zoom.apply(zoom.value + zoom.step)
    }
    function zoomOut() {
        if (!zoom.enabled) return
        if (Array.isArray(zoom.presets) && zoom.presets.length > 0) {
            for (let i = zoom.presets.length - 1; i >= 0; --i) {
                if (zoom.presets[i] < zoom.value - 1e-6) {
                    zoom.apply(zoom.presets[i])
                    return
                }
            }
            zoom.apply(zoom.from)
            return
        }
        zoom.apply(zoom.value - zoom.step)
    }
    function reset() {
        if (!zoom.enabled) return
        zoom.resetRequested()
        zoom.apply(1.0)
    }

    Tk.Flex {
        id: row
        anchors.fill: parent
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs

        Tk.IconButton {
            objectName: "zoomOut"
            iconName: "minus"
            tooltip: qsTr("Zoom out")
            enabled: zoom.value > zoom.from + 1e-6
            onClicked: zoom.zoomOut()
        }
        Tk.Button {
            objectName: "zoomReadout"
            text: Math.round(zoom.value * 100) + "%"
            tooltip: qsTr("Reset zoom")
            variant: "ghost"
            Accessible.name: qsTr("Reset zoom")
            Accessible.description: qsTr("Current zoom %1%").arg(Math.round(zoom.value * 100))
            font.family: Tk.Theme.font.monoFamily
            font.pixelSize: Tk.Theme.font.body
            onClicked: zoom.reset()
            Accessible.onPressAction: if (enabled) zoom.reset()
            Keys.onReturnPressed: event => { event.accepted = true; click() }
            Keys.onEnterPressed: event => { event.accepted = true; click() }
            implicitWidth: Math.max(Tk.Theme.size.action + Tk.Theme.space.sm,
                                    text.length * Tk.Theme.font.body + Tk.Theme.space.md * 2)
            implicitHeight: Tk.Theme.size.control
            Tk.Flex.shrink: 0
        }
        Tk.IconButton {
            objectName: "zoomIn"
            iconName: "plus"
            tooltip: qsTr("Zoom in")
            enabled: zoom.value < zoom.to - 1e-6
            onClicked: zoom.zoomIn()
        }
    }
}
