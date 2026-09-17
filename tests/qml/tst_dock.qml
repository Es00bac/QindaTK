// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// DockHost draws what DockModel says: frames land in their zones, model
// operations re-render, seams follow lane extents, and panel content is
// reparented (never re-created) across moves.
TestCase {
    id: root
    name: "Dock"
    when: windowShown
    // AGENT-NOTE: TestCase items are invisible by default and Flex skips
    // invisible children, so the host must sit in a visible item tree.
    visible: true
    width: 900
    height: 600

    Component {
        id: hostComponent
        Tk.DockHost {
            width: 900
            height: 600
            workspace: "test"
            canvas: Rectangle { objectName: "testCanvas"; color: "black" }
            Tk.DockPanel { panelId: "a"; title: "Alpha"; zone: "left"; extent: 200
                Rectangle { objectName: "contentA"; color: "red" } }
            Tk.DockPanel { panelId: "b"; title: "Beta"; zone: "right"; extent: 240; order: 0
                Rectangle { objectName: "contentB"; color: "green" } }
            Tk.DockPanel { panelId: "c"; title: "Gamma"; zone: "right"; extent: 240; order: 1
                Rectangle { objectName: "contentC"; color: "blue" } }
        }
    }

    // Walks the item tree (Repeater delegates are item children, not
    // QObject children, so TestCase.findChild would miss them).
    function findItem(parent, name) {
        for (let i = 0; i < parent.children.length; ++i) {
            const child = parent.children[i]
            if (child.objectName === name) {
                return child
            }
            const found = findItem(child, name)
            if (found !== null) {
                return found
            }
        }
        return null
    }

    function isInside(item, ancestor) {
        let p = item
        while (p !== null && p !== undefined) {
            if (p === ancestor) {
                return true
            }
            p = p.parent
        }
        return false
    }

    function test_frames_land_in_zones() {
        const host = createTemporaryObject(hostComponent, root)
        tryVerify(function() { return findItem(host, "dockFrame_a") !== null })
        const frameA = findItem(host, "dockFrame_a")
        const frameB = findItem(host, "dockFrame_b")
        const frameC = findItem(host, "dockFrame_c")
        verify(isInside(frameA, findItem(host, "dockZone_left")))
        verify(isInside(frameB, findItem(host, "dockZone_right")))
        verify(isInside(frameC, findItem(host, "dockZone_right")))
        verify(findItem(host, "dockLane_left_0") !== null)
        verify(findItem(host, "dockCanvas") !== null)
        verify(isInside(findItem(host, "testCanvas"), findItem(host, "dockCanvas")))
        tryCompare(findItem(host, "dockLane_left_0"), "width", 200)
        // Stacked slots share the lane height equally (2 slots, one seam).
        tryVerify(function() { return Math.abs(frameB.height - frameC.height) <= 1 })
        // Content items were seated inside their frames.
        verify(isInside(findItem(host, "contentA"), frameA))
        compare(host.hiddenPanels.length, 0)
        compare(host.panelMenuModel.length, 3)
    }

    function test_model_ops_rerender_and_keep_content() {
        const host = createTemporaryObject(hostComponent, root)
        tryVerify(function() { return findItem(host, "dockFrame_b") !== null })
        const contentB = findItem(host, "contentB")
        const declB = host.declarationOf("b")
        const holder = declB.contentItem
        verify(holder !== null)

        host.model.floatPanel("b", 300, 100)
        tryVerify(function() { return findItem(host, "dockFloating_b") !== null })
        // Let the replaced frame's deferred destruction run: content must
        // stay seated in the floating frame afterwards.
        wait(50)
        verify(findItem(host, "dockFrame_b") === null)
        const floating = findItem(host, "dockFloating_b")
        compare(floating.x, 300)
        compare(floating.y, 100)
        // Same content object, now inside the floating frame.
        compare(findItem(host, "contentB"), contentB)
        verify(isInside(contentB, floating))
        compare(declB.contentItem, holder)

        host.model.hide("b")
        tryVerify(function() { return findItem(host, "dockFloating_b") === null })
        compare(host.hiddenPanels, ["b"])
        // Hidden content lives in the declaration again.
        compare(holder.parent, declB)

        host.showPanel("b")
        tryVerify(function() { return findItem(host, "dockFrame_b") !== null })
        wait(50)
        verify(isInside(contentB, findItem(host, "dockFrame_b")))
    }

    function test_group_shows_tab_strip() {
        const host = createTemporaryObject(hostComponent, root)
        tryVerify(function() { return findItem(host, "dockFrame_c") !== null })
        const contentC = findItem(host, "contentC")
        host.model.groupWith("c", "b")
        tryVerify(function() { return findItem(host, "dockTab_c") !== null })
        wait(50)
        verify(findItem(host, "dockTab_b") !== null)
        verify(isInside(contentC, findItem(host, "dockContent_c")))
        // The grouped frame is named after its active tab (c joined last).
        verify(findItem(host, "dockFrame_c") !== null)
        verify(findItem(host, "dockContent_b") !== null)
        verify(!findItem(host, "dockContent_b").visible)
        verify(findItem(host, "dockContent_c").visible)
        host.model.activateInGroup("b")
        tryVerify(function() { return findItem(host, "dockContent_b").visible })
        verify(!findItem(host, "dockContent_c").visible)
        host.model.ungroup("c")
        tryVerify(function() { return findItem(host, "dockTab_c") === null })
        wait(50)
        verify(isInside(contentC, findItem(host, "dockFrame_c")))
    }

    function test_lane_extent_follows_model() {
        const host = createTemporaryObject(hostComponent, root)
        tryVerify(function() { return findItem(host, "dockLane_right_0") !== null })
        host.model.setLaneExtent("right", 0, 320)
        tryCompare(findItem(host, "dockLane_right_0"), "width", 320)
        const zone = findItem(host, "dockZone_right")
        tryCompare(zone, "width", 320 + Tk.Theme.size.seam)
        // The divider is drawn before a right-zone lane.
        const divider = findItem(host, "dockDivider_right_0")
        verify(divider !== null)
        verify(divider.x < findItem(host, "dockLane_right_0").x)
        host.resizeLane("right", 0, -20)
        tryCompare(findItem(host, "dockLane_right_0"), "width", 300)
    }

    function test_share_seam_keeps_total() {
        const host = createTemporaryObject(hostComponent, root)
        tryVerify(function() { return findItem(host, "dockShareSeam_right_0_0") !== null })
        const before = host.slotInfo("right", 0, 0).share + host.slotInfo("right", 0, 1).share
        host.adjustShares("right", 0, 0, 100, 500)
        const a = host.slotInfo("right", 0, 0).share
        const b = host.slotInfo("right", 0, 1).share
        verify(a > b)
        verify(Math.abs(a + b - before) < 0.001)
        const frameB = findItem(host, "dockFrame_b")
        const frameC = findItem(host, "dockFrame_c")
        tryVerify(function() { return frameB.height > frameC.height })
    }

    function test_dock_beside_rebuilds_zone() {
        const host = createTemporaryObject(hostComponent, root)
        tryVerify(function() { return findItem(host, "dockFrame_a") !== null })
        host.model.dockBeside("a", "b", "lane-after")
        tryVerify(function() { return findItem(host, "dockLane_right_1") !== null })
        verify(findItem(host, "dockZone_left") === null || !findItem(host, "dockZone_left").visible)
        verify(isInside(findItem(host, "dockFrame_a"), findItem(host, "dockLane_right_1")))
    }

    function test_drag_hit_test_targets() {
        const host = createTemporaryObject(hostComponent, root)
        tryVerify(function() { return findItem(host, "dockFrame_a") !== null })
        host.beginDrag("a", host.mapToItem(null, 5, 300), false)
        verify(host.dragging)
        compare(host.dropTarget.kind, "zone")
        compare(host.dropTarget.zone, "left")
        const frameB = findItem(host, "dockFrame_b")
        tryVerify(function() { return frameB.width > 0 && frameB.height > 0 })
        const centre = frameB.mapToItem(null, frameB.width / 2, frameB.height / 2)
        host.updateDrag(centre)
        compare(host.dropTarget.kind, "beside")
        compare(host.dropTarget.target, "b")
        compare(host.dropTarget.placement, "tab")
        host.endDrag()
        verify(!host.dragging)
        tryVerify(function() { return findItem(host, "dockTab_a") !== null })
    }
}
