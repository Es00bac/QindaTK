// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "DockDividerAccessibility"
    when: windowShown
    visible: true
    width: 900
    height: 600

    Component {
        id: verticalDivider
        Tk.DockDivider {
            objectName: "verticalDivider"
            width: Tk.Theme.size.seam
            height: 120
            vertical: true
            tooltip: "Resize side panels"
        }
    }
    Component {
        id: horizontalDivider
        Tk.DockDivider {
            objectName: "horizontalDivider"
            width: 240
            height: Tk.Theme.size.seam
            vertical: false
        }
    }
    Component {
        id: hostComponent
        Tk.DockHost {
            width: 900
            height: 600
            workspace: "divider-accessibility"
            canvas: Rectangle { color: "black" }
            Tk.DockPanel { panelId: "a"; title: "Alpha"; zone: "right"; extent: 240; order: 0
                Rectangle { color: "red" } }
            Tk.DockPanel { panelId: "b"; title: "Beta"; zone: "right"; extent: 240; order: 1
                Rectangle { color: "green" } }
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

    function divider(component) {
        const item = createTemporaryObject(component, root)
        verify(item !== null)
        return item
    }

    function dragSpy(item) {
        const spy = signalSpyComponent.createObject(root, {
            target: item,
            signalName: "dragged"
        })
        verify(spy !== null)
        return spy
    }

    function test_verticalDividerKeyboardAndAssistiveActionsUseDragDelta() {
        const item = divider(verticalDivider)
        const spy = dragSpy(item)
        const width = item.width

        compare(item.Accessible.role, Accessible.Splitter)
        compare(item.Accessible.name, "Resize side panels")
        verify(item.Accessible.focusable)
        verify(item.activeFocusOnTab)
        verify(item.Accessible.description.indexOf("Left and Right") >= 0)
        verify(item.Accessible.description.indexOf("Increase moves it right") >= 0)

        item.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(item, "activeFocus", true)
        verify(findItem(item, "verticalDivider_focus").visible)

        keyClick(Qt.Key_Left)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], -item.keyboardStep)
        keyClick(Qt.Key_Right)
        compare(spy.count, 2)
        compare(spy.signalArguments[1][0], item.keyboardStep)
        keyClick(Qt.Key_Up)
        compare(spy.count, 2)

        item.Accessible.increaseAction()
        item.Accessible.decreaseAction()
        compare(spy.count, 4)
        compare(spy.signalArguments[2][0], item.keyboardStep)
        compare(spy.signalArguments[3][0], -item.keyboardStep)
        compare(item.width, width)
        spy.destroy()
    }

    function test_horizontalDividerKeyboardAndAssistiveActionsUseDragDelta() {
        const item = divider(horizontalDivider)
        const spy = dragSpy(item)

        compare(item.Accessible.role, Accessible.Splitter)
        compare(item.Accessible.name, "Resize dock panels")
        verify(item.Accessible.description.indexOf("Up and Down") >= 0)
        verify(item.Accessible.description.indexOf("Increase moves it down") >= 0)
        item.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(item, "activeFocus", true)

        keyClick(Qt.Key_Up)
        keyClick(Qt.Key_Down)
        compare(spy.count, 2)
        compare(spy.signalArguments[0][0], -item.keyboardStep)
        compare(spy.signalArguments[1][0], item.keyboardStep)
        item.Accessible.decreaseAction()
        item.Accessible.increaseAction()
        compare(spy.count, 4)
        compare(spy.signalArguments[2][0], -item.keyboardStep)
        compare(spy.signalArguments[3][0], item.keyboardStep)
        spy.destroy()
    }

    function test_disabledDividerRejectsKeyboardAndAssistiveResize() {
        const item = divider(verticalDivider)
        const spy = dragSpy(item)
        item.enabled = false

        verify(!item.Accessible.focusable)
        verify(!item.activeFocusOnTab)
        keyClick(Qt.Key_Left)
        item.Accessible.increaseAction()
        item.Accessible.decreaseAction()
        item.moveBy(10)
        compare(spy.count, 0)
        spy.destroy()
    }

    function test_dockHostKeepsModelAsResizeAuthorityForKeyboardAndAssistiveActions() {
        const host = createTemporaryObject(hostComponent, root)
        verify(host !== null)
        tryVerify(() => findItem(host, "dockShareSeam_right_0_0") !== null)

        const laneDivider = findItem(host, "dockDivider_right_0")
        const shareDivider = findItem(host, "dockShareSeam_right_0_0")
        verify(laneDivider && shareDivider)
        const oldExtent = host.laneExtent("right", 0)
        laneDivider.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(laneDivider, "activeFocus", true)
        keyClick(Qt.Key_Right)
        tryVerify(() => host.laneExtent("right", 0) < oldExtent)
        compare(host.laneExtent("right", 0), oldExtent - laneDivider.keyboardStep)

        const slotsBefore = host.model.lanes("right")[0].slots
        const firstShare = slotsBefore[0].share
        const secondShare = slotsBefore[1].share
        shareDivider.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(shareDivider, "activeFocus", true)
        shareDivider.Accessible.increaseAction()
        const slotsAfter = host.model.lanes("right")[0].slots
        verify(slotsAfter[0].share > firstShare)
        verify(slotsAfter[1].share < secondShare)
        verify(Math.abs(slotsAfter[0].share + slotsAfter[1].share
                        - firstShare - secondShare) < 0.001)

        const extentAfterResize = host.laneExtent("right", 0)
        const shareAfterResize = slotsAfter[0].share
        host.enabled = false
        verify(!laneDivider.Accessible.focusable)
        verify(!shareDivider.Accessible.focusable)
        keyClick(Qt.Key_Up)
        laneDivider.Accessible.increaseAction()
        shareDivider.Accessible.decreaseAction()
        compare(host.laneExtent("right", 0), extentAfterResize)
        compare(host.model.lanes("right")[0].slots[0].share, shareAfterResize)
    }

    Component { id: signalSpyComponent; SignalSpy { } }
}
