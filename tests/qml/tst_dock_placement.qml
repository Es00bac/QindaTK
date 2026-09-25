// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "DockPlacement"
    visible: true
    when: windowShown
    width: 900
    height: 700
    Component {
        id: fixture
        Tk.DockHost {
            width: 900; height: 700
            workspace: "placement-test"
            Tk.DockPanel { panelId: "a"; title: "Alpha"; zone: "left"; group: "tools"; groupActive: true }
            Tk.DockPanel { panelId: "b"; title: "Beta"; zone: "left"; group: "tools"; groupActive: false }
            Tk.DockPanel { panelId: "c"; title: "Gamma"; zone: "right" }
            Tk.DockPanel {
                panelId: "f"; title: "Floating"; mode: "floating"
                allowedZones: ["left", "right"]
                floatingRect: ({ x: 100, y: 100, width: 280, height: 240 })
            }
        }
    }
    SignalSpy { id: acceptedSpy; signalName: "panelPlacementAccepted" }
    function init() { acceptedSpy.clear() }
    function find(parent, name) { return findChild(parent, name) }
    function setup() {
        const host = createTemporaryObject(fixture, root)
        verify(host !== null)
        tryVerify(function() { return find(host, "dockPlacement_a") !== null })
        acceptedSpy.target = host
        return host
    }
    function dialog(host) { return find(host, "dockPlacementDialog") }
    function choose(d, predicate) {
        const index = d.choices.findIndex(predicate)
        verify(index >= 0)
        find(d.contentItem, "dockPlacementDestination").currentIndex = index
    }
    function test_float_draft_cancel_and_keyboard_apply() {
        const host = setup()
        const button = find(host, "dockPlacement_f")
        compare(button.Accessible.name, "Place Floating")
        compare(button.Accessible.role, Accessible.Button)
        button.forceActiveFocus(Qt.TabFocusReason)
        verify(button.activeFocus)
        keyClick(Qt.Key_Space)
        const d = dialog(host)
        tryCompare(d, "opened", true)
        compare(d.selected.kind, "move")
        const x = find(d.contentItem, "dockPlacementX")
        x.inputItem.forceActiveFocus()
        keyClick(Qt.Key_Up)
        compare(x.value, 100 + Tk.Theme.space.lg)
        compare(host.model.panel("f").x, 100)
        find(d.footer, "dialogSecondary").Accessible.pressAction()
        tryCompare(d, "visible", false)
        compare(host.model.panel("f").x, 100)
        compare(acceptedSpy.count, 0)
        button.Accessible.pressAction()
        tryCompare(d, "opened", true)
        x.inputItem.forceActiveFocus()
        keyClick(Qt.Key_Up)
        const apply = find(d.footer, "dialogPrimary")
        apply.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryCompare(d, "visible", false)
        compare(host.model.panel("f").x, 100 + Tk.Theme.space.lg)
        compare(acceptedSpy.count, 1)
        tryVerify(function() { return find(host, "dockPlacement_f").activeFocus })
    }
    function test_destinations_order_and_active_panel() {
        const host = setup()
        const tab = find(host, "dockTab_b")
        tab.forceActiveFocus()
        keyClick(Qt.Key_F10, Qt.ShiftModifier)
        const d = dialog(host)
        tryCompare(d, "opened", true)
        compare(d.panelId, "b")
        choose(d, function(c) { return c.kind === "order" && c.index === 0 })
        find(d.footer, "dialogPrimary").Accessible.pressAction()
        tryCompare(d, "visible", false)
        compare(host.model.lanes("left")[0].slots[0].panelIds[0], "b")
        compare(host.model.lanes("left")[0].slots[0].activeId, "a")
        compare(acceptedSpy.count, 1)
        host.openPlacement("b")
        tryCompare(d, "opened", true)
        choose(d, function(c) { return c.kind === "beside" && c.target === "c" && c.placement === "tab" })
        d.accept()
        tryCompare(d, "visible", false)
        compare(host.model.panel("b").zone, "right")
        compare(host.model.panel("b").group, host.model.panel("c").group)
        compare(host.model.lanes("right")[0].slots[0].activeId, "b")
        compare(acceptedSpy.count, 2)
    }
    function test_relative_destination_data() {
        return [
            { tag: "before", placement: "before" },
            { tag: "after", placement: "after" },
            { tag: "lane-before", placement: "lane-before" },
            { tag: "lane-after", placement: "lane-after" }
        ]
    }
    function test_relative_destination(data) {
        const host = setup()
        host.openPlacement("f")
        const d = dialog(host)
        tryCompare(d, "opened", true)
        choose(d, function(c) { return c.kind === "beside" && c.target === "c" && c.placement === data.placement })
        find(d.footer, "dialogPrimary").Accessible.pressAction()
        tryCompare(d, "visible", false)
        const moved = host.model.panel("f")
        const target = host.model.panel("c")
        compare(moved.zone, "right")
        compare(moved.docked, true)
        if (data.placement === "before") verify(moved.order < target.order)
        if (data.placement === "after") verify(moved.order > target.order)
        if (data.placement === "lane-before") verify(moved.lane < target.lane)
        if (data.placement === "lane-after") verify(moved.lane > target.lane)
        compare(acceptedSpy.count, 1)
    }
    function test_bounds_disabled_stale_and_allowed_zones() {
        const host = setup()
        host.openPlacement("f")
        const d = dialog(host)
        tryCompare(d, "opened", true)
        verify(!d.choices.some(function(c) { return c.kind === "zone" && c.zone === "top" }))
        const x = find(d.contentItem, "dockPlacementX")
        const y = find(d.contentItem, "dockPlacementY")
        x.value = 100000; y.value = -100000
        d.accept()
        tryCompare(d, "visible", false)
        compare(host.model.panel("f").x, 620)
        compare(host.model.panel("f").y, 0)
        compare(acceptedSpy.count, 1)
        host.enabled = false
        host.openPlacement("f")
        compare(d.visible, false)
        host.enabled = true
        host.openPlacement("f")
        tryCompare(d, "opened", true)
        choose(d, function(c) { return c.kind === "zone" && c.zone === "right" })
        host.model.hide("f")
        d.accept()
        tryCompare(d, "visible", false)
        compare(host.model.panel("f").hidden, true)
        compare(acceptedSpy.count, 1)
    }

    function test_small_touch_large_text_keeps_position_fields_reachable() {
        const window = root.Window.window
        const previousWidth = window.width
        const previousHeight = window.height
        const previousDensity = Tk.Density.modeName
        const previousScale = Tk.Density.textScale
        try {
            window.width = 480
            window.height = 320
            Tk.Density.modeName = "touch"
            Tk.Density.textScale = 3
            const host = setup()
            host.width = 480
            host.height = 320
            host.openPlacement("f")
            const d = dialog(host)
            tryCompare(d, "opened", true)
            const body = d.contentItem
            const y = find(body, "dockPlacementY")
            verify(y !== null)
            verify(d.y >= 0 && d.y + d.height <= window.height)
            verify(body.viewportHeight >= Tk.Theme.size.controlSm)
            verify(body.scrollableY)
            y.inputItem.forceActiveFocus(Qt.TabFocusReason)
            tryVerify(function() { return body.contentY > 0 })
            // The panel's y (100) is past this small host's range; the field
            // shows and steps from the clamped value, not the raw draft (D-198).
            const draftY = y.clampedValue
            keyClick(Qt.Key_Down)
            compare(y.value, draftY - Tk.Theme.space.lg)
            compare(host.model.panel("f").y, 100)
            d.reject()
        } finally {
            Tk.Density.textScale = previousScale
            Tk.Density.modeName = previousDensity
            window.width = previousWidth
            window.height = previousHeight
        }
    }
}
