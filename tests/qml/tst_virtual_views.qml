// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "VirtualViews"
    when: windowShown
    visible: true
    width: 920
    height: 520

    function rowId(value) { return "00000000-0000-4000-8000-" + String(value).padStart(12, "0") }
    ListModel {
        id: records
        dynamicRoles: false
    }
    Tk.SelectionModel { id: selection }
    QtObject {
        id: fakeProvider
        property int requestCount: 0
        property int releaseCount: 0
        property var released: []
        signal assetChanged(string subscriptionId)
        function requestAsset(resourceId, kind, options, document, viewId) {
            ++requestCount
            return "subscription-" + requestCount
        }
        function asset(subscriptionId) { return ({ subscriptionId: subscriptionId, state: "loading", data: ({}), error: "" }) }
        function release(subscriptionId) { ++releaseCount; released.push(subscriptionId); return true }
    }
    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.md
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.md
        Tk.VirtualList {
            id: list
            width: 280
            height: 400
            model: records
            selectionModel: selection
            tooltip: "Files"
        }
        Tk.VirtualTree {
            id: tree
            width: 280
            height: 400
            model: records
            selectionModel: selection
            tooltip: "Folders"
            flatProjection: true
        }
        Tk.AssetGrid {
            id: grid
            width: 300
            height: 400
            model: records
            selectionModel: selection
            provider: fakeProvider
            tooltip: "Assets"
        }
    }
    SignalSpy { id: expansionSpy; target: tree; signalName: "expansionRequested" }

    function initTestCase() {
        for (let i = 0; i < 240; ++i) {
            records.append({ id: rowId(i), label: "Asset " + i, secondary: "Item " + i,
                icon: "file", depth: i % 3, hasChildren: i === 0, expanded: false,
                loading: false, resourceId: "resource:" + i, caption: "Preview " + i })
        }
        verify(list.collectionAdapter.valid)
        waitForRendering(list)
    }

    function test_modelsRemainDirectAndDelegateCountsStayViewportBounded() {
        compare(list.listView.count, records.count)
        compare(tree.treeView.count, records.count)
        compare(grid.gridView.count, records.count)
        const listDelegates = countDelegates(list.listView, "virtualListDelegate")
        const treeDelegates = countDelegates(tree.treeView, "virtualTreeDelegate")
        const gridDelegates = countDelegates(grid.gridView, "assetGridDelegate")
        verify(listDelegates > 0 && listDelegates < 80)
        verify(treeDelegates > 0 && treeDelegates < 80)
        verify(gridDelegates > 0 && gridDelegates < 100)
        verify(list.model === records && tree.model === records && grid.model === records)
        compare(list.Accessible.name, "Files")
        compare(tree.Accessible.role, Accessible.Tree)
        compare(grid.Accessible.name, "Assets")
        console.log("delegate counts for 240 rows:", listDelegates, treeDelegates, gridDelegates)
    }

    function test_selectionUsesStableIdsAcrossSortAndKeyboard() {
        const chosen = rowId(20)
        const oldRow = list.collectionAdapter.indexOfId(chosen)
        verify(oldRow >= 0)
        verify(selection.select(chosen))
        records.move(oldRow, 0, 1)
        const movedRow = list.collectionAdapter.indexOfId(chosen)
        verify(movedRow >= 0)
        compare(list.collectionAdapter.idAt(movedRow), chosen)
        compare(selection.currentId, chosen)
        list.forceActiveFocus()
        verify(list.activeFocus)
        keyClick(Qt.Key_Down)
        compare(selection.currentId, list.collectionAdapter.idAt(movedRow + 1))
        verify(selection.selectedIds.length === 1)
    }

    function test_pointerModifiersReachStableIdSelection() {
        selection.clear()
        list.listView.positionViewAtBeginning()
        const first = list.listView.itemAtIndex(0)
        const second = list.listView.itemAtIndex(1)
        verify(first && second)
        mouseClick(first, 12, 10, Qt.LeftButton)
        compare(selection.selectedIds.length, 1)
        mouseClick(second, 12, 10, Qt.LeftButton, Qt.ControlModifier)
        compare(selection.selectedIds.length, 2)
        verify(selection.selectedIds.indexOf(list.collectionAdapter.idAt(0)) >= 0)
        verify(selection.selectedIds.indexOf(list.collectionAdapter.idAt(1)) >= 0)
    }

    function test_treeRequestsExpansionByStableIdAndModelOwnsState() {
        const id = rowId(0)
        expansionSpy.clear()
        const disclosure = findChild(tree, "treeDisclosure_" + id)
        verify(disclosure)
        disclosure.click()
        compare(expansionSpy.count, 1)
        compare(expansionSpy.signalArguments[0][0], id)
        compare(expansionSpy.signalArguments[0][1], true)
        records.setProperty(tree.collectionAdapter.indexOfId(id), "expanded", true)
        tryCompare(findChild(tree, "treeDisclosure_" + id), "tooltip", "Collapse Asset 0")
    }

    function test_recycled_asset_delegates_release_thumbnail_subscriptions() {
        const beforeRequests = fakeProvider.requestCount
        const beforeReleases = fakeProvider.releaseCount
        verify(beforeRequests > 0)
        grid.gridView.contentY = Math.max(0, grid.gridView.contentHeight - grid.gridView.height)
        tryVerify(function() { return fakeProvider.releaseCount > beforeReleases }, 3000)
        verify(fakeProvider.requestCount > beforeRequests)
        verify(fakeProvider.released.length > 0)
    }

    function countDelegates(view, name) {
        let count = 0
        const children = view.contentItem.children
        for (let i = 0; i < children.length; ++i)
            if (children[i].objectName === name) ++count
        return count
    }
}
