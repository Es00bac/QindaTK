// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Host-owned fixture data. The peaks and spectral bins are representative
// snapshots for interaction/design review; no analyzer or audio engine runs here.
Item {
    id: page
    width: 1100
    height: 700
    property string status: qsTr("Fixture snapshots · no audio engine attached")
    property string selectionStart: "9007199254741290"
    property string selectionEnd: "9007199254741690"
    property string selectionChannel: "all"
    property string spectralRange: "No spectral selection"
    // A committed lasso mask around the middle harmonic, in sample/Hz coordinates.
    property var spectralMask: [[
        { sample: "9007199254741290", hz: 900 }, { sample: "9007199254741690", hz: 700 },
        { sample: "9007199254741890", hz: 1500 }, { sample: "9007199254741490", hz: 2600 }
    ]]

    readonly property var waveformColumns: [
        { minimum: -0.08, maximum: 0.06, startSample: "9007199254740990", endSample: "9007199254741090" },
        { minimum: -0.28, maximum: 0.31, startSample: "9007199254741090", endSample: "9007199254741190" },
        { minimum: -0.52, maximum: 0.47, startSample: "9007199254741190", endSample: "9007199254741290" },
        { minimum: -0.82, maximum: 0.91, startSample: "9007199254741290", endSample: "9007199254741390" },
        { minimum: -0.45, maximum: 0.62, startSample: "9007199254741390", endSample: "9007199254741490" },
        { minimum: -0.20, maximum: 0.18, startSample: "9007199254741490", endSample: "9007199254741590" },
        { minimum: -0.63, maximum: 0.70, startSample: "9007199254741590", endSample: "9007199254741690" },
        { minimum: -0.31, maximum: 0.38, startSample: "9007199254741690", endSample: "9007199254741790" },
        { minimum: -0.12, maximum: 0.10, startSample: "9007199254741790", endSample: "9007199254741890" },
        { minimum: -0.75, maximum: 0.84, startSample: "9007199254741890", endSample: "9007199254741990" },
        { minimum: -0.30, maximum: 0.26, startSample: "9007199254741990", endSample: "9007199254742090" },
        { minimum: -0.09, maximum: 0.11, startSample: "9007199254742090", endSample: "9007199254742190" }
    ]
    readonly property var rightColumns: page.waveformColumns.map(c => ({
        minimum: c.minimum * 0.6, maximum: c.maximum * 0.7, startSample: c.startSample, endSample: c.endSample }))
    readonly property var spectralTiles: {
        const cells = []
        for (let x = 0; x < 24; ++x) {
            for (let band = 0; band < 10; ++band) {
                const hzLow = 30 * Math.pow(18000 / 30, band / 10)
                const hzHigh = 30 * Math.pow(18000 / 30, (band + 1) / 10)
                const harmonic = (band === 2 || band === 5 || band === 7) ? 0.74 : 0.12
                const pulse = ((x + band * 3) % 7) < 3 ? 0.18 : 0.55
                cells.push({ x0: x / 24, x1: (x + 1) / 24,
                             lowHz: hzLow, highHz: hzHigh,
                             intensity: Math.min(1, harmonic * pulse + (x % 5) * 0.035) })
            }
        }
        return cells
    }
    readonly property var boundaries: {
        const list = []
        for (let i = 0; i < waveformColumns.length; ++i)
            list.push(waveformColumns[i].startSample)
        list.push(waveformColumns[waveformColumns.length - 1].endSample)
        return list
    }

    Tk.Box {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
        Tk.Flex {
            direction: Tk.Flex.Column
            padding: Tk.Theme.space.md
            gap: Tk.Theme.space.md
            Tk.Overline { text: qsTr("AUDIO SOURCE · ROOM TAKE 04") }
            Tk.Caption {
                text: qsTr("Host sample range: %1 – %2 · stereo source · snapshot revision 18")
                      .arg(page.boundaries[0]).arg(page.boundaries[page.boundaries.length - 1])
                color: Tk.Theme.color.textMuted
            }
            Item {
                width: parent.width
                implicitHeight: 250
                Tk.Flex.shrink: 0
                Tk.WaveformEditor {
                id: waveform
                objectName: "fixtureWaveformEditor"
                anchors.fill: parent
                sourceId: "asset:room-take-04"
                columns: page.waveformColumns
                channelLayout: [{ id: "0", label: "L" }, { id: "1", label: "R" }]
                channelColumns: [page.waveformColumns, page.rightColumns]
                selectionChannelId: page.selectionChannel
                gainDb: -3
                regions: [{ id: "impact-04", startSample: "9007199254741290",
                            endSample: "9007199254741690", label: "Impact transient" }]
                selectionStartSample: page.selectionStart
                selectionEndSample: page.selectionEnd
                rulerMarks: [{ fraction: 0, label: "sample 9007199254740990" },
                             { fraction: 0.5, label: "midpoint" },
                             { fraction: 0.92, label: "end" }]
                tooltip: qsTr("Room take waveform, both channels")
                onSelectionPreviewRequested: function(id, source, channel, start, end) {
                    page.status = qsTr("Preview %1 · %2:%3–%4").arg(id).arg(channel).arg(start).arg(end)
                }
                onSelectionCommitted: function(id, source, channel, start, end) {
                    page.selectionStart = start
                    page.selectionEnd = end
                    page.selectionChannel = channel
                    page.status = qsTr("Host acknowledged selection %1–%2 (%3)").arg(start).arg(end).arg(id)
                }
                onGestureCanceled: function(id) { page.status = qsTr("Selection canceled · %1").arg(id) }
                onEditRequested: function(id, source, channel, operation, start, end, parameters) {
                    page.status = qsTr("Edit request %1 · channel %2 · %3–%4 · %5")
                        .arg(operation).arg(channel).arg(start).arg(end).arg(JSON.stringify(parameters))
                }
                onZoomRequested: function(anchor, factor) { page.status = qsTr("Host zoom request at sample %1 ×%2").arg(anchor).arg(factor) }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.md
                Tk.Overline { text: qsTr("SPECTRAL SNAPSHOT") }
                Tk.Caption { text: qsTr("FFT 4096 · Hann · hop 1024 · power normalized") }
            }
            Item {
                width: parent.width
                implicitHeight: 300
                Tk.Flex.shrink: 0
                Tk.SpectrogramView {
                id: spectrogram
                objectName: "fixtureSpectrogramView"
                anchors.fill: parent
                sourceId: "asset:room-take-04"
                channelId: "channel:left"
                tiles: page.spectralTiles
                sampleBoundaries: page.boundaries
                minimumHz: 30
                maximumHz: 18000
                frequencyScale: "log"
                fftSize: 4096
                fftWindow: "Hann"
                hopSize: 1024
                fftNormalization: "power normalized"
                statusText: qsTr("Mock provider payload · replace with bounded analysis tiles")
                maskPolygons: page.spectralMask
                tooltip: qsTr("Spectrogram, logarithmic frequency axis")
                onSelectionPreviewRequested: function(id, source, channel, start, end, low, high) {
                    page.spectralRange = qsTr("Preview %1 · samples %2–%3 · %4–%5 Hz")
                        .arg(id).arg(start).arg(end).arg(Math.round(low)).arg(Math.round(high))
                    page.status = page.spectralRange
                }
                onSelectionCommitted: function(id, source, channel, start, end, low, high) {
                    page.spectralRange = qsTr("Committed request %1 · %2–%3 · %4–%5 Hz")
                        .arg(id).arg(start).arg(end).arg(Math.round(low)).arg(Math.round(high))
                    page.status = page.spectralRange
                }
                onGestureCanceled: function(id) { page.status = qsTr("Spectral selection canceled · %1").arg(id) }
                onMaskCommitted: function(id, source, channel, polygons, start, end, low, high) {
                    page.spectralMask = polygons
                    page.status = qsTr("Mask %1 · %2 polygon(s) · %3–%4").arg(id).arg(polygons.length).arg(start).arg(end)
                }
                onProcessRequested: function(id, source, channel, operation, start, end, low, high, parameters) {
                    page.status = qsTr("Process request %1 · %2–%3 · %4")
                        .arg(operation).arg(start).arg(end).arg(parameters.mask ? qsTr("mask") : qsTr("region"))
                }
                }
            }
            Tk.Caption { objectName: "fixtureStatus"; text: page.status; color: Tk.Theme.color.textMuted }
        }
    }
}
