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
}
