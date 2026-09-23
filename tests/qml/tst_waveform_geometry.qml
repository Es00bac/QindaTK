// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "WaveformGeometry"
    when: windowShown
    visible: true
    width: 320
    height: 180

    Component {
        id: waveformComponent
        Tk.Waveform {
            width: 240
            height: Tk.Theme.size.waveformHeight
        }
    }

    function test_empty_has_only_centre_line() {
        const wave = createTemporaryObject(waveformComponent, root)
        verify(wave !== null)
        verify(wave.empty)

        const shape = findChild(wave, "waveformShape")
        const centre = findChild(wave, "waveformCentreLine")
        verify(shape !== null && centre !== null)
        verify(!shape.visible)
        verify(centre.visible)
        compare(shape.width, wave.width)
        compare(shape.height, wave.height)
    }

    function test_single_peak_has_finite_visible_geometry() {
        const wave = createTemporaryObject(waveformComponent, root)
        wave.peaks = [0.75]
        waitForRendering(wave)

        const shape = findChild(wave, "waveformShape")
        verify(shape !== null)
        verify(!wave.empty)
        verify(shape.visible)
        compare(shape.width, wave.width)
        compare(shape.height, wave.height)
        verify(isFinite(shape.x) && isFinite(shape.y))
        verify(isFinite(shape.width) && isFinite(shape.height))
    }

    function test_many_peaks_keep_existing_geometry() {
        const wave = createTemporaryObject(waveformComponent, root)
        wave.peaks = [0.1, 0.4, 0.9, 0.3, 0.05]
        waitForRendering(wave)

        const shape = findChild(wave, "waveformShape")
        verify(shape !== null)
        verify(!wave.empty)
        verify(shape.visible)
        compare(shape.width, wave.width)
        compare(shape.height, wave.height)
        verify(isFinite(shape.x) && isFinite(shape.y))
    }
}
