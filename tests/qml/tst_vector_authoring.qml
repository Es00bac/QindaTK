// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "VectorAuthoring"
    when: windowShown
    visible: true
    width: 440
    height: 320

    Component {
        id: paletteComponent
        Tk.ShapePalette {
            width: 360
            definitions: [
                { id: "ellipse", label: "Ellipse", category: "Basic", iconName: "circle", parameters: { startAngle: 0, sweepAngle: 360 } }
            ]
        }
    }
    Component {
        id: tilePaletteComponent
        Tk.ShapePalette {
            width: 360
            columns: 2
            categoryPicker: "menu"
            searchAllCategories: true
            dragEnabled: true
            definitions: [
                { id: "process", label: "Process", category: "Flowchart", imageSource: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=",
                  description: "An activity step", parameters: { width: 120 } },
                { id: "server", label: "Server", category: "Network", imageSource: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=" },
                { id: "note", label: "Note", category: "Basic", iconName: "sticky-note" }
            ]
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_tiles_menu_categories_and_search_everywhere() {
        const palette = createTemporaryObject(tilePaletteComponent, this)
        verify(palette !== null)
        const tile = findChild(palette, "shape_process")
        verify(tile !== null)
        verify(findChild(tile, "shapeThumbnail") !== null, "an imageSource definition draws a tile")
        verify(findChild(findChild(palette, "shape_note"), "shapeThumbnail") === null, "an icon definition keeps the button")
        verify(findChild(palette, "shapeCategoryMenu").visible)
        verify(!findChild(palette, "shapeCategories").visible)
        palette.category = "Network"
        compare(palette.visibleDefinitions.length, 1)
        palette.searchText = "activity"
        compare(palette.visibleDefinitions.length, 1, "search reaches every category")
        compare(palette.visibleDefinitions[0].id, "process")
        palette.searchText = ""
        compare(palette.visibleDefinitions.length, 1)
    }

    function test_dragging_a_tile_reports_scene_points_and_finishes_once() {
        const palette = createTemporaryObject(tilePaletteComponent, this)
        const started = createTemporaryObject(spyComponent, this, { target: palette, signalName: "shapeDragStarted" })
        const moved = createTemporaryObject(spyComponent, this, { target: palette, signalName: "shapeDragMoved" })
        const finished = createTemporaryObject(spyComponent, this, { target: palette, signalName: "shapeDragFinished" })
        const requested = createTemporaryObject(spyComponent, this, { target: palette, signalName: "shapeRequested" })
        const tile = findChild(palette, "shape_process")
        waitForRendering(palette)
        mousePress(tile, 10, 10)
        for (let i = 1; i <= 8; ++i) mouseMove(tile, 10 + i * 12, 10 + i * 6)
        compare(started.count, 1)
        compare(started.signalArguments[0][0], "process")
        compare(palette.draggingShapeId, "process")
        verify(moved.count > 0)
        mouseRelease(tile, 106, 58)
        compare(finished.count, 1)
        compare(finished.signalArguments[0][0], "process")
        compare(finished.signalArguments[0][1].width, 120)
        const expected = tile.mapToItem(null, 106, 58)
        fuzzyCompare(finished.signalArguments[0][2].x, expected.x, 1)
        fuzzyCompare(finished.signalArguments[0][2].y, expected.y, 1)
        compare(palette.draggingShapeId, "")
        compare(requested.count, 0, "a drag is not a click")
        compare(palette.recentIds[0], "process")
    }

    function test_shape_button_requests_one_stable_id_and_parameter_copy() {
        const palette = createTemporaryObject(paletteComponent, this)
        verify(palette !== null)
        const requested = createTemporaryObject(spyComponent, this,
            { target: palette, signalName: "shapeRequested" })
        const button = findChild(palette, "shape_ellipse")
        verify(button !== null)

        mouseClick(button)

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "ellipse")
        compare(requested.signalArguments[0][1].sweepAngle, 360)
        compare(palette.recentIds[0], "ellipse")
    }
}
