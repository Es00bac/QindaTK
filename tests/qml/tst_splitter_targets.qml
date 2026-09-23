// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "SplitterTargets"
    when: windowShown
    visible: true
    width: 420
    height: 420
    property string formerDensity

    Component {
        id: fixture
        Tk.Splitter {
            width: 360
            height: 360
            Tk.Button {
                objectName: "firstPane"
                text: "First"
                T.SplitView.preferredWidth: 160
                T.SplitView.preferredHeight: 160
                T.SplitView.minimumWidth: 80
                T.SplitView.minimumHeight: 80
                T.SplitView.maximumWidth: 240
                T.SplitView.maximumHeight: 240
            }
            Tk.Button { objectName: "secondPane"; text: "Second" }
        }
    }
    Component {
        id: threePaneFixture
        Tk.Splitter {
            width: 360
            height: 240
            Item { objectName: "paneA"; T.SplitView.preferredWidth: 100; T.SplitView.minimumWidth: 60 }
            Item { objectName: "paneB"; T.SplitView.preferredWidth: 120; T.SplitView.minimumWidth: 60 }
            Item { objectName: "paneC"; T.SplitView.fillWidth: true }
        }
    }

    function allNamed(parent, name, found) {
        if (parent.objectName === name) found.push(parent)
        for (const child of parent.children) allNamed(child, name, found)
        return found
    }

    function init() { formerDensity = Tk.Density.modeName }
    function cleanup() { Tk.Density.modeName = formerDensity }

    function test_expanded_target_data() {
        const rows = []
        for (const density of ["compact", "comfortable", "touch"])
            for (const orientation of [Qt.Horizontal, Qt.Vertical])
                for (const side of [-1, 1])
                    rows.push({tag: density + "-" + orientation + "-" + side,
                               density: density, orientation: orientation, side: side})
        return rows
    }

    function test_expanded_target(data) {
        Tk.Density.modeName = data.density
        const split = createTemporaryObject(fixture, root, {orientation: data.orientation})
        verify(split)
        const seam = findChild(split, "splitterHandle")
        const target = findChild(split, "splitterHitTarget")
        const pane = findChild(split, "firstPane")
        const horizontal = data.orientation === Qt.Horizontal
        tryCompare(seam, horizontal ? "width" : "height", Tk.Theme.size.seam)
        compare(target[horizontal ? "width" : "height"], Tk.Theme.size.seamTarget)
        const before = pane[horizontal ? "width" : "height"]
        compare(before, 160)
        const state = split.saveState()
        const offset = data.side * (Tk.Theme.size.seamTarget / 2 - 1)
        const start = seam.mapToItem(root, seam.width / 2 + (horizontal ? offset : 0),
                                    seam.height / 2 + (horizontal ? 0 : offset))
        const touch = touchEvent(root)
        touch.press(0, root, start.x, start.y).commit()
        tryCompare(split, "resizing", true)
        compare(pane[horizontal ? "width" : "height"], before)
        const finish = Qt.point(start.x + (horizontal ? 40 : 0),
                                start.y + (horizontal ? 0 : 40))
        touch.move(0, root, finish.x, finish.y).commit()
        tryCompare(pane, horizontal ? "width" : "height", before + 40)
        touch.release(0, root, finish.x, finish.y).commit()
        tryCompare(split, "resizing", false)
        verify(split.restoreState(state))
        tryCompare(pane, horizontal ? "width" : "height", before)
        compare(seam[horizontal ? "width" : "height"], Tk.Theme.size.seam)
    }

    function test_mouse_constraints_and_pane_click_data() {
        return [{tag: "horizontal", orientation: Qt.Horizontal},
                {tag: "vertical", orientation: Qt.Vertical}]
    }

    function test_mouse_constraints_and_pane_click(data) {
        Tk.Density.modeName = "touch"
        const split = createTemporaryObject(fixture, root, {orientation: data.orientation})
        const seam = findChild(split, "splitterHandle")
        const pane = findChild(split, "firstPane")
        const horizontal = data.orientation === Qt.Horizontal
        tryCompare(pane, horizontal ? "width" : "height", 160)
        const start = seam.mapToItem(root, seam.width / 2 + (horizontal ? 20 : 0),
                                    seam.height / 2 + (horizontal ? 0 : 20))
        mousePress(root, start.x, start.y)
        tryCompare(split, "resizing", true)
        mouseMove(root, start.x - (horizontal ? 120 : 0), start.y - (horizontal ? 0 : 120))
        tryCompare(pane, horizontal ? "width" : "height", 80)
        mouseRelease(root, start.x - (horizontal ? 120 : 0), start.y - (horizontal ? 0 : 120))
        tryCompare(split, "resizing", false)
        const spy = createTemporaryQmlObject('import QtTest; SignalSpy {}', root)
        spy.target = pane
        spy.signalName = "clicked"
        // Outside the expanded target, the adjacent pane keeps its own input.
        mouseClick(pane, pane.width / 2, pane.height / 2)
        compare(spy.count, 1)
        compare(split.resizing, false)
    }

    function test_keyboard_accessible_resize_data() {
        return [{tag: "horizontal", orientation: Qt.Horizontal, key: Qt.Key_Right},
                {tag: "vertical", orientation: Qt.Vertical, key: Qt.Key_Down}]
    }

    function test_keyboard_accessible_resize(data) {
        Tk.Density.modeName = "compact"
        const split = createTemporaryObject(fixture, root, {orientation: data.orientation})
        verify(split !== null)
        const seam = findChild(split, "splitterHandle")
        const pane = findChild(split, "firstPane")
        const axis = data.orientation === Qt.Horizontal ? "width" : "height"
        tryCompare(pane, axis, 160)
        compare(seam.Accessible.role, Accessible.Splitter)
        compare(seam.Accessible.name, "Resize pane 1")
        verify(seam.Accessible.focusable)
        seam.forceActiveFocus(Qt.TabFocusReason)
        verify(seam.activeFocus)
        verify(findChild(seam, "splitterFocusRing").visible)

        keyClick(data.key)
        tryCompare(pane, axis, 160 + Tk.Theme.space.lg)
        seam.Accessible.decreaseAction()
        tryCompare(pane, axis, 160)
        seam.Accessible.increaseAction()
        tryCompare(pane, axis, 160 + Tk.Theme.space.lg)

        for (let i = 0; i < 30; ++i) {
            seam.Accessible.increaseAction()
            tryCompare(pane, axis, Math.min(240, 160 + (i + 2) * Tk.Theme.space.lg))
        }
        tryCompare(pane, axis, 240)
        seam.Accessible.increaseAction()
        compare(pane[axis], 240)
        for (let i = 0; i < 40; ++i) {
            seam.Accessible.decreaseAction()
            tryCompare(pane, axis, Math.max(80, 240 - (i + 1) * Tk.Theme.space.lg))
        }
        tryCompare(pane, axis, 80)
        seam.Accessible.decreaseAction()
        compare(pane[axis], 80)

        split.enabled = false
        verify(!seam.Accessible.focusable)
        verify(!findChild(seam, "splitterFocusRing").visible)
        seam.Accessible.increaseAction()
        keyClick(data.key)
        compare(pane[axis], 80)
    }

    function test_second_handle_resizes_second_pane_only() {
        const split = createTemporaryObject(threePaneFixture, root)
        verify(split !== null)
        const handles = allNamed(split, "splitterHandle", [])
        compare(handles.length, 2)
        const first = findChild(split, "paneA")
        const second = findChild(split, "paneB")
        const later = handles[0].x > handles[1].x ? handles[0] : handles[1]
        tryCompare(first, "width", 100)
        tryCompare(second, "width", 120)
        compare(later.Accessible.name, "Resize pane 2")
        later.Accessible.increaseAction()
        tryCompare(second, "width", 120 + Tk.Theme.space.lg)
        compare(first.width, 100)
    }
}
