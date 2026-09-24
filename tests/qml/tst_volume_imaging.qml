// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk
pragma ComponentBehavior: Bound

// ImageSliceView and TransferFunctionEditor: documented sizes and parts,
// pointer/keyboard/wheel navigation of one shared cursor across two planes,
// and transfer edits that never touch the voxel source.
TestCase {
    id: root
    name: "VolumeImaging"
    when: windowShown
    visible: true
    width: 760
    height: 520

    Tk.VoxelVolume { id: phantom }
    Tk.VolumeCursor { id: sharedCursor; volume: phantom }
    Tk.TransferFunctionModel { id: transfer }

    Component {
        id: viewsComponent
        Item {
            width: 740
            height: 260
            property alias axial: axialView
            property alias coronal: coronalView
            Tk.ImageSliceView {
                id: axialView
                width: 360
                height: 260
                volume: phantom
                cursor: sharedCursor
                plane: Tk.ImageSliceItem.Axial
            }
            Tk.ImageSliceView {
                id: coronalView
                x: 380
                width: 360
                height: 260
                volume: phantom
                cursor: sharedCursor
                plane: Tk.ImageSliceItem.Coronal
            }
        }
    }
    Component { id: bareViewComponent; Tk.ImageSliceView {} }
    Component { id: cursorComponent; Tk.VolumeCursor {} }
    Component { id: editorComponent; Tk.TransferFunctionEditor { width: 520; model: transfer } }
    Component { id: spyComponent; SignalSpy {} }

    function init() {
        verify(phantom.configure({sizeX: 17, sizeY: 17, sizeZ: 17, voxelType: "uint8",
                                  spacing: [1, 1, 1], unit: "mm", valueUnit: "a.u."}))
        verify(phantom.fillSpherePhantom())
        verify(sharedCursor.setVoxelPoint(8, 8, 8))
        verify(transfer.configure(0, 255, "a.u."))
        verify(transfer.replacePoints([
            {id: "low", value: 0, color: "#000000", opacity: 0},
            {id: "mid", value: 110, color: "#22d3ee", opacity: 0.8},
            {id: "high", value: 255, color: "#ffffff", opacity: 1}
        ]))
    }

    function test_documented_sizes_and_parts() {
        const view = createTemporaryObject(bareViewComponent, root)
        verify(view)
        compare(view.implicitWidth, Tk.Theme.size.panelMinWidth)
        compare(view.implicitHeight, Tk.Theme.size.panelMinHeight)
        compare(view.sliceCount, 0)
        compare(findChild(view, "imageSliceIndex").text, "—")
        compare(findChild(view, "imageSliceCoordinate").text, "No coordinate")
        verify(findChild(view, "imageSliceViewport"))
        verify(findChild(view, "imageSliceImage"))
        const previous = findChild(view, "imageSlicePrevious")
        compare(previous.height, Tk.Theme.size.control)
        compare(previous.tooltip, "Previous slice")
        verify(!previous.enabled)
        compare(view.Accessible.role, Accessible.Grouping)

        const editor = createTemporaryObject(editorComponent, root)
        verify(editor)
        waitForRendering(editor)
        compare(editor.implicitHeight, editor.implicitContentHeight + editor.topPadding + editor.bottomPadding)
        compare(editor.height, editor.implicitHeight)
        const gradient = findChild(editor, "transferGradient")
        const field = findChild(editor, "transferPointValue")
        compare(gradient.height, gradient.implicitHeight)
        compare(field.height, Tk.Theme.size.control)
        const gradientBottom = gradient.mapToItem(editor, 0, gradient.height).y
        verify(field.mapToItem(editor, 0, 0).y >= gradientBottom)
        verify(!field.enabled)
        verify(!findChild(editor, "transferPointOpacity").enabled)
        verify(!findChild(editor, "transferPointColor").enabled)
        compare(editor.Accessible.description, "3 transfer points, 0 to 255 a.u.")
    }

    function test_view_adopts_volume_for_an_unbound_cursor() {
        const cursor = createTemporaryObject(cursorComponent, root)
        const view = createTemporaryObject(bareViewComponent, root,
                                           {width: 300, height: 220, volume: phantom, cursor: cursor})
        verify(view)
        compare(cursor.volume, phantom)
        compare(cursor.x, 8)
        compare(view.sliceIndex, 8)
        compare(findChild(view, "imageSliceIndex").text, "9 / 17")
        compare(findChild(view, "imageSliceCoordinate").text, "Voxel 8.0, 8.0, 8.0  ·  220 a.u.")
    }

    function test_click_moves_the_shared_cursor_and_linked_plane() {
        const views = createTemporaryObject(viewsComponent, root)
        verify(views)
        waitForRendering(views)
        const axial = views.axial
        const coronal = views.coronal
        compare(coronal.sliceIndex, 8)
        const image = findChild(axial, "imageSliceImage")
        // Voxel (5, 11, 8) lies in the phantom shell (4.24 voxels from the centre).
        const target = image.positionForVoxel(5, 11, sharedCursor.z)
        compare(target.length, 2)
        const spy = createTemporaryObject(spyComponent, root, {target: axial, signalName: "pointSelected"})
        mouseClick(image, target[0], target[1])
        fuzzyCompare(sharedCursor.x, 5, 0.1)
        fuzzyCompare(sharedCursor.y, 11, 0.1)
        compare(sharedCursor.z, 8)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0].length, 3)
        verify(axial.activeFocus)
        compare(coronal.sliceIndex, 11)
        compare(findChild(coronal, "imageSliceIndex").text, "12 / 17")
        verify(axial.selectedSample.valid)
        compare(axial.selectedSample.value, 110)
    }

    function test_paint_interpolates_values_before_mapping() {
        // A stop at 165 turns the core/shell midpoint red; blending the core
        // (green) and shell (blue) colors instead could not produce red.
        verify(transfer.replacePoints([
            {id: "low", value: 0, color: "#000000", opacity: 0},
            {id: "shell", value: 110, color: "#0000ff", opacity: 1},
            {id: "mid", value: 165, color: "#ff0000", opacity: 1},
            {id: "core", value: 220, color: "#00ff00", opacity: 1},
            {id: "high", value: 255, color: "#ffffff", opacity: 1}
        ]))
        verify(sharedCursor.setVoxelPoint(8, 3, 8))  // keep the crosshair off row 8
        const views = createTemporaryObject(viewsComponent, root)
        verify(views)
        const axial = views.axial
        axial.transfer = transfer
        const image = findChild(axial, "imageSliceImage")
        waitForRendering(image)
        const core = image.positionForVoxel(10, 8, 8)       // core voxel, 220
        const between = image.positionForVoxel(10.5, 8, 8)  // shell (11, 8, 8) is 110
        compare(String(grabImage(image).pixel(core[0], core[1])), "#00ff00")
        axial.interpolation = Tk.VoxelVolume.Linear
        waitForRendering(image)
        const mixed = grabImage(image).pixel(Math.floor(between[0]), Math.floor(between[1]))
        verify(mixed.r > 0.75 && mixed.g < 0.25 && mixed.b < 0.25, String(mixed))
    }

    function test_keys_wheel_and_buttons_step_through_slices() {
        const views = createTemporaryObject(viewsComponent, root)
        verify(views)
        waitForRendering(views)
        const axial = views.axial
        axial.forceActiveFocus()
        verify(axial.activeFocus)
        keyClick(Qt.Key_PageUp)
        compare(sharedCursor.z, 9)
        keyClick(Qt.Key_PageDown)
        keyClick(Qt.Key_PageDown)
        compare(sharedCursor.z, 7)
        keyClick(Qt.Key_End)
        compare(axial.sliceIndex, 16)
        compare(findChild(axial, "imageSliceIndex").text, "17 / 17")
        verify(!findChild(axial, "imageSliceNext").enabled)
        keyClick(Qt.Key_Home)
        compare(sharedCursor.z, 0)
        keyClick(Qt.Key_Right)
        compare(sharedCursor.x, 9)
        keyClick(Qt.Key_Up)
        compare(sharedCursor.y, 7)
        compare(views.coronal.sliceIndex, 7)

        const image = findChild(axial, "imageSliceImage")
        const cx = image.width / 2
        const cy = image.height / 2
        mouseWheel(image, cx, cy, 0, 120)
        compare(sharedCursor.z, 1)
        mouseWheel(image, cx, cy, 0, 60)
        compare(sharedCursor.z, 1)
        mouseWheel(image, cx, cy, 0, 60)
        compare(sharedCursor.z, 2)
        mouseWheel(image, cx, cy, 120, 0)
        compare(sharedCursor.z, 2)
        mouseWheel(image, cx, cy, 0, -240)
        compare(sharedCursor.z, 0)
        mouseClick(findChild(axial, "imageSliceNext"))
        compare(sharedCursor.z, 1)
        mouseClick(findChild(axial, "imageSlicePrevious"))
        compare(sharedCursor.z, 0)
    }

    function test_transfer_edits_leave_voxels_untouched() {
        const editor = createTemporaryObject(editorComponent, root)
        verify(editor)
        waitForRendering(editor)
        const revision = phantom.revision
        const gradient = findChild(editor, "transferGradient")
        const usable = gradient.width - 2 * gradient.trackInset
        mouseClick(gradient, gradient.trackInset + 110 / 255 * usable, gradient.handleY)
        compare(editor.selectedPointId, "mid")
        const field = findChild(editor, "transferPointValue")
        verify(field.enabled)
        compare(field.value, 110)

        const input = findChild(field, "numberInput")
        input.forceActiveFocus()
        keyClick(Qt.Key_Up)
        compare(transfer.point("mid").value, 111)
        field.commit(300)  // clamps to 255, which the model rejects
        compare(transfer.point("mid").value, 111)
        compare(field.value, 111)

        const opacity = findChild(editor, "transferPointOpacity")
        opacity.forceActiveFocus()
        keyClick(Qt.Key_Right)
        fuzzyCompare(transfer.point("mid").opacity, 0.9, 0.001)

        const colorButton = findChild(editor, "transferPointColor")
        mouseClick(colorButton)
        const popover = findChild(colorButton, "colorButtonPopover")
        verify(popover)
        tryCompare(popover, "opened", true)
        const swatch = findChild(popover.contentItem, "swatch_1")
        verify(swatch)
        compare(swatch.entry.name, "Accent")
        mouseClick(swatch)
        compare(String(transfer.point("mid").color), String(Tk.Theme.color.accent))
        tryCompare(popover, "visible", false)
        const customSpy = createTemporaryObject(spyComponent, root,
                                                {target: editor, signalName: "customColorRequested"})
        colorButton.customRequested()
        compare(customSpy.count, 1)
        compare(customSpy.signalArguments[0][0], "mid")

        const before = function(v) { return 0.9 + (v - 111) / (255 - 111) * 0.1 }
        mouseClick(gradient, gradient.trackInset + 200 / 255 * usable, gradient.handleY)
        compare(transfer.points.length, 4)
        const added = transfer.point(editor.selectedPointId)
        verify(added.value > 111 && added.value < 255)
        fuzzyCompare(added.opacity, before(added.value), 0.000001)
        gradient.forceActiveFocus()
        keyClick(Qt.Key_Delete)
        compare(transfer.points.length, 3)
        compare(editor.selectedPointId, "")
        compare(phantom.revision, revision)
    }
}
