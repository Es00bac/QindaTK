// SPDX-License-Identifier: LGPL-3.0-or-later
import QtCore
import QtQuick
import QindaTK as Tk

// A known sphere phantom in three linked, calibrated principal planes, an
// oblique world reslice and a direct volume rendering, all sharing one
// cursor and one transfer function. The voxels are anisotropic
// (0.8 × 0.8 × 1.6 mm), so round cross-sections in every plane show that the
// views fit physical spacing, not voxel counts. The centre voxel (20, 20, 10)
// sits at world (0, 0, 0) mm; the shell (110 a.u.) reaches 10.496 mm and the
// core (220 a.u.) 5.773 mm. "Save view" writes the cursor, every view's
// settings and the transfer function to a settings file, apart from the
// voxels; "Restore view" reapplies them, also after a restart, only to the
// same dataset, and the transfer part is one undoable step (D-260, D-262).
Rectangle {
    id: page
    width: 1280
    height: 900
    color: Tk.Theme.color.bg
    // Where saved views live (a host picks its own; tests pass a temp file).
    property url settingsLocation: StandardPaths.writableLocation(StandardPaths.AppConfigLocation)
                                   + "/volume-imaging-view.ini"
    property string notice: ""
    // The saved view as JSON text ("" when none). Written with setValue +
    // sync, so it is on disk when saveView() returns, not on a later timer.
    property string savedView: ""
    Settings {
        id: stored
        location: page.settingsLocation
        category: "volumeImaging"
    }
    readonly property var sliceViews: ({axial: axialView, coronal: coronalView,
                                        sagittal: sagittalView, oblique: obliqueView})

    function saveView() {
        const slices = {}
        for (const name in sliceViews) slices[name] = sliceViews[name].saveSettings()
        savedView = JSON.stringify({format: 1, cursor: crosshair.worldPoint, slices: slices,
                                    volume: phantomVolume.saveSettings(), transfer: transfer.saveSettings()})
        stored.setValue("savedView", savedView)
        stored.sync()
        notice = qsTr("View saved")
    }
    // Applies a saved view; false (with `notice`) when it cannot. The
    // dataset check runs first, so a view of another volume changes nothing.
    function restoreView() {
        let saved = null
        try { saved = JSON.parse(savedView) } catch (error) { saved = null }
        if (!saved || saved.format !== 1 || !saved.volume || !saved.slices || !saved.transfer) {
            notice = qsTr("The saved view is unreadable")
            return false
        }
        const mismatch = phantom.identityMismatch(saved.volume.volume || ({}))
        if (mismatch.length > 0) { notice = mismatch; return false }
        if (!phantomVolume.restoreSettings(saved.volume)) { notice = phantomVolume.lastError; return false }
        for (const name in sliceViews) {
            if (saved.slices[name] && !sliceViews[name].restoreSettings(saved.slices[name])) {
                notice = sliceViews[name].lastError
                return false
            }
        }
        // applySettings, not restoreSettings: one undo brings the edits back.
        if (!transfer.applySettings(saved.transfer)) { notice = transfer.lastError; return false }
        if (saved.cursor && saved.cursor.length === 3)
            crosshair.setWorldPoint(saved.cursor[0], saved.cursor[1], saved.cursor[2])
        notice = qsTr("View restored")
        return true
    }
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
            gap: Tk.Theme.space.sm
            Tk.Heading { text: qsTr("Volume imaging · known sphere phantom"); Tk.Flex.grow: 1 }
            Tk.Caption {
                objectName: "phantomShape"
                text: qsTr("%1 × %2 × %3 · %4 · %5 %6 voxels")
                    .arg(phantom.sizeX).arg(phantom.sizeY).arg(phantom.sizeZ).arg(phantom.voxelType)
                    .arg(phantom.spacing.join(" × ")).arg(phantom.unit)
            }
            Tk.Button {
                objectName: "phantomSaveView"
                text: qsTr("Save view")
                iconName: "save"
                small: true
                tooltip: qsTr("Save the cursor, view and transfer settings to the settings file")
                onClicked: page.saveView()
            }
            Tk.Button {
                objectName: "phantomRestoreView"
                text: qsTr("Restore view")
                iconName: "rotate-ccw"
                small: true
                enabled: page.savedView.length > 0
                tooltip: qsTr("Reapply the saved view; undo brings back the transfer edits")
                onClicked: page.restoreView()
            }
            Tk.Caption { objectName: "phantomViewNotice"; text: page.notice; visible: text.length > 0 }
        }
        Tk.Caption {
            text: qsTr("Click a slice or the volume to move the shared cursor. Scroll or press Page Up/Down to change slice; drag the volume to orbit and scroll it to zoom. The scientific 3D family (surface, scatter and this volume) is joined in the scientific-3d-family example.")
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.md
            Tk.Flex.grow: 1
            Tk.ImageSliceView {
                id: axialView
                objectName: "phantomAxial"
                Tk.Flex.flex: 1
                volume: phantom; cursor: crosshair; transfer: transfer
                plane: Tk.ImageSliceItem.Axial
                tooltip: qsTr("Axial sphere slice")
            }
            Tk.ImageSliceView {
                id: coronalView
                objectName: "phantomCoronal"
                Tk.Flex.flex: 1
                volume: phantom; cursor: crosshair; transfer: transfer
                plane: Tk.ImageSliceItem.Coronal
                tooltip: qsTr("Coronal sphere slice")
            }
            Tk.ImageSliceView {
                id: sagittalView
                objectName: "phantomSagittal"
                Tk.Flex.flex: 1
                volume: phantom; cursor: crosshair; transfer: transfer
                plane: Tk.ImageSliceItem.Sagittal
                tooltip: qsTr("Sagittal sphere slice")
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.md
            Tk.Flex.grow: 1
            Tk.ImageSliceView {
                id: obliqueView
                objectName: "phantomOblique"
                Tk.Flex.flex: 1
                volume: phantom; cursor: crosshair; transfer: transfer
                plane: Tk.ImageSliceItem.Oblique
                // 30° about world X: rows tilt from +Y towards +Z.
                resliceOrientation: [1, 0, 0,
                                     0, 0.8660254037844387, -0.5,
                                     0, 0.5, 0.8660254037844387]
                tooltip: qsTr("Oblique reslice through the cursor")
            }
            Tk.VolumeView {
                id: phantomVolume
                objectName: "phantomVolume"
                Tk.Flex.flex: 2
                title: qsTr("Volume · composite")
                volume: phantom; cursor: crosshair; transfer: transfer
                tooltip: qsTr("Sphere phantom volume rendering")
            }
        }
        Tk.TransferFunctionEditor {
            objectName: "phantomTransferEditor"
            model: transfer
            // The value distribution over the transfer domain, with explicit
            // below/above/missing counts (the phantom has none of those).
            histogram: phantom.revision >= 0 && transfer.revision >= 0
                ? phantom.histogram(64, transfer.domainMin, transfer.domainMax) : null
            selectedPointId: "shell"
        }
        Tk.Caption {
            objectName: "phantomReadout"
            text: {
                const world = crosshair.worldPoint
                const sample = phantom.revision >= 0
                    ? phantom.sample(crosshair.x, crosshair.y, crosshair.z) : ({valid: false})
                return qsTr("World point: %1 %2 · Cursor value %3 %4 · Volume path: %5")
                    .arg(world.map(function(v) { return Number(v).toFixed(2) }).join(", "))
                    .arg(phantom.unit)
                    .arg(sample.valid ? sample.value : qsTr("missing"))
                    .arg(phantom.valueUnit)
                    .arg(phantomVolume.renderPath === "gpu" ? qsTr("GPU")
                         : qsTr("CPU reference (%1)").arg(phantomVolume.fallbackReason))
            }
        }
    }

    Component.onCompleted: {
        savedView = String(stored.value("savedView", ""))
        phantom.configure({sizeX: 41, sizeY: 41, sizeZ: 21, voxelType: "uint8",
                           spacing: [0.8, 0.8, 1.6], origin: [-16, -16, -16],
                           unit: "mm", valueUnit: "a.u.",
                           datasetId: "demo:sphere-phantom", frameOfReference: "phantom"})
        phantom.fillSpherePhantom()
        transfer.configure(0, 255, "a.u.")
        transfer.replacePoints([
            {id: "clear", value: 0, color: Qt.rgba(0.05, 0.12, 0.18, 1), opacity: 0},
            {id: "shell", value: 110, color: Qt.rgba(0.20, 0.75, 0.95, 1), opacity: 0.35},
            {id: "core", value: 220, color: Qt.rgba(1.0, 0.55, 0.28, 1), opacity: 1},
            {id: "cap", value: 255, color: Qt.rgba(1, 1, 1, 1), opacity: 1}
        ])
    }
}
