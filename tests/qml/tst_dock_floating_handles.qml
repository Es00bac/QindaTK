// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "DockFloatingHandles"
    when: windowShown
    visible: true
    width: 640
    height: 480
    property string formerDensity

    Component {
        id: fixture
        Tk.DockHost {
            width: 640
            height: 480
            workspace: "floating-handle-test"
            canvas: Rectangle { color: Tk.Theme.color.canvas }
            Tk.DockPanel {
                panelId: "float"
                title: "Floating panel"
                mode: "floating"
                minWidth: 100
                minHeight: 100
                floatingRect: ({ "x": 180, "y": 120, "width": 240, "height": 180 })
                Rectangle { color: Tk.Theme.color.panel }
            }
        }
    }

    function findItem(parent, name) {
        for (let i = 0; i < parent.children.length; ++i) {
            const child = parent.children[i]
            if (child.objectName === name) return child
            const found = findItem(child, name)
            if (found !== null) return found
        }
        return null
    }

    function init() { formerDensity = Tk.Density.modeName }
    function cleanup() { Tk.Density.modeName = formerDensity }

    function test_touch_targets_and_corner_distinction() {
        Tk.Density.modeName = "touch"
        const host = createTemporaryObject(fixture, root)
        tryVerify(function() { return findItem(host, "dockFloating_float") !== null })
        const frame = findItem(host, "dockFloating_float")
        const left = findItem(host, "dockFloatingResize_float_left")
        const corner = findItem(host, "dockFloatingResize_float_topLeft")
        verify(left !== null)
        verify(corner !== null)
        compare(left.width, Tk.Theme.size.seamTarget)
        compare(corner.width, Math.min(Tk.Theme.size.seamTarget, frame.width / 2, frame.height / 2))
        compare(corner.height, corner.width)
        // The vertical edge's target begins exactly below the top corner target.
        const edgeStart = left.y
        verify(Math.abs(edgeStart - corner.cornerTarget) < 0.1)

        const beforeWidth = frame.width
        const start = left.mapToItem(root, left.width / 2, left.height / 2)
        waitForRendering(frame)
        const touch = touchEvent(root)
        touch.press(0, root, start.x, start.y).commit()
        touch.move(0, root, start.x - 8, start.y).commit()
        const drag = findChild(left, "dockFloatingResizeDrag_float_left")
        tryCompare(drag, "active", true)
        touch.move(0, root, start.x - 24, start.y).commit()
        tryCompare(frame, "width", beforeWidth + 24)
        touch.release(0, root, start.x - 24, start.y).commit()
    }

    function test_keyboard_and_assistive_actions() {
        Tk.Density.modeName = "compact"
        const host = createTemporaryObject(fixture, root)
        tryVerify(function() { return findItem(host, "dockFloating_float") !== null })
        const frame = findItem(host, "dockFloating_float")
        const right = findItem(host, "dockFloatingResize_float_right")
        compare(right.Accessible.role, Accessible.Splitter)
        compare(right.Accessible.name, "Resize right of Floating panel")
        compare(right.Accessible.focusable, true)
        const before = frame.width
        right.Accessible.increaseAction()
        tryCompare(frame, "width", before + Tk.Theme.space.lg)
        const afterAccessible = frame.width
        right.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return right.activeFocus })
        verify(findItem(right, right.objectName + "_focus").visible)
        keyClick(Qt.Key_Left)
        tryCompare(frame, "width", afterAccessible - Tk.Theme.space.lg)
        compare(frame.x, 180)
    }

    function test_minimum_size_keeps_corner_targets_disjoint() {
        Tk.Density.modeName = "touch"
        const host = createTemporaryObject(fixture, root)
        tryVerify(function() { return findItem(host, "dockFloating_float") !== null })
        host.model.setFloatingRect("float", 180, 120, 100, 100)
        const frame = findItem(host, "dockFloating_float")
        const left = findItem(host, "dockFloatingResize_float_left")
        const top = findItem(host, "dockFloatingResize_float_top")
        const topLeft = findItem(host, "dockFloatingResize_float_topLeft")
        const topRight = findItem(host, "dockFloatingResize_float_topRight")
        compare(frame.width, 100)
        compare(frame.height, 100)
        compare(topLeft.width, Tk.Theme.size.seamTarget)
        compare(topLeft.height, Tk.Theme.size.seamTarget)
        verify(topLeft.x + topLeft.width <= topRight.x)
        compare(left.enabled, false)
        compare(top.enabled, false)
        compare(left.Accessible.focusable, false)
    }

}
