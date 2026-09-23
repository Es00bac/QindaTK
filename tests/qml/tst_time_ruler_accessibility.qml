// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "TimeRulerAccessibility"
    when: windowShown
    visible: true
    width: 480
    height: 80

    Component {
        id: rulerComponent
        Tk.TimeRuler {
            width: 400
            height: Tk.Theme.size.timeRuler
            pixelsPerUnit: 10
            originUnits: 5
            playheadUnits: 15
            tooltip: "Timeline position"
        }
    }

    function makeRuler() {
        const ruler = createTemporaryObject(rulerComponent, root)
        verify(ruler)
        return ruler
    }

    function spyOn(ruler) {
        const spy = signalSpyComponent.createObject(root, {
            target: ruler,
            signalName: "scrubbed"
        })
        verify(spy)
        return spy
    }

    function test_rulerHasAccessibleNameValueAndRange() {
        const ruler = makeRuler()
        compare(ruler.Accessible.name, "Timeline position")
        compare(ruler.value, 15)
        compare(ruler.minimumValue, 5)
        compare(ruler.maximumValue, 45)
        compare(ruler.stepSize, 1)
        verify(ruler.Accessible.description.indexOf("Current position 15s") >= 0)
        verify(ruler.Accessible.focusable)
    }

    function test_keyboardAndAccessibleActionsUseBoundedScrub() {
        const ruler = makeRuler()
        const spy = spyOn(ruler)
        ruler.forceActiveFocus()
        tryCompare(ruler, "activeFocus", true)

        keyPress(Qt.Key_Right)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], 16)

        // The ruler is controlled: a caller applies the requested value.
        // Until then, its next relative action starts from the current value.
        compare(ruler.playheadUnits, 15)
        ruler.playheadUnits = 16
        keyPress(Qt.Key_Left)
        compare(spy.count, 2)
        compare(spy.signalArguments[1][0], 15)

        ruler.Accessible.decreaseAction()
        compare(spy.count, 3)
        compare(spy.signalArguments[2][0], 15)

        keyPress(Qt.Key_Home)
        compare(spy.count, 4)
        compare(spy.signalArguments[3][0], 5)

        keyPress(Qt.Key_End)
        compare(spy.count, 5)
        compare(spy.signalArguments[4][0], 45)

        ruler.playheadUnits = 45
        ruler.Accessible.increaseAction()
        compare(spy.count, 6)
        compare(spy.signalArguments[5][0], 45)
        spy.destroy()
    }

    function test_pointerUsesSameBoundedScrub() {
        const ruler = makeRuler()
        const spy = spyOn(ruler)
        mouseClick(ruler, 100, ruler.height / 2)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], 15)
        spy.destroy()
    }

    function test_externalUpdatesDescribeValueWithoutScrubSignal() {
        const ruler = makeRuler()
        const spy = spyOn(ruler)
        ruler.playheadUnits = 23
        compare(ruler.value, 23)
        verify(ruler.Accessible.description.indexOf("Current position 23s") >= 0)
        ruler.originUnits = 10
        compare(ruler.minimumValue, 10)
        compare(ruler.maximumValue, 50)
        compare(spy.count, 0)

        ruler.playheadUnits = -1
        compare(ruler.value, 10)
        verify(ruler.Accessible.description.indexOf("No playhead is set") >= 0)
        compare(spy.count, 0)
        spy.destroy()
    }

    function test_disabledRulerDoesNotEmitFromAnyInputPath() {
        const ruler = makeRuler()
        const spy = spyOn(ruler)
        ruler.enabled = false
        verify(!ruler.Accessible.focusable)
        mouseClick(ruler, 100, ruler.height / 2)
        ruler.forceActiveFocus()
        keyPress(Qt.Key_Right)
        ruler.Accessible.increaseAction()
        ruler.scrubTo(20)
        compare(spy.count, 0)
        spy.destroy()
    }

    Component {
        id: signalSpyComponent
        SignalSpy { }
    }
}
