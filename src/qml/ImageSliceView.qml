// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

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
    property string tooltip: qsTr("Image slice")
    readonly property int sliceCount: !volume ? 0 : plane === Tk.ImageSliceItem.Axial ? volume.sizeZ
        : plane === Tk.ImageSliceItem.Coronal ? volume.sizeY : volume.sizeX
    readonly property int sliceIndex: !cursor ? 0 : Math.round(plane === Tk.ImageSliceItem.Axial ? cursor.z
        : plane === Tk.ImageSliceItem.Coronal ? cursor.y : cursor.x)
    readonly property var selectedSample: {
        if (!volume || !cursor) return ({valid: false})
        return volume.revision >= 0
            ? volume.sample(cursor.x, cursor.y, cursor.z, channel, frame, interpolation) : ({valid: false})
    }
    signal pointSelected(var voxelPoint)

    function setSliceIndex(index) {
        if (!cursor || !volume || sliceCount < 1) return false
        const next = Math.max(0, Math.min(sliceCount - 1, Math.round(index)))
        if (plane === Tk.ImageSliceItem.Axial) return cursor.setVoxelPoint(cursor.x, cursor.y, next)
        if (plane === Tk.ImageSliceItem.Coronal) return cursor.setVoxelPoint(cursor.x, next, cursor.z)
        return cursor.setVoxelPoint(next, cursor.y, cursor.z)
    }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    padding: Tk.Theme.space.sm
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: volume
        ? qsTr("%1 slice %2 of %3; spacing %4 %5; value %6 %7")
            .arg(plane === Tk.ImageSliceItem.Axial ? qsTr("Axial")
                 : plane === Tk.ImageSliceItem.Coronal ? qsTr("Coronal") : qsTr("Sagittal"))
            .arg(sliceIndex + 1).arg(sliceCount).arg(volume.spacing.join(", ")).arg(volume.unit)
            .arg(selectedSample.valid ? selectedSample.value : qsTr("missing")).arg(volume.valueUnit)
        : qsTr("No volume")
    Tk.ToolTip { text: control.tooltip; visible: hover.hovered }
    HoverHandler { id: hover }

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
            Tk.Label {
                text: control.plane === Tk.ImageSliceItem.Axial ? qsTr("Axial")
                    : control.plane === Tk.ImageSliceItem.Coronal ? qsTr("Coronal") : qsTr("Sagittal")
                Tk.Flex.grow: 1
            }
            Tk.Mono { objectName: "imageSliceIndex"; text: control.sliceCount ? (control.sliceIndex + 1) + " / " + control.sliceCount : "—" }
            Tk.IconButton { objectName: "imageSlicePrevious"; iconName: "chevron-left"; tooltip: qsTr("Previous slice"); enabled: control.sliceIndex > 0; onClicked: control.setSliceIndex(control.sliceIndex - 1) }
            Tk.IconButton { objectName: "imageSliceNext"; iconName: "chevron-right"; tooltip: qsTr("Next slice"); enabled: control.sliceIndex + 1 < control.sliceCount; onClicked: control.setSliceIndex(control.sliceIndex + 1) }
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
                    if (!control.cursor) return
                    const point = slice.voxelAt(mouse.x, mouse.y)
                    if (point.length === 3 && control.cursor.setVoxelPoint(point[0], point[1], point[2]))
                        control.pointSelected(point)
                    control.forceActiveFocus()
                }
                onWheel: function(wheel) {
                    if (!control.volume || !control.cursor) return
                    control.setSliceIndex(control.sliceIndex + (wheel.angleDelta.y > 0 ? 1 : -1))
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
                    .arg(control.selectedSample.valid ? control.selectedSample.value.toFixed(2) : qsTr("missing"))
                    .arg(control.volume.valueUnit)
                : qsTr("No coordinate")
        }
    }
}
