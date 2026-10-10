// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// D-312: EditableGrid group header rows — a band with chevron, name and
// summary and per-column values over the rows the host marks; the chevron,
// Return and Space report groupToggled; a group row is never edited.
TestCase {
    id: root
    name: "GridGroupRows"
    when: windowShown
    visible: true
    width: 520
    height: 300

    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: gridComponent
        Item {
            width: 480
            height: 260
            Tk.EditableGridModel {
                id: data
                Component.onCompleted: {
                    setRowIds(["g:north", "r:1", "r:2", "g:south", "r:3"])
                    setColumnIds(["name", "duration", "cost"])
                }
            }
            Tk.EditableGrid {
                objectName: "editableGrid"
                anchors.fill: parent
                model: data
                tooltip: "Tasks by region"
                groupHeader: function(rowId, index) {
                    if (rowId === "g:north")
                        return { label: "North", summary: "2 tasks", collapsed: false, level: 0, values: { cost: "$300" } }
                    if (rowId === "g:south") return { label: "South", summary: "1 task", collapsed: true, level: 1 }
                    return null
                }
            }
        }
    }

    function test_group_bands_summaries_and_toggles() {
        const host = createTemporaryObject(gridComponent, root)
        verify(host !== null)
        const grid = findChild(host, "editableGrid")
        waitForRendering(grid)
        const north = findChild(grid, "gridGroupHeader_g:north")
        const south = findChild(grid, "gridGroupHeader_g:south")
        verify(north !== null && south !== null)
        verify(north.visible && south.visible)
        compare(findChild(grid, "gridGroupHeader_r:1"), null)
        const viewport = findChild(grid, "gridCellViewport")
        compare(north.width, viewport.width)
        compare(north.y, grid.model.cellRect(0, 0, 0, 0).y)
        compare(north.height, grid.model.cellRect(0, 0, 0, 0).height)
        compare(south.y, grid.model.cellRect(3, 0, 0, 0).y)
        compare(findChild(north, "gridGroupChevron").name, "chevron-down")
        compare(findChild(south, "gridGroupChevron").name, "chevron-right")
        verify(findChild(south, "gridGroupChevron").x > findChild(north, "gridGroupChevron").x)
        compare(findChild(north, "gridGroupLabel").text, "North")
        compare(findChild(north, "gridGroupSummary").text, "2 tasks")
        const cost = findChild(north, "gridGroupValue_cost")
        verify(cost.visible)
        compare(cost.text, "$300")
        compare(cost.x, grid.model.cellRect(0, 2, 0, 0).x + Tk.Theme.space.sm)
        compare(north.Accessible.name, "North, 2 tasks")

        const toggled = createTemporaryObject(spyComponent, root, { target: grid, signalName: "groupToggled" })
        mouseClick(findChild(north, "gridGroupChevron"))
        compare(toggled.count, 1)
        compare(toggled.signalArguments[0][0], "g:north")
        compare(toggled.signalArguments[0][1], true)
        // A tap on the band selects the group row's cell under the pointer.
        mouseClick(north, cost.x + 4, north.height / 2)
        compare(grid.activeRowId, "g:north")
        compare(grid.activeColumnId, "cost")
        verify(grid.activeFocus)
        keyClick(Qt.Key_Return)
        compare(toggled.count, 2)
        keyClick(Qt.Key_Space)
        compare(toggled.count, 3)
        compare(grid.cellEditing, false)
        verify(!grid.beginCellEdit())
        // South is collapsed: a toggle asks to expand it.
        grid.setCurrent(3, 0, false)
        keyClick(Qt.Key_Return)
        compare(toggled.count, 4)
        compare(toggled.signalArguments[3][0], "g:south")
        compare(toggled.signalArguments[3][1], false)
        verify(!grid.setGroupCollapsed("g:south", true))
        compare(toggled.count, 4)
        verify(!grid.toggleGroup("r:1"))
        // An ordinary row still edits.
        grid.setCurrent(1, 0, false)
        keyClick(Qt.Key_Return)
        compare(grid.cellEditing, true)
        keyClick(Qt.Key_Escape)
        compare(grid.cellEditing, false)
        compare(toggled.count, 4)
    }
}
