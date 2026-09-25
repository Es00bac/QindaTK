// SPDX-License-Identifier: LGPL-3.0-or-later
import QtCore
import QtQuick
import QtTest
import QindaTK as Tk
pragma ComponentBehavior: Bound

// WP-086 audit gaps in QML: the volume-imaging example's saved view survives
// a restart, belongs to its dataset and restores transfer settings as one
// undoable step (D-260, D-262); the scientific 3D family example shows the
// surface/scatter and volume pages with their known data; the transfer
// editor closes the gestures it opened when it goes away or changes model,
// and its preset, range and histogram parts drive the model (D-263).
TestCase {
    id: root
    name: "VolumeExample"
    when: windowShown
    visible: true
    width: 1280
    height: 960

    // A fresh settings file per test and run: nothing leaks between tests or
    // into the user's own configuration.
    readonly property string runId: String(Date.now())
    function settingsFile(name) {
        return StandardPaths.writableLocation(StandardPaths.TempLocation)
            + "/qindatk-volume-view-" + runId + "-" + name + ".ini"
    }

    Loader { id: loader; anchors.fill: parent; active: false }

    Tk.TransferFunctionModel { id: transfer }
    Tk.TransferFunctionModel { id: other }
    Component { id: editorComponent; Tk.TransferFunctionEditor { width: 560; model: transfer } }

    function openPage(file, properties) {
        loader.active = false
        loader.setSource(Qt.resolvedUrl(file), properties || {})
        loader.active = true
        tryCompare(loader, "status", Loader.Ready)
        waitForRendering(loader.item)
        return loader.item
    }
    function part(page, name) {
        const item = findChild(page, name)
        verify(item !== null, name)
        return item
    }

    function init() {
        verify(transfer.configure(0, 255, "a.u."))
        verify(transfer.replacePoints([
            {id: "low", value: 0, color: "#000000", opacity: 0},
            {id: "mid", value: 110, color: "#22d3ee", opacity: 0.5},
            {id: "high", value: 255, color: "#ffffff", opacity: 1}]))
        verify(transfer.restoreSettings(transfer.saveSettings()))
        verify(other.configure(0, 1))
    }

    function test_saved_view_survives_a_restart() {
        const settings = {settingsLocation: settingsFile("restart")}
        let page = openPage("../../examples/volume-imaging/Main.qml", settings)
        let volumeView = part(page, "phantomVolume")
        let axial = part(page, "phantomAxial")
        let model = volumeView.transfer
        verify(!part(page, "phantomRestoreView").enabled) // a fresh settings file
        volumeView.yaw = 75
        volumeView.mode = Tk.VolumeViewItem.MaximumIntensity
        axial.windowWidth = 80
        verify(volumeView.cursor.setVoxelPoint(14, 26, 6))
        // A copy: the property's list is tied to the cursor, which the restart deletes.
        const savedWorld = volumeView.cursor.worldPoint.map(Number)
        verify(model.applyPreset("hot"))
        mouseClick(part(page, "phantomSaveView"))
        compare(part(page, "phantomViewNotice").text, "View saved")

        // Edits after saving; Restore view brings the saved view back and
        // one undo brings the transfer edits back (not a history-wiping load).
        volumeView.yaw = -40
        axial.windowWidth = 200
        verify(model.applyPreset("viridis"))
        mouseClick(part(page, "phantomRestoreView"))
        compare(part(page, "phantomViewNotice").text, "View restored")
        compare(volumeView.yaw, 75)
        compare(axial.windowWidth, 80)
        compare(model.presetId, "hot")
        verify(model.canUndo)
        verify(model.undo())
        compare(model.presetId, "viridis")

        // Restart: a new page instance reads the same settings file.
        page = openPage("../../examples/volume-imaging/Main.qml", settings)
        volumeView = part(page, "phantomVolume")
        axial = part(page, "phantomAxial")
        model = volumeView.transfer
        compare(volumeView.yaw, 30)
        compare(model.presetId, "")
        const restore = part(page, "phantomRestoreView")
        verify(restore.enabled)
        mouseClick(restore)
        compare(part(page, "phantomViewNotice").text, "View restored")
        compare(volumeView.yaw, 75)
        compare(volumeView.mode, Tk.VolumeViewItem.MaximumIntensity)
        compare(axial.windowWidth, 80)
        compare(model.presetId, "hot")
        for (let axis = 0; axis < 3; ++axis)
            fuzzyCompare(volumeView.cursor.worldPoint[axis], savedWorld[axis], 1e-9)
        compare(volumeView.cursor.x, 14)
        verify(model.undo()) // back to the example's own transfer function
        compare(model.presetId, "")
        compare(model.point("shell").value, 110)
    }

    function test_saved_view_rejects_another_dataset() {
        const page = openPage("../../examples/volume-imaging/Main.qml",
                              {settingsLocation: settingsFile("dataset")})
        const volumeView = part(page, "phantomVolume")
        mouseClick(part(page, "phantomSaveView"))
        // The host loads a different dataset into the same views.
        verify(volumeView.volume.configure({sizeX: 41, sizeY: 41, sizeZ: 21, spacing: [0.8, 0.8, 1.6],
                                            origin: [-16, -16, -16], unit: "mm", valueUnit: "a.u.",
                                            datasetId: "demo:other-phantom", frameOfReference: "phantom"}))
        volumeView.yaw = -10
        const undoSteps = volumeView.transfer.canUndo
        mouseClick(part(page, "phantomRestoreView"))
        verify(part(page, "phantomViewNotice").text.indexOf("demo:sphere-phantom") >= 0)
        compare(volumeView.yaw, -10)
        compare(volumeView.transfer.canUndo, undoSteps) // nothing applied
    }

    function test_family_example_shows_both_pages() {
        const page = openPage("../../examples/scientific-3d-family/Main.qml")
        const plot = part(page, "scientific3DExample")
        verify(plot.visible)
        compare(plot.grid.rows, 21)
        compare(plot.grid.columns, 21)
        compare(plot.grid.missingCount, 4)
        compare(plot.points.count, 3)
        const volumePage = part(page, "familyVolumePage")
        verify(!volumePage.visible)
        part(page, "familySwitcher").activate(1)
        verify(volumePage.visible)
        verify(!plot.visible)
        const volumeView = part(page, "phantomVolume")
        tryVerify(function() { return volumeView.width > 200 && volumeView.renderStatistics().completed >= 1 })
        compare(volumeView.volume.datasetId, "demo:sphere-phantom")
        compare(part(page, "phantomAxial").sliceCount, 21)
        compare(volumeView.linkError, "")
    }

    function test_editor_closes_its_gesture_when_destroyed() {
        let editor = createTemporaryObject(editorComponent, root)
        waitForRendering(editor)
        part(editor, "transferGradient").movePreviewRequested("mid", 150 / 255)
        verify(transfer.editing)
        editor.destroy()
        wait(0)
        verify(!transfer.editing)
        compare(transfer.point("mid").value, 110)
        verify(transfer.beginEdit("next gesture"))
        verify(transfer.cancelEdit())

        // A model swap cancels the gesture on the old model only.
        editor = createTemporaryObject(editorComponent, root)
        waitForRendering(editor)
        part(editor, "transferGradient").movePreviewRequested("mid", 150 / 255)
        verify(transfer.editing)
        editor.model = other
        verify(!transfer.editing)
        verify(!other.editing)
        compare(transfer.point("mid").value, 110)
        verify(!transfer.canUndo)

        // A gesture another client opened is left alone.
        editor.model = transfer
        verify(transfer.beginEdit("host gesture"))
        editor.model = other
        verify(transfer.editing)
        verify(transfer.cancelEdit())
    }

    function test_editor_presets_ranges_and_histogram() {
        const volume = Qt.createQmlObject("import QindaTK as Tk; Tk.VoxelVolume {}", root)
        verify(volume.configure({sizeX: 17, sizeY: 17, sizeZ: 17}))
        verify(volume.fillSpherePhantom())
        const editor = createTemporaryObject(editorComponent, root)
        editor.histogram = volume.histogram(64, 0, 200)
        waitForRendering(editor)
        compare(findChild(editor, "transferHistogram").visible, true)
        // The phantom's core (220) is above the 0–200 domain and counted apart.
        verify(Number(part(editor, "transferRangeCount_above").text) > 0)
        compare(part(editor, "transferRangeCount_missing").text, "0")

        const presets = part(editor, "transferPreset")
        compare(presets.currentText, "Custom")
        presets.activated(3) // Custom, Grayscale, Hot, Viridis…
        compare(transfer.presetId, "viridis")
        compare(presets.currentText, "Viridis")
        compare(transfer.colorAt(0).toString(), "#00440154") // viridis start, opacity 0

        const above = part(editor, "transferRange_above")
        above.colorSelected("#ff0000")
        compare(transfer.aboveRange.mode, "color")
        compare(transfer.classify(300), "above")
        compare(transfer.colorAt(300).toString(), "#ff0000")
        const clamp = part(editor, "transferRangeReset_above")
        verify(clamp.enabled)
        mouseClick(clamp)
        compare(transfer.aboveRange.mode, "clamp")
        compare(transfer.colorAt(300).toString(), transfer.colorAt(255).toString())
        verify(transfer.undo()) // clamp
        verify(transfer.undo()) // above colour
        verify(transfer.undo()) // preset
        compare(transfer.presetId, "")
        volume.destroy()
    }
}
