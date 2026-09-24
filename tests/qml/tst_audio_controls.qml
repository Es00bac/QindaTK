// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "AudioControls"
    when: windowShown
    visible: true
    width: 420
    height: 220

    QtObject {
        id: mapper
        function positionFor(value, silent) { return silent ? 0 : (value + 60) / 66 }
        function valueAt(position) {
            const p = Math.max(0, Math.min(1, position))
            return { value: -60 + p * 66, silent: p === 0 }
        }
    }
    Component {
        id: knobComponent
        Tk.Knob { width: 110; mapper: mapper; from: -60; to: 6; value: -27; unit: " dB" }
    }
    Component {
        id: faderComponent
        Tk.Fader { width: 100; height: 240; mapper: mapper; from: -60; to: 6; value: -60; silent: true }
    }

    function test_mapper_and_keyboard_are_one_gesture() {
        const knob = createTemporaryObject(knobComponent, root, { x: 10, y: 10 })
        verify(knob !== null)
        const commits = createTemporaryObject(spyComponent, root, { target: knob, signalName: "gestureCommitted" })
        knob.forceActiveFocus()
        keyClick(Qt.Key_Right)
        verify(knob.value > -27)
        verify(knob.value < -26)
        compare(commits.count, 1)
    }

    function test_silence_is_explicit_and_keyboard_unmutes() {
        const fader = createTemporaryObject(faderComponent, root, { x: 150, y: 10 })
        verify(fader !== null)
        compare(fader.normalizedValue, 0)
        verify(fader.silent)
        const commits = createTemporaryObject(spyComponent, root, { target: fader, signalName: "gestureCommitted" })
        fader.forceActiveFocus()
        keyClick(Qt.Key_Right)
        verify(!fader.silent)
        verify(fader.value > -60)
        compare(commits.count, 1)
    }

    Component { id: spyComponent; SignalSpy {} }
}
