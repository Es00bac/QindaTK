// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "DockOverflow"
    when: windowShown
    visible: true
    width: 1280
    height: 800
    property int oldDensity
    property real oldScale

    function init() {
        oldDensity = Tk.Density.mode
        oldScale = Tk.Density.textScale
        Tk.Density.mode = Tk.Density.Touch
        Tk.Density.textScale = 3
    }
    function cleanup() {
        Tk.Density.mode = oldDensity
        Tk.Density.textScale = oldScale
    }
    function item(parent, name) {
        if (parent.objectName === name) return parent
        for (const child of parent.children) {
            const found = item(child, name)
            if (found) return found
        }
        return null
    }
    function test_example_workspace_overflow_preserves_arrangement() {
        const component = Qt.createComponent("../../examples/docking/Main.qml")
        compare(component.status, Component.Ready, component.errorString())
        const example = createTemporaryObject(component, root, { width: 480, height: 320 })
        verify(example)
        const host = item(example, "dock")
        const viewport = item(example, "dockingViewport")
        tryVerify(function() { return host.canvasItem.width > 400 && host.canvasItem.height > 200 })
        verify(viewport.scrollableX)
        verify(viewport.scrollableY)
        const canvas = host.canvasItem
        viewport.ensureVisible(canvas)
        tryVerify(function() { return viewport.contentX > 0 && viewport.contentY > 0 })
        let point = canvas.mapToItem(viewport, canvas.width / 2, canvas.height / 2)
        verify(point.x >= 0 && point.x <= viewport.width)
        verify(point.y >= 0 && point.y <= viewport.height)
        // The keyboard viewport can reach the far edges without a pointer.
        viewport.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_End)
        tryCompare(viewport, "contentY", viewport.contentHeight - viewport.viewportHeight)
        keyClick(Qt.Key_Home)
        tryCompare(viewport, "contentY", 0)
        const floating = item(example, "dockFloating_history")
        viewport.ensureVisible(floating)
        point = floating.mapToItem(viewport, floating.width / 2, floating.height / 2)
        verify(point.x >= 0 && point.x <= viewport.width)
        verify(point.y >= 0 && point.y <= viewport.height)
        const seam = item(example, "dockDivider_left_0")
        viewport.ensureVisible(seam)
        const extent = host.model.panel("sourceBin").extent
        mouseDrag(seam, seam.width / 2, seam.height - 100, 30, 0, Qt.LeftButton)
        tryVerify(function() { return Math.abs(host.model.panel("sourceBin").extent - extent - 30) <= 1 })
        const channels = item(example, "dockTab_channels")
        viewport.ensureVisible(channels)
        mouseClick(channels)
        tryVerify(function() { return host.model.panel("channels").groupActive })
        const arrangement = host.model.serialize()
        example.width = 1280
        example.height = 800
        tryCompare(viewport, "scrollableX", false)
        tryCompare(viewport, "scrollableY", false)
        compare(host.model.serialize(), arrangement)
    }
}
