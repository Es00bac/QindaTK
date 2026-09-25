// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// One calibrated principal plane of a Tk.VoxelVolume, or an oblique world
// reslice through the cursor (plane Oblique, resliceOrientation): a header
// (plane, slice n / N, previous/next), the slice fitted by physical spacing
// with the shared crosshair, and a voxel/value readout. Click moves the shared cursor; wheel,
// Page Up/Down, Home/End and the chevrons change slice; arrow keys move the
// crosshair one voxel within the plane.
// AGENT-CONTRACT (docs/volume-imaging.md): all principal planes use one
// VolumeCursor in voxel coordinates; the provider owns physical transforms.
T.Control {
    id: control
    objectName: "imageSliceView"
    property alias volume: slice.volume
    property alias cursor: slice.cursor
    property alias transfer: slice.transfer
    property alias plane: slice.plane
    property alias channel: slice.channel
    property alias frame: slice.frame
    property alias interpolation: slice.interpolation
    property alias windowCenter: slice.windowCenter
    property alias windowWidth: slice.windowWidth
    property alias resliceOrientation: slice.resliceOrientation
    property string tooltip: qsTr("Image slice")
    readonly property bool oblique: plane === Tk.ImageSliceItem.Oblique
    readonly property string planeName: plane === Tk.ImageSliceItem.Axial ? qsTr("Axial")
        : plane === Tk.ImageSliceItem.Coronal ? qsTr("Coronal")
        : oblique ? qsTr("Oblique") : qsTr("Sagittal")
    // AGENT-NOTE: an oblique plane steps by the smallest voxel spacing along
    // its normal; ImageSliceItem owns that count and index (D-149).
    readonly property int sliceCount: !volume ? 0 : oblique ? slice.sliceCount
        : plane === Tk.ImageSliceItem.Axial ? volume.sizeZ
        : plane === Tk.ImageSliceItem.Coronal ? volume.sizeY : volume.sizeX
    readonly property int sliceIndex: oblique ? slice.sliceIndex
        : !cursor ? Math.max(0, Math.round((sliceCount - 1) / 2))
        : Math.round(plane === Tk.ImageSliceItem.Axial ? cursor.z
                     : plane === Tk.ImageSliceItem.Coronal ? cursor.y : cursor.x)
    readonly property var selectedSample: {
        if (!volume || !cursor) return ({valid: false})
        return volume.revision >= 0
            ? volume.sample(cursor.x, cursor.y, cursor.z, channel, frame, interpolation) : ({valid: false})
    }
    signal pointSelected(var voxelPoint)

    property real _wheelDelta: 0

    function setSliceIndex(index) {
        if (!cursor || !volume || sliceCount < 1) return false
        const next = Math.max(0, Math.min(sliceCount - 1, Math.round(index)))
        if (oblique) return next !== sliceIndex && slice.stepSlice(next - sliceIndex)
        if (plane === Tk.ImageSliceItem.Axial) return cursor.setVoxelPoint(cursor.x, cursor.y, next)
        if (plane === Tk.ImageSliceItem.Coronal) return cursor.setVoxelPoint(cursor.x, next, cursor.z)
        return cursor.setVoxelPoint(next, cursor.y, cursor.z)
    }
    // Moves the crosshair by whole voxels along this plane's horizontal (dh)
    // and downward vertical (dv) axes, snapping the point to voxel centres.
    function moveCrosshair(dh, dv) {
        if (!cursor || !volume) return false
        if (oblique) return slice.moveCrosshair(dh, dv)
        const x = Math.round(cursor.x), y = Math.round(cursor.y), z = Math.round(cursor.z)
        if (plane === Tk.ImageSliceItem.Axial) return cursor.setVoxelPoint(x + dh, y + dv, z)
        if (plane === Tk.ImageSliceItem.Coronal) return cursor.setVoxelPoint(x + dh, y, z + dv)
        return cursor.setVoxelPoint(x, y + dh, z + dv)
    }
    function _formatValue(sample) {
        return sample.valid ? String(+Number(sample.value).toFixed(3)) : qsTr("missing")
    }
    // AGENT-NOTE: a cursor without a volume adopts this view's, so a
    // declarative `cursor: shared` works without also wiring cursor.volume.
    function _adoptVolume() {
        if (cursor && volume && !cursor.volume) cursor.volume = volume
    }
    onCursorChanged: _adoptVolume()
    onVolumeChanged: _adoptVolume()
    Component.onCompleted: _adoptVolume()

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    padding: Tk.Theme.space.sm
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.focusable: true
    Accessible.focused: activeFocus
    Accessible.description: volume && cursor
        ? qsTr("%1 slice %2 of %3; spacing %4 %5; cursor %6 %5; value %7 %8. Arrow keys move the crosshair; Page Up and Page Down change slice.")
            .arg(planeName).arg(sliceIndex + 1).arg(sliceCount).arg(volume.spacing.join(", ")).arg(volume.unit)
            .arg(cursor.worldPoint.map(function(v) { return Number(v).toFixed(2) }).join(", "))
            .arg(_formatValue(selectedSample)).arg(volume.valueUnit)
        : volume ? qsTr("%1 slice %2 of %3").arg(planeName).arg(sliceIndex + 1).arg(sliceCount)
        : qsTr("No volume")
    Tk.ToolTip { text: control.tooltip; visible: hover.hovered }
    HoverHandler { id: hover }

    Keys.onPressed: function(event) {
        let handled = false
        switch (event.key) {
        case Qt.Key_PageUp: handled = control.setSliceIndex(control.sliceIndex + 1); break
        case Qt.Key_PageDown: handled = control.setSliceIndex(control.sliceIndex - 1); break
        case Qt.Key_Home: handled = control.setSliceIndex(0); break
        case Qt.Key_End: handled = control.setSliceIndex(control.sliceCount - 1); break
        case Qt.Key_Left: handled = control.moveCrosshair(-1, 0); break
        case Qt.Key_Right: handled = control.moveCrosshair(1, 0); break
        case Qt.Key_Up: handled = control.moveCrosshair(0, -1); break
        case Qt.Key_Down: handled = control.moveCrosshair(0, 1); break
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
            Tk.Label { text: control.planeName; Tk.Flex.grow: 1 }
            Tk.Mono { objectName: "imageSliceIndex"; text: control.sliceCount ? (control.sliceIndex + 1) + " / " + control.sliceCount : "—" }
            Tk.IconButton { objectName: "imageSlicePrevious"; iconName: "chevron-left"; tooltip: qsTr("Previous slice"); enabled: !!control.cursor && control.sliceIndex > 0; onClicked: control.setSliceIndex(control.sliceIndex - 1) }
            Tk.IconButton { objectName: "imageSliceNext"; iconName: "chevron-right"; tooltip: qsTr("Next slice"); enabled: !!control.cursor && control.sliceIndex + 1 < control.sliceCount; onClicked: control.setSliceIndex(control.sliceIndex + 1) }
        }
        Item {
            id: viewport
            objectName: "imageSliceViewport"
            Tk.Flex.grow: 1
            clip: true
            Tk.ImageSliceItem {
                id: slice
                objectName: "imageSliceImage"
                anchors.fill: parent
                backgroundColor: Tk.Theme.color.panel
                crosshairColor: Tk.Theme.color.accent
                crosshairWidth: Tk.Theme.size.border
            }
            Tk.Caption {
                anchors.centerIn: parent
                visible: !control.volume
                text: qsTr("No volume")
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onClicked: function(mouse) {
                    control.forceActiveFocus()
                    if (!control.cursor) return
                    const point = slice.voxelAt(mouse.x, mouse.y)
                    if (point.length === 3 && control.cursor.setVoxelPoint(point[0], point[1], point[2]))
                        control.pointSelected([control.cursor.x, control.cursor.y, control.cursor.z])
                }
                onWheel: function(wheel) {
                    // AGENT-NOTE: one slice per 120 angle units. Touchpad deltas
                    // accumulate; a purely horizontal scroll must not step
                    // backwards, so it passes to the parent instead.
                    if (!control.volume || !control.cursor || wheel.angleDelta.y === 0) {
                        wheel.accepted = false
                        return
                    }
                    control._wheelDelta += wheel.angleDelta.y
                    const steps = control._wheelDelta > 0 ? Math.floor(control._wheelDelta / 120)
                                                          : Math.ceil(control._wheelDelta / 120)
                    if (steps !== 0) {
                        control._wheelDelta -= steps * 120
                        control.setSliceIndex(control.sliceIndex + steps)
                    }
                    wheel.accepted = true
                }
            }
        }
        Tk.Caption {
            objectName: "imageSliceCoordinate"
            text: control.cursor && control.volume
                ? qsTr("Voxel %1, %2, %3  ·  %4 %5")
                    .arg(control.cursor.x.toFixed(1)).arg(control.cursor.y.toFixed(1))
                    .arg(control.cursor.z.toFixed(1))
                    .arg(control._formatValue(control.selectedSample))
                    .arg(control.volume.valueUnit)
                : qsTr("No coordinate")
        }
    }
}
