// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The spectral editor of examples/waveform-live: SpectrogramView over the
// worker-thread STFT tiles of AudioSpectrogramSource, sharing the waveform's
// view range. A rectangle, lasso or painted mask becomes a spectralGain
// preview on the thread pool; Commit records it as one undo step.
Tk.Panel {
    id: panel
    required property var page
    required property var session
    required property var peaks
    required property var spectrum
    readonly property bool previewReady: panel.page.pendingPreview.length > 0
        && panel.session.edits.previewState === "ready"

    title: qsTr("Spectrogram · STFT tiles")
    iconName: "chart-column"
    collapsible: false
    padding: Tk.Theme.space.sm

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Flex {
            width: parent.width
            gap: Tk.Theme.space.xs
            align: Tk.Flex.Center
            Tk.Segmented {
                objectName: "liveSpectralMode"
                small: true
                model: [{ text: qsTr("Rectangle"), value: "rectangle" }, { text: qsTr("Lasso"), value: "lasso" },
                        { text: qsTr("Paint"), value: "paint" }]
                currentIndex: ["rectangle", "lasso", "paint"].indexOf(spectrogram.selectionMode)
                tooltip: qsTr("Selection tool (R, L, B)")
                onActivated: function(index) { spectrogram.selectionMode = ["rectangle", "lasso", "paint"][index] }
            }
            Tk.NumberField {
                objectName: "liveBandGainField"
                small: true
                width: Tk.Theme.size.fieldWidth
                label: qsTr("Band")
                suffix: " dB"
                from: -150
                to: 24
                stepSize: 3
                value: panel.page.bandGainDb
                tooltip: qsTr("Gain for the selected band; −150 dB removes it")
                onValueModified: function(value) { panel.page.bandGainDb = value }
            }
            Tk.Button {
                objectName: "liveSpectralPreview"
                small: true
                text: qsTr("Preview")
                busy: panel.session.edits.previewState === "running"
                enabled: spectrogram.hasSelection || spectrogram.hasMask
                tooltip: qsTr("Delete: process the selected band or mask on the thread pool")
                onClicked: spectrogram.requestProcess("spectralGain")
            }
            Tk.Button {
                objectName: "liveSpectralCommit"
                small: true
                variant: "primary"
                text: qsTr("Commit")
                enabled: panel.previewReady
                tooltip: qsTr("Record the previewed band gain as one undo step")
                onClicked: panel.page.commitSpectral()
            }
            Tk.Button {
                small: true
                text: qsTr("Cancel")
                enabled: panel.page.pendingPreview.length > 0
                tooltip: qsTr("Drop the preview and restore the committed samples")
                onClicked: panel.page.cancelSpectral()
            }
            Tk.Button {
                small: true
                text: qsTr("Clear mask")
                enabled: spectrogram.hasMask
                tooltip: qsTr("Forget the drawn mask")
                onClicked: panel.page.spectralMask = []
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption {
                objectName: "liveSpectrumInfo"
                text: qsTr("%1 · %2 tiles cached · hop %3")
                      .arg(panel.spectrum.state).arg(panel.spectrum.cachedTiles).arg(panel.spectrum.effectiveHop)
                color: Tk.Theme.color.textMuted
            }
        }

        Tk.SpectrogramView {
            id: spectrogram
            objectName: "liveSpectrogramView"
            width: parent.width
            height: parent.height
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            sourceId: panel.page.resourceId
            channelId: "all"
            tiles: panel.spectrum.tiles
            sampleBoundaries: panel.spectrum.sampleBoundaries
            minimumHz: panel.spectrum.minimumHz
            maximumHz: panel.spectrum.resolvedMaximumHz
            frequencyScale: panel.spectrum.frequencyScale
            fftSize: panel.spectrum.fftSize
            hopSize: panel.spectrum.effectiveHop
            fftWindow: panel.spectrum.window
            fftNormalization: panel.spectrum.normalization
            selectionStartSample: panel.page.spectralStart
            selectionEndSample: panel.page.spectralEnd
            selectionLowHz: panel.page.spectralLow
            selectionHighHz: panel.page.spectralHigh
            maskPolygons: panel.page.spectralMask
            statusText: qsTr("Spectrum %1 · %2 tiles · revision %3")
                        .arg(panel.spectrum.state).arg(panel.spectrum.tiles.length).arg(panel.spectrum.revision)
            tooltip: qsTr("Room take spectrogram, logarithmic frequency axis")
            onSelectionCommitted: function(id, source, channel, start, end, low, high) {
                panel.page.spectralMask = []
                panel.page.spectralStart = start
                panel.page.spectralEnd = end
                panel.page.spectralLow = low
                panel.page.spectralHigh = high
                panel.page.status = qsTr("Band %1–%2 Hz over %3–%4 selected")
                    .arg(Math.round(low)).arg(Math.round(high))
                    .arg(panel.page.seconds(start)).arg(panel.page.seconds(end))
            }
            onMaskCommitted: function(id, source, channel, polygons, start, end, low, high) {
                panel.page.spectralMask = polygons
                panel.page.status = qsTr("Mask of %1 polygon(s) over %2–%3 Hz")
                    .arg(polygons.length).arg(Math.round(low)).arg(Math.round(high))
            }
            onProcessRequested: function(id, source, channel, operation, start, end, low, high, parameters) {
                panel.page.requestSpectral(id, start, end, low, high, parameters)
            }
            onZoomRequested: function(anchor, hz, factor) { panel.peaks.zoom(anchor, factor) }
            onScrollRequested: function(timeFraction, frequencyFraction) {
                if (timeFraction !== 0) panel.peaks.scroll(timeFraction)
            }
        }
    }
}
