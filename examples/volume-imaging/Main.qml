// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A known sphere phantom in three linked, calibrated principal planes. The
// voxels are anisotropic (0.8 × 0.8 × 1.6 mm), so round cross-sections in
// every plane show that the views fit physical spacing, not voxel counts.
// The centre voxel (20, 20, 10) sits at world (0, 0, 0) mm; the shell
// (110 a.u.) reaches 10.496 mm and the core (220 a.u.) 5.773 mm.
Rectangle {
    width: 1120
    height: 720
    color: Tk.Theme.color.bg
    Tk.VoxelVolume { id: phantom }
    Tk.VolumeCursor { id: crosshair; volume: phantom }
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
            Tk.Caption {
                objectName: "phantomShape"
                text: qsTr("%1 × %2 × %3 · %4 · %5 %6 voxels")
                    .arg(phantom.sizeX).arg(phantom.sizeY).arg(phantom.sizeZ).arg(phantom.voxelType)
                    .arg(phantom.spacing.join(" × ")).arg(phantom.unit)
            }
        }
        Tk.Caption {
            text: qsTr("Three principal planes share one calibrated cursor. Click a slice, scroll or press Page Up/Down to change slice, and move the crosshair with the arrow keys.")
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
            selectedPointId: "shell"
        }
        Tk.Caption {
            objectName: "phantomReadout"
            text: {
                const world = crosshair.worldPoint
                const sample = phantom.revision >= 0
                    ? phantom.sample(crosshair.x, crosshair.y, crosshair.z) : ({valid: false})
                return qsTr("World point: %1 %2 · Cursor value %3 %4")
                    .arg(world.map(function(v) { return Number(v).toFixed(2) }).join(", "))
                    .arg(phantom.unit)
                    .arg(sample.valid ? sample.value : qsTr("missing"))
                    .arg(phantom.valueUnit)
            }
        }
    }

    Component.onCompleted: {
        phantom.configure({sizeX: 41, sizeY: 41, sizeZ: 21, voxelType: "uint8",
                           spacing: [0.8, 0.8, 1.6], origin: [-16, -16, -16],
                           unit: "mm", valueUnit: "a.u."})
        phantom.fillSpherePhantom()
        transfer.configure(0, 255, "a.u.")
        transfer.replacePoints([
            {id: "clear", value: 0, color: Qt.rgba(0.05, 0.12, 0.18, 1), opacity: 0},
            {id: "shell", value: 110, color: Qt.rgba(0.20, 0.75, 0.95, 1), opacity: 0.9},
            {id: "core", value: 220, color: Qt.rgba(1.0, 0.55, 0.28, 1), opacity: 1},
            {id: "cap", value: 255, color: Qt.rgba(1, 1, 1, 1), opacity: 1}
        ])
    }
}
