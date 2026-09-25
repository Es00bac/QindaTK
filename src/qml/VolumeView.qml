// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Direct volume rendering of a Tk.VoxelVolume: a header (title, Composite /
// MIP, render-path badge, reset), the rendered volume and a camera readout.
// Drag orbits, the wheel zooms, a click without a drag picks the voxel under
// the pointer into the shared cursor. Arrow keys orbit, +/- zoom, Home resets
// and Escape cancels an orbit drag back to where it started.
// AGENT-CONTRACT (docs/volume-imaging.md#volume-rendering): the camera, mode,
// sampling and clip box are view settings (saveSettings/restoreSettings);
// the view never writes voxels, and the GPU or reference choice is reported
// in renderPath/fallbackReason by Tk.VolumeViewItem.
T.Control {
    id: control
    objectName: "volumeView"
    property alias volume: rendered.volume
    property alias cursor: rendered.cursor
    property alias transfer: rendered.transfer
    property alias channel: rendered.channel
    property alias frame: rendered.frame
    property alias interpolation: rendered.interpolation
    property alias mode: rendered.mode
    property alias yaw: rendered.yaw
    property alias pitch: rendered.pitch
    property alias zoom: rendered.zoom
    property alias samplingRate: rendered.samplingRate
    property alias clipLow: rendered.clipLow
    property alias clipHigh: rendered.clipHigh
    property alias windowCenter: rendered.windowCenter
    property alias windowWidth: rendered.windowWidth
    property alias renderer: rendered.renderer
    property alias maximumTextureBytes: rendered.maximumTextureBytes
    readonly property string renderPath: rendered.renderPath
    readonly property string fallbackReason: rendered.fallbackReason
    readonly property bool orbiting: _drag.active
    property string title: qsTr("Volume")
    property string tooltip: qsTr("Volume rendering")
    // Gesture ratios, not visual sizes: degrees per key press and per pixel
    // dragged, and the zoom factor per wheel notch or +/- press.
    property real orbitStep: 5
    property real orbitDegreesPerPixel: 0.5
    property real zoomStep: 1.25
    signal pointSelected(var voxelPoint)

    function saveSettings() { return rendered.saveSettings() }
    function restoreSettings(settings) { return rendered.restoreSettings(settings) }
    function resetView() { rendered.resetView() }
    function orbit(deltaYaw, deltaPitch) { rendered.orbit(deltaYaw, deltaPitch) }
    function pickAt(x, y) { return rendered.pick(x, y) }
    function cancelOrbit() {
        if (!_drag.active) return false
        rendered.yaw = _drag.startYaw
        rendered.pitch = _drag.startPitch
        _drag.active = false
        _drag.canceled = true
        return true
    }
    // AGENT-NOTE: a cursor without a volume adopts this view's, as in
    // ImageSliceView, so `cursor: shared` needs no extra wiring.
    function _adoptVolume() {
        if (cursor && volume && !cursor.volume) cursor.volume = volume
    }
    onCursorChanged: _adoptVolume()
    onVolumeChanged: _adoptVolume()
    Component.onCompleted: _adoptVolume()

    QtObject {
        id: _drag
        property bool pressed: false
        property bool active: false
        property bool canceled: false
        property real startX: 0
        property real startY: 0
        property real startYaw: 0
        property real startPitch: 0
    }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    padding: Tk.Theme.space.sm
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.focusable: true
    Accessible.focused: activeFocus
    Accessible.description: volume
        ? qsTr("%1 rendering, yaw %2 degrees, pitch %3 degrees, zoom %4, %5. Drag or use the arrow keys to orbit; plus and minus zoom; Home resets.")
            .arg(mode === Tk.VolumeViewItem.MaximumIntensity ? qsTr("Maximum intensity") : qsTr("Composite"))
            .arg(Math.round(yaw)).arg(Math.round(pitch)).arg(zoom.toFixed(2))
            .arg(renderPath === "gpu" ? qsTr("GPU") : qsTr("CPU reference"))
        : qsTr("No volume")
    Tk.ToolTip { text: control.tooltip; visible: hover.hovered && !_drag.active }
    HoverHandler { id: hover }

    Keys.onPressed: function(event) {
        let handled = true
        switch (event.key) {
        case Qt.Key_Left: control.orbit(-control.orbitStep, 0); break
        case Qt.Key_Right: control.orbit(control.orbitStep, 0); break
        case Qt.Key_Up: control.orbit(0, control.orbitStep); break
        case Qt.Key_Down: control.orbit(0, -control.orbitStep); break
        case Qt.Key_Plus:
        case Qt.Key_Equal: control.zoom = control.zoom * control.zoomStep; break
        case Qt.Key_Minus: control.zoom = control.zoom / control.zoomStep; break
        case Qt.Key_Home: control.resetView(); break
        case Qt.Key_Escape: handled = control.cancelOrbit(); break
        default: handled = false
        }
        event.accepted = handled
    }

    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.xs
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Label { text: control.title; Tk.Flex.grow: 1 }
            Tk.Segmented {
                objectName: "volumeViewMode"
                small: true
                tooltip: qsTr("Rendering mode")
                model: [{text: qsTr("Composite"), tooltip: qsTr("Accumulate color and opacity")},
                        {text: qsTr("MIP"), tooltip: qsTr("Maximum intensity projection")}]
                currentIndex: control.mode === Tk.VolumeViewItem.MaximumIntensity ? 1 : 0
                onActivated: function(index) {
                    control.mode = index === 1 ? Tk.VolumeViewItem.MaximumIntensity : Tk.VolumeViewItem.Composite
                }
            }
            Tk.Badge {
                objectName: "volumeViewRenderPath"
                text: control.renderPath === "gpu" ? qsTr("GPU") : qsTr("CPU")
                variant: control.renderPath === "gpu" ? "success" : "info"
                tooltip: control.renderPath === "gpu" ? qsTr("Rendered on the GPU")
                    : control.fallbackReason.length > 0 ? qsTr("CPU reference: %1").arg(control.fallbackReason)
                    : qsTr("CPU reference")
            }
            Tk.IconButton {
                objectName: "volumeViewReset"
                iconName: "rotate-ccw"
                tooltip: qsTr("Reset view")
                enabled: !!control.volume
                onClicked: control.resetView()
            }
        }
        Item {
            id: viewport
            objectName: "volumeViewViewport"
            Tk.Flex.grow: 1
            clip: true
            Tk.VolumeViewItem {
                id: rendered
                objectName: "volumeViewItem"
                anchors.fill: parent
                backgroundColor: Tk.Theme.color.panel
            }
            Tk.Caption {
                anchors.centerIn: parent
                visible: !control.volume
                text: qsTr("No volume")
            }
            MouseArea {
                objectName: "volumeViewPointer"
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                onPressed: function(mouse) {
                    control.forceActiveFocus()
                    _drag.pressed = true
                    _drag.active = false
                    _drag.canceled = false
                    _drag.startX = mouse.x
                    _drag.startY = mouse.y
                    _drag.startYaw = rendered.yaw
                    _drag.startPitch = rendered.pitch
                }
                onPositionChanged: function(mouse) {
                    if (!_drag.pressed || _drag.canceled) return
                    const dx = mouse.x - _drag.startX, dy = mouse.y - _drag.startY
                    if (!_drag.active && Math.hypot(dx, dy) < Tk.Theme.space.xs) return
                    _drag.active = true
                    rendered.yaw = _drag.startYaw + dx * control.orbitDegreesPerPixel
                    rendered.pitch = _drag.startPitch + dy * control.orbitDegreesPerPixel
                }
                onReleased: function(mouse) {
                    const wasDrag = _drag.active || _drag.canceled
                    _drag.pressed = false
                    _drag.active = false
                    _drag.canceled = false
                    if (!wasDrag && rendered.setCursorFromPick(mouse.x, mouse.y))
                        control.pointSelected([control.cursor.x, control.cursor.y, control.cursor.z])
                }
                onCanceled: control.cancelOrbit()
                onWheel: function(wheel) {
                    if (!control.volume || wheel.angleDelta.y === 0) {
                        wheel.accepted = false
                        return
                    }
                    control.zoom = control.zoom * Math.pow(control.zoomStep, wheel.angleDelta.y / 120)
                    wheel.accepted = true
                }
            }
        }
        Tk.Caption {
            objectName: "volumeViewReadout"
            text: control.volume
                ? qsTr("Yaw %1° · Pitch %2° · Zoom %3 · %4 %5 %6")
                    .arg(Math.round(control.yaw)).arg(Math.round(control.pitch)).arg(control.zoom.toFixed(2))
                    .arg(control.volume.sizeX + " × " + control.volume.sizeY + " × " + control.volume.sizeZ)
                    .arg(control.volume.voxelType)
                    .arg(control.renderPath === "gpu" ? qsTr("· GPU") : qsTr("· CPU reference"))
                : qsTr("No volume")
        }
    }
}
