// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A 25³ sphere phantom in two linked planes and a volume rendering: the axial
// view and the volume map values through the edited transfer function, the
// sagittal view uses window/level.
Section {
    title: "Volume imaging"
    note: "ImageSliceView · VolumeView · VolumeCursor · TransferFunctionEditor"
    Tk.VoxelVolume { id: phantom }
    Tk.VolumeCursor { id: crosshair; volume: phantom }
    Tk.TransferFunctionModel { id: transfer }
    Component.onCompleted: {
        phantom.configure({sizeX: 25, sizeY: 25, sizeZ: 25, voxelType: "uint8",
                           spacing: [1, 1, 1], unit: "mm", valueUnit: "a.u."})
        phantom.fillSpherePhantom()
        transfer.configure(0, 255, "a.u.")
        transfer.replacePoints([
            {id: "air", value: 0, color: "#0d1f2d", opacity: 0},
            {id: "shell", value: 110, color: "#33bff2", opacity: 0.4},
            {id: "core", value: 220, color: "#ff8c47", opacity: 1},
            {id: "cap", value: 255, color: "#ffffff", opacity: 1}
        ])
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.md
        Tk.ImageSliceView {
            objectName: "galleryAxialSlice"
            implicitWidth: 300
            implicitHeight: 260
            volume: phantom
            cursor: crosshair
            transfer: transfer
            plane: Tk.ImageSliceItem.Axial
            tooltip: qsTr("Axial phantom slice")
        }
        Tk.ImageSliceView {
            objectName: "gallerySagittalSlice"
            implicitWidth: 300
            implicitHeight: 260
            volume: phantom
            cursor: crosshair
            plane: Tk.ImageSliceItem.Sagittal
            windowCenter: 110
            windowWidth: 240
            tooltip: qsTr("Sagittal phantom slice, window/level")
        }
        Tk.VolumeView {
            objectName: "galleryVolumeView"
            implicitWidth: 300
            implicitHeight: 260
            volume: phantom
            cursor: crosshair
            transfer: transfer
            tooltip: qsTr("Phantom volume rendering")
        }
    }
    Tk.TransferFunctionEditor {
        objectName: "galleryTransferEditor"
        implicitWidth: 924
        model: transfer
        selectedPointId: "core"
    }
}
