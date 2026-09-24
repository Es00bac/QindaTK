// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 1120
    height: 720
    color: Tk.Theme.color.bg
    Tk.VoxelVolume { id: phantom }
    Tk.VolumeCursor { id: crosshair }
    Tk.TransferFunctionModel { id: transfer }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Heading { text: qsTr("Volume imaging · known sphere phantom"); Tk.Flex.grow: 1 }
            Tk.Caption { text: qsTr("33 × 33 × 33 · uint8 · 1 mm voxels") }
        }
        Tk.Caption {
            text: qsTr("Three linked principal planes share one calibrated voxel cursor. Click a slice or use its arrows and wheel.")
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.md
            Tk.Flex.grow: 1
            Tk.ImageSliceView {
                objectName: "phantomAxial"
                Tk.Flex.flex: 1
                volume: phantom; cursor: crosshair; transfer: transfer
                plane: Tk.ImageSliceItem.Axial
                tooltip: qsTr("Axial sphere slice")
            }
            Tk.ImageSliceView {
                objectName: "phantomCoronal"
                Tk.Flex.flex: 1
                volume: phantom; cursor: crosshair; transfer: transfer
                plane: Tk.ImageSliceItem.Coronal
                tooltip: qsTr("Coronal sphere slice")
            }
            Tk.ImageSliceView {
                objectName: "phantomSagittal"
                Tk.Flex.flex: 1
                volume: phantom; cursor: crosshair; transfer: transfer
                plane: Tk.ImageSliceItem.Sagittal
                tooltip: qsTr("Sagittal sphere slice")
            }
        }
        Tk.TransferFunctionEditor {
            objectName: "phantomTransferEditor"
            model: transfer
            height: Tk.Theme.size.waveformHeight + Tk.Theme.size.controlLg + Tk.Theme.space.lg
        }
        Tk.Caption {
            text: qsTr("World point: %1, %2, %3 %4 · Center value %5 %6")
                .arg(crosshair.worldPoint[0]).arg(crosshair.worldPoint[1])
                .arg(crosshair.worldPoint[2]).arg(phantom.unit)
                .arg(phantom.sample(crosshair.x, crosshair.y, crosshair.z).value).arg(phantom.valueUnit)
        }
    }

    Component.onCompleted: {
        phantom.configure({sizeX: 33, sizeY: 33, sizeZ: 33,
                           voxelType: "uint8", spacing: [1, 1, 1],
                           origin: [0, 0, 0], unit: "mm", valueUnit: "a.u."})
        phantom.fillSpherePhantom()
        crosshair.volume = phantom
        transfer.configure(0, 255, "a.u.")
        transfer.replacePoints([
            {id: "clear", value: 0, color: Qt.rgba(0.05, 0.12, 0.18, 1), opacity: 0},
            {id: "shell", value: 110, color: Qt.rgba(0.20, 0.75, 0.95, 1), opacity: 0.9},
            {id: "core", value: 220, color: Qt.rgba(1.0, 0.55, 0.28, 1), opacity: 1},
            {id: "cap", value: 255, color: Qt.rgba(1, 1, 1, 1), opacity: 1}
        ])
    }
}
