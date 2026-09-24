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
    height: 320

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
    Component { id: sessionComponent; Tk.AudioSession {} }
    Component { id: bindingComponent; Tk.AudioParameterBinding {} }
    Component { id: boundFaderComponent; Tk.Fader { width: 100; height: 240 } }
    Component { id: boundKnobComponent; Tk.Knob { width: 110 } }
    Component { id: meterModelComponent; Tk.AudioMeterModel {} }
    Component { id: meterViewComponent; Tk.AudioMeter { width: 240 } }
    Component { id: loudnessModelComponent; Tk.AudioLoudnessModel {} }
    Component { id: historyViewComponent; Tk.LoudnessHistory { width: 360; height: 160 } }
    Component { id: spyComponent; SignalSpy {} }

    // A session track gain (dB, -60..+12, static 0 dB) bound for a control.
    function makeBinding() {
        const session = createTemporaryObject(sessionComponent, root)
        const id = session.ensureGainParameter(session.addTrack("Bus"))
        return createTemporaryObject(bindingComponent, root, { session: session, parameterId: id })
    }
    function collect(item, name, out) {
        if (!item) return out
        if (item.objectName === name) out.push(item)
        for (let i = 0; i < item.children.length; ++i) collect(item.children[i], name, out)
        return out
    }
    function shown(items) { return items.filter(function(item) { return item.visible }) }
    // True when no two items overlap along the axis (in root coordinates).
    function disjoint(items, horizontal) {
        for (let i = 0; i < items.length; ++i) {
            for (let j = i + 1; j < items.length; ++j) {
                const a = items[i].mapToItem(root, 0, 0), b = items[j].mapToItem(root, 0, 0)
                const apart = horizontal ? a.x + items[i].width <= b.x || b.x + items[j].width <= a.x
                                         : a.y + items[i].height <= b.y || b.y + items[j].height <= a.y
                if (!apart) return false
            }
        }
        return true
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

    function test_bound_fader_gestures_are_engine_transactions() {
        const binding = makeBinding()
        verify(binding.valid)
        const history = binding.session.automation
        const fader = createTemporaryObject(boundFaderComponent, root, { x: 150, y: 10, binding: binding })
        verify(fader !== null)
        compare(fader.from, -60)
        compare(fader.to, 12)
        compare(fader.unit, "dB")
        compare(fader.value, 0)
        verify(fader.silenceMark)
        fuzzyCompare(fader.normalizedValue, 0.75, 1e-9) // unity at the mapper's unity position

        // One arrow press: one transaction on the engine parameter.
        fader.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(history.historyCount, 1)
        verify(binding.staticValue < 0)
        compare(fader.value, binding.value)
        const before = binding.staticValue

        // A drag canceled with Escape restores the level and stores nothing.
        const thumb = findChild(fader, "faderThumb")
        verify(thumb !== null)
        mousePress(thumb, thumb.width / 2, thumb.height / 2)
        mouseMove(thumb, thumb.width / 2, thumb.height / 2 + 40, -1, Qt.LeftButton)
        verify(binding.value < before)
        verify(binding.touched)
        keyClick(Qt.Key_Escape)
        mouseRelease(thumb, thumb.width / 2, thumb.height / 2 + 40)
        compare(binding.value, before)
        compare(binding.staticValue, before)
        verify(!binding.touched)
        compare(history.historyCount, 1)

        // A double-click resets to unity as ONE transaction.
        mouseDoubleClickSequence(thumb, thumb.width / 2, thumb.height / 2)
        compare(binding.staticValue, 0)
        compare(history.historyCount, 2)
        verify(history.undo())
        compare(binding.staticValue, before)
        compare(fader.value, before)
    }

    function test_late_bound_controls_follow_the_mapping() {
        // The parameter arrives after the controls exist, as in the example.
        const session = createTemporaryObject(sessionComponent, root)
        const binding = createTemporaryObject(bindingComponent, root, { session: session })
        const fader = createTemporaryObject(boundFaderComponent, root, {
            x: 150, y: 10, binding: binding, scaleMarks: [-60, -30, -18, -12, -6, 0, 6, 12] })
        const knob = createTemporaryObject(boundKnobComponent, root, { x: 10, y: 10, binding: binding })
        verify(fader !== null && knob !== null)
        binding.parameterId = session.ensureGainParameter(session.addTrack("Late"))
        verify(binding.valid)
        fuzzyCompare(fader.normalizedValue, 0.75, 1e-9)
        fuzzyCompare(knob.normalizedValue, 0.75, 1e-9)
        // The thumb sits on the unity line at 0 dB, well above the bottom.
        const thumb = findChild(fader, "faderThumb")
        const unity = findChild(fader, "faderUnityMark")
        tryVerify(function() { return thumb.height > 0 && fader.travel > thumb.height })
        fuzzyCompare(thumb.y + thumb.height / 2, unity.y + unity.height / 2, 0.5)
        const track = findChild(fader, "faderTrack")
        verify(thumb.y + thumb.height / 2 < track.y + track.height / 2)
        // Scale labels never overlap; crowded ones are dropped, ends and unity stay.
        const labels = shown(collect(fader, "faderScaleLabel", []))
        verify(labels.length >= 3 && labels.length < 8)
        verify(disjoint(labels, false))
        const texts = labels.map(function(label) { return label.text })
        verify(texts.indexOf("0") >= 0 && texts.indexOf("12") >= 0 && texts.indexOf("−∞") >= 0)
    }

    function test_meter_scale_clears_the_reset_button() {
        const model = createTemporaryObject(meterModelComponent, root)
        verify(model.submitInterleaved([1.0, 0.5], 2, 10)) // channel 1 clips: Reset clip shows
        const view = createTemporaryObject(meterViewComponent, root, { x: 10, y: 10, meter: model })
        verify(view !== null)
        const reset = findChild(view, "meterClipReset")
        // Once laid out, every scale label ends above Reset clip (the labels
        // spilled onto it when the scale row reserved no height).
        const labels = function() { return shown(collect(view, "meterScaleLabel", [])) }
        tryVerify(function() {
            const top = reset.mapToItem(view, 0, 0).y
            return reset.visible && labels().length >= 2
                && labels().every(function(label) { return label.mapToItem(view, 0, label.height).y <= top })
                && view.implicitHeight >= reset.mapToItem(view, 0, reset.height).y
        })
        // Labels clear each other and the unit; the bars start where the scale starts.
        verify(disjoint(labels().concat([findChild(view, "meterScaleUnit")]), true))
        fuzzyCompare(findChild(view, "channelTrack").mapToItem(view, 0, 0).x, view.trackX, 0.5)
    }

    function test_bound_knob_wheel_burst_is_one_transaction() {
        const binding = makeBinding()
        const history = binding.session.automation
        const knob = createTemporaryObject(boundKnobComponent, root, { x: 10, y: 10, binding: binding })
        verify(knob !== null)
        compare(knob.unit, " dB")
        const commits = createTemporaryObject(spyComponent, root, { target: knob, signalName: "gestureCommitted" })
        const dial = findChild(knob, "knobDial")
        for (let i = 0; i < 3; ++i) mouseWheel(dial, dial.width / 2, dial.height / 2, 0, -120)
        verify(knob.gestureOpen)
        verify(binding.touched)
        verify(binding.value < 0) // heard while the burst is open
        compare(history.historyCount, 0)
        tryCompare(commits, "count", 1, 3000) // Tk.Theme.motion.gestureIdle ends the burst
        compare(history.historyCount, 1)
        verify(!binding.touched)
        fuzzyCompare(binding.staticValue, knob.value, 1e-9)
    }

    function test_meter_view_projects_a_measured_snapshot() {
        const model = createTemporaryObject(meterModelComponent, root)
        verify(model.submitInterleaved([0.5, 0.25, -0.5, -0.25], 2, 10))
        const view = createTemporaryObject(meterViewComponent, root, { x: 10, y: 10, meter: model })
        verify(view !== null)
        compare(view.channelCount, 2)
        compare(view.metric, "sample-peak+rms")
        verify(view.summary.indexOf("peak -6.0 dBFS") >= 0)
        verify(view.summary.indexOf("peak -12.0 dBFS") >= 0)
        verify(findChild(view, "meterScale") !== null)
        verify(!view.hasClip)
        verify(model.submitInterleaved([1.0, 0.0], 2, 10))
        verify(view.hasClip)
        const requests = createTemporaryObject(spyComponent, root, { target: view, signalName: "clipResetRequested" })
        findChild(view, "meterClipReset").clicked()
        compare(requests.count, 1)
        verify(!view.hasClip) // the provider cleared its latches
    }

    function test_history_view_shows_bs1770_readings() {
        const model = createTemporaryObject(loudnessModelComponent, root)
        model.historyMetric = "momentary"
        const samples = []
        for (let i = 0; i < 48000; ++i) samples.push(0.5 * Math.sin(2 * Math.PI * 1000 * i / 48000))
        verify(model.submitInterleaved(samples, 1, 48000))
        const view = createTemporaryObject(historyViewComponent, root, { x: 10, y: 10, loudness: model })
        verify(view !== null)
        compare(view.metricId, "bs1770.momentary")
        compare(view.unit, "LUFS")
        compare(view.channelLayout, "Mono")
        compare(view.sampleCount, 7) // one reading per 100 ms from 0.4 s
        fuzzyCompare(model.momentaryLufs, -9.024, 0.01) // mono -6.02 dBFS 1 kHz sine
        compare(view.readout.indexOf("M -9.0"), 0)
        verify(view.summary.indexOf("ITU-R BS.1770-4") >= 0)
        const exported = createTemporaryObject(spyComponent, root, { target: view, signalName: "reportExported" })
        view.exportReport()
        compare(exported.count, 1)
        compare(exported.signalArguments[0][0].schema, "qindatk.loudness-report/1")
        view.resetHistory()
        compare(view.sampleCount, 0)
        verify(isNaN(model.integratedLufs))
    }
}
