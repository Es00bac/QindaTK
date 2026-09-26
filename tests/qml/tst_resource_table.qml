// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "ResourceTable"
    when: windowShown
    visible: true
    width: 520
    height: 280

    Component {
        id: tableComponent
        Item {
            width: 480
            height: 240
            Tk.ResourcePlanModel {
                id: plan
                Component.onCompleted: {
                    setResources([
                        { id: "artist", name: "Artist", unit: "hours", capacity: 8 },
                        { id: "node", name: "Render node", unit: "GPU hours", capacity: 4 },
                        { id: "editor", name: "Editor", unit: "hours", capacity: 6 }
                    ])
                    setBuckets([
                        { id: "mon", name: "Monday" }, { id: "tue", name: "Tuesday" },
                        { id: "wed", name: "Wednesday" }, { id: "thu", name: "Thursday" },
                        { id: "fri", name: "Friday" }, { id: "sat", name: "Saturday" },
                        { id: "sun", name: "Sunday" }
                    ])
                    requestAllocation("artist", "mon", 5)
                    requestAllocation("artist", "tue", 9)
                    requestAllocation("node", "mon", 2)
                }
            }
            Tk.ResourceTable {
                id: view
                anchors.fill: parent
                model: plan
                tooltip: "Weekly resource allocation"
            }
        }
    }

    Component {
        id: largeTableComponent
        Item {
            width: 480
            height: 240
            Tk.ResourcePlanModel {
                id: plan
                Component.onCompleted: {
                    const resources = []
                    const buckets = []
                    for (let i = 0; i < 200; ++i)
                        resources.push({ id: "r-" + i, name: "Resource " + i,
                                         unit: "hours", capacity: 8 })
                    for (let i = 0; i < 40; ++i)
                        buckets.push({ id: "b-" + i, name: "Bucket " + i })
                    setResources(resources)
                    setBuckets(buckets)
                }
            }
            Tk.ResourceTable {
                id: view
                anchors.fill: parent
                model: plan
                tooltip: "Large resource allocation matrix"
            }
        }
    }

    function test_virtual_matrix_and_accessible_table() {
        const host = createTemporaryObject(tableComponent, root)
        verify(host !== null)
        const view = findChild(host, "resourceTable")
        verify(view !== null)
        const table = view
        compare(table.model.resourceCount, 3)
        compare(table.model.bucketCount, 7)
        verify(table.visibleCellRecords.length < 30)
        verify(findChild(table, "resourceCellViewport") !== null)
        verify(findChild(table, "resourceCell_artist_mon") !== null)
        compare(table.Accessible.role, Accessible.Table)
        compare(table.Accessible.name, "Weekly resource allocation")
    }

    function test_blank_zeros_and_summary_modes() {
        const host = createTemporaryObject(tableComponent, root)
        const table = findChild(host, "resourceTable")
        const value = function(cell) { return findChild(findChild(table, cell), "resourceCellValue").text }
        const summary = function() { return findChild(findChild(table, "resourceRowHeader_artist"), "resourceRowSummary").text }
        compare(value("resourceCell_artist_wed"), "0")
        compare(summary(), "14/56 hours")
        table.showZeros = false
        compare(value("resourceCell_artist_wed"), "")
        compare(value("resourceCell_artist_mon"), "5")
        table.summaryMode = "usage"
        compare(summary(), "14 hours")
        table.summaryMode = "none"
        compare(summary(), "")
    }

    function test_keyboard_edit_conflict_totals_and_undo() {
        const host = createTemporaryObject(tableComponent, root)
        const table = findChild(host, "resourceTable")
        table.setCurrent(0, 1)
        table.forceActiveFocus()
        keyClick(Qt.Key_Return)
        verify(table.editing)
        const editor = findChild(table, "resourceAllocationEditor")
        verify(editor !== null)
        editor.text = "3"
        verify(table.commitCellEdit(editor.text))
        compare(table.model.allocation("artist", "tue"), 3)
        compare(table.model.resourceUsage("artist"), 8)
        verify(!table.model.resourceConflict("artist"))
        verify(table.model.canUndo)
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(table.model.allocation("artist", "tue"), 9)
        compare(table.model.resourceUsage("artist"), 14)
        verify(table.model.resourceConflict("artist"))
        verify(table.model.overAllocated("artist", "tue"))
        compare(table.Accessible.name, "Weekly resource allocation")
        verify(table.Accessible.description.indexOf("Over allocated") >= 0)
    }

    function test_stable_selection_survives_reorder_by_id() {
        const host = createTemporaryObject(tableComponent, root)
        const table = findChild(host, "resourceTable")
        table.setCurrent(1, 2)
        table.model.setResources([
            { id: "editor", name: "Editor", unit: "hours", capacity: 6 },
            { id: "node", name: "Render node", unit: "GPU hours", capacity: 4 },
            { id: "artist", name: "Artist", unit: "hours", capacity: 8 }
        ])
        tryCompare(table, "activeRow", 1)
        compare(table.activeResourceId, "node")
        compare(table.activeBucketId, "wed")
        verify(table.model.resourceIndex("node") === 1)
    }

    function test_both_axes_are_bounded_and_scroll_anchor_survives_reorder() {
        const host = createTemporaryObject(largeTableComponent, root)
        const table = findChild(host, "resourceTable")
        compare(table.model.resourceCount, 200)
        compare(table.model.bucketCount, 40)
        verify(table.visibleCellRecords.length < 80)
        table.scrollY = 100 * Tk.Theme.size.row + Tk.Theme.space.xs
        table.scrollX = 20 * table.bucketColumnWidth + Tk.Theme.space.xs
        table.setCurrent(102, 22)
        const rowAnchor = table.model.resourceIdAt(100)
        const bucketAnchor = table.model.bucketIdAt(20)
        const resources = []
        const buckets = []
        for (let i = 199; i >= 0; --i)
            resources.push({ id: "r-" + i, name: "Resource " + i,
                             unit: "hours", capacity: 8 })
        for (let i = 39; i >= 0; --i)
            buckets.push({ id: "b-" + i, name: "Bucket " + i })
        table.model.setResources(resources)
        table.model.setBuckets(buckets)
        compare(table.activeResourceId, "r-102")
        compare(table.activeBucketId, "b-22")
        compare(table.model.resourceIdAt(Math.floor(table.scrollY / Tk.Theme.size.row)), rowAnchor)
        compare(table.model.bucketIdAt(Math.floor(table.scrollX / table.bucketColumnWidth)),
                bucketAnchor)
        verify(table.visibleCellRecords.length < 80)
    }
}
