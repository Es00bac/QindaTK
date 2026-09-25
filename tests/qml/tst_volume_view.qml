// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk
pragma ComponentBehavior: Bound

// VolumeView: documented parts, the software-renderer fallback, orbit drag
// with Escape cancel, click picking into the shared cursor, keys, wheel and
// settings persistence; TransferFunctionEditor gestures as one undoable step;
// and oblique ImageSliceView navigation along the reslice normal.
TestCase {
    id: root
    name: "VolumeView"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Tk.VoxelVolume { id: phantom }
    Tk.VolumeCursor { id: cursor3d; volume: phantom }
    Tk.TransferFunctionModel { id: transfer }

    Component {
        id: viewComponent
        Tk.VolumeView { width: 400; height: 320; volume: phantom; cursor: cursor3d; transfer: transfer }
    }
    Component { id: bareComponent; Tk.VolumeView {} }
    Component { id: editorComponent; Tk.TransferFunctionEditor { width: 520; model: transfer } }
    Component {
        id: obliqueComponent
        Tk.ImageSliceView {
            width: 300; height: 260
            volume: phantom; cursor: cursor3d
            plane: Tk.ImageSliceItem.Oblique
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function init() {
        verify(phantom.configure({sizeX: 17, sizeY: 17, sizeZ: 17, voxelType: "uint8",
                                  spacing: [1, 1, 1], unit: "mm", valueUnit: "a.u."}))
        verify(phantom.fillSpherePhantom())
        verify(cursor3d.setVoxelPoint(8, 8, 8))
        verify(transfer.configure(0, 255, "a.u."))
        verify(transfer.replacePoints([
            {id: "low", value: 0, color: "#000000", opacity: 0},
            {id: "mid", value: 110, color: "#22d3ee", opacity: 1},
            {id: "high", value: 255, color: "#ffffff", opacity: 1}
        ]))
        // A settings load resets the history, so each test starts clean.
        verify(transfer.restoreSettings(transfer.saveSettings()))
    }

    function test_parts_and_software_fallback() {
        const bare = createTemporaryObject(bareComponent, root)
        verify(bare)
        compare(bare.implicitWidth, Tk.Theme.size.panelMinWidth)
        compare(bare.implicitHeight, Tk.Theme.size.panelMinHeight)
        compare(findChild(bare, "volumeViewReadout").text, "No volume")
        verify(findChild(bare, "volumeViewViewport"))
        verify(findChild(bare, "volumeViewItem"))
        const reset = findChild(bare, "volumeViewReset")
        compare(reset.tooltip, "Reset view")
        verify(!reset.enabled)
        compare(bare.Accessible.role, Accessible.Grouping)
        compare(bare.Accessible.description, "No volume")

        const view = createTemporaryObject(viewComponent, root)
        verify(view)
        waitForRendering(view)
        compare(cursor3d.volume, phantom)
        tryCompare(view, "renderPath", "reference")
        compare(view.fallbackReason, "The scene graph has no GPU (software renderer)")
        compare(findChild(view, "volumeViewRenderPath").text, "CPU")
        tryCompare(findChild(view, "volumeViewReadout"), "text",
                   "Yaw 30° · Pitch 20° · Zoom 1.00 · 17 × 17 × 17 uint8 · CPU reference")
    }

    function test_drag_orbits_and_escape_cancels() {
        const view = createTemporaryObject(viewComponent, root)
        verify(view)
        waitForRendering(view)
        const viewport = findChild(view, "volumeViewViewport")
        const cx = Math.round(viewport.width / 2), cy = Math.round(viewport.height / 2)
        compare(view.yaw, 30)
        compare(view.pitch, 20)
        mousePress(viewport, cx, cy)
        mouseMove(viewport, cx + 40, cy, -1, Qt.LeftButton)
        compare(view.yaw, 50) // 40 px at 0.5° per pixel
        verify(view.orbiting)
        keyClick(Qt.Key_Escape)
        compare(view.yaw, 30)
        verify(!view.orbiting)
        mouseMove(viewport, cx + 60, cy + 10, -1, Qt.LeftButton)
        compare(view.yaw, 30) // a canceled drag ignores the rest of the gesture
        mouseRelease(viewport, cx + 60, cy + 10)
        compare(cursor3d.z, 8) // and does not pick
        mousePress(viewport, cx, cy)
        mouseMove(viewport, cx, cy + 20, -1, Qt.LeftButton)
        mouseRelease(viewport, cx, cy + 20)
        compare(view.pitch, 30)
        compare(cursor3d.z, 8)
    }

    function test_click_picks_into_the_shared_cursor() {
        const view = createTemporaryObject(viewComponent, root)
        verify(view)
        waitForRendering(view)
        view.yaw = 0
        view.pitch = 0
        const item = findChild(view, "volumeViewItem")
        const cx = Math.round(item.width / 2), cy = Math.round(item.height / 2)
        const expected = item.pick(cx, cy)
        compare(expected.length, 3)
        verify(expected[2] > 12 && expected[2] < 14) // the shell seen from +Z
        const spy = createTemporaryObject(spyComponent, root, {target: view, signalName: "pointSelected"})
        mouseClick(item, cx, cy)
        compare(spy.count, 1)
        fuzzyCompare(cursor3d.x, expected[0], 0.5)
        fuzzyCompare(cursor3d.y, expected[1], 0.5)
        fuzzyCompare(cursor3d.z, expected[2], 0.5)
        compare(spy.signalArguments[0][0].length, 3)
        verify(view.activeFocus)
    }

    function test_keys_wheel_and_mode() {
        const view = createTemporaryObject(viewComponent, root)
        verify(view)
        waitForRendering(view)
        view.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(view.yaw, 35)
        keyClick(Qt.Key_Up)
        compare(view.pitch, 25)
        keyClick(Qt.Key_Plus)
        compare(view.zoom, 1.25)
        keyClick(Qt.Key_Minus)
        fuzzyCompare(view.zoom, 1, 1e-9)
        const viewport = findChild(view, "volumeViewViewport")
        mouseWheel(viewport, viewport.width / 2, viewport.height / 2, 0, 120)
        fuzzyCompare(view.zoom, 1.25, 1e-9)
        keyClick(Qt.Key_Home)
        compare(view.yaw, 30)
        compare(view.pitch, 20)
        compare(view.zoom, 1)
        const mode = findChild(view, "volumeViewMode")
        compare(mode.currentIndex, 0)
        mouseClick(findChild(mode, "segment_1"))
        compare(view.mode, Tk.VolumeViewItem.MaximumIntensity)
    }

    function test_settings_round_trip_without_touching_voxels() {
        const view = createTemporaryObject(viewComponent, root)
        verify(view)
        const revision = phantom.revision
        const saved = JSON.parse(JSON.stringify(view.saveSettings()))
        view.yaw = 100
        view.mode = Tk.VolumeViewItem.MaximumIntensity
        view.clipLow = Qt.vector3d(0.25, 0, 0)
        compare(findChild(view, "volumeViewMode").currentIndex, 1)
        verify(view.restoreSettings(saved))
        compare(view.yaw, 30)
        compare(view.mode, Tk.VolumeViewItem.Composite)
        compare(view.clipLow.x, 0)
        const bad = JSON.parse(JSON.stringify(saved))
        bad.pitch = 120
        verify(!view.restoreSettings(bad))
        compare(view.pitch, 20)
        compare(phantom.revision, revision)
    }

    function test_transfer_gesture_cancel_and_undo() {
        const editor = createTemporaryObject(editorComponent, root)
        verify(editor)
        waitForRendering(editor)
        const revision = phantom.revision
        const gradient = findChild(editor, "transferGradient")
        const undoButton = findChild(editor, "transferUndo")
        const redoButton = findChild(editor, "transferRedo")
        verify(!undoButton.enabled)
        gradient.movePreviewRequested("mid", 150 / 255)
        verify(transfer.editing)
        fuzzyCompare(transfer.point("mid").value, 150, 1e-9)
        gradient.moveCanceled("mid")
        verify(!transfer.editing)
        compare(transfer.point("mid").value, 110)
        verify(!undoButton.enabled)

        gradient.movePreviewRequested("mid", 140 / 255)
        gradient.movePreviewRequested("mid", 150 / 255)
        gradient.moveFinished("mid", 160 / 255)
        verify(!transfer.editing)
        fuzzyCompare(transfer.point("mid").value, 160, 1e-9)
        verify(undoButton.enabled)
        compare(undoButton.tooltip, "Undo Move transfer point")
        mouseClick(undoButton)
        compare(transfer.point("mid").value, 110)
        verify(redoButton.enabled)
        mouseClick(redoButton)
        fuzzyCompare(transfer.point("mid").value, 160, 1e-9)
        editor.forceActiveFocus()
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(transfer.point("mid").value, 110)
        verify(!transfer.canUndo)
        compare(phantom.revision, revision)
    }

    function test_oblique_slice_steps_along_its_normal() {
        const view = createTemporaryObject(obliqueComponent, root)
        verify(view)
        waitForRendering(view)
        compare(view.planeName, "Oblique")
        compare(view.sliceCount, 30) // √867 mm diagonal in 1 mm steps
        compare(view.sliceIndex, 15)
        compare(findChild(view, "imageSliceIndex").text, "16 / 30")
        view.forceActiveFocus()
        keyClick(Qt.Key_PageUp)
        compare(cursor3d.z, 9)
        compare(view.sliceIndex, 16)
        keyClick(Qt.Key_Right)
        compare(cursor3d.x, 9)
        // Normal along world −Y: Page Up now moves the cursor to lower Y.
        view.resliceOrientation = [1, 0, 0, 0, 0, -1, 0, 1, 0]
        keyClick(Qt.Key_PageUp)
        compare(cursor3d.y, 7)
    }
}
