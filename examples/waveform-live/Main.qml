// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-063 on the real stack (docs/audio-editing.md). A generated recording is
// imported into an AudioSession that plays through an AudioEngine on the
// silent built-in null device. AudioWaveformSource and AudioSpectrogramSource
// build peaks and spectra on worker threads; the editors only report
// gestures, and every edit goes through session.edits, which recompiles the
// engine graph and invalidates both analyses. Nothing here is a snapshot.
// The older examples/waveform-spectrogram keeps the fixture design review.
Rectangle {
    id: root
    width: 1280
    height: 800
    color: Tk.Theme.color.canvas

    property string resourceId: ""
    property string status: qsTr("Opening the null device…")
    property string selectionStart: "48000"
    property string selectionEnd: "72000"
    property string selectionChannel: "all"
    property string spectralStart: ""
    property string spectralEnd: ""
    property real spectralLow: 0
    property real spectralHigh: 0
    property var spectralMask: []
    property real bandGainDb: -150
    property string pendingPreview: ""
    property var editList: []
    property int revision: 0
    readonly property int rate: peaks.sampleRate > 0 ? peaks.sampleRate : 48000
    readonly property string frames: root.revision >= 0 && root.resourceId.length
        ? session.edits.frames(root.resourceId) : "0"

    function seconds(sample) { return (Number(sample) / root.rate).toFixed(3) + " s" }
    function describe(edit) {
        const range = qsTr("%1–%2").arg(root.seconds(edit.startSample)).arg(root.seconds(edit.endSample))
        const channel = Number(edit.channel) >= 0 ? qsTr(" · ch %1").arg(edit.channel) : ""
        if (edit.operation === "gain") return qsTr("Gain %1 dB · %2%3").arg(Number(edit.gainDb).toFixed(1)).arg(range).arg(channel)
        if (edit.operation === "fadeIn" || edit.operation === "fadeOut")
            return qsTr("%1 (%2) · %3%4").arg(edit.operation === "fadeIn" ? qsTr("Fade in") : qsTr("Fade out"))
                   .arg(edit.shape || "linear").arg(range).arg(channel)
        if (edit.operation === "spectralGain")
            return qsTr("Band %1 dB · %2–%3 Hz · %4%5").arg(Number(edit.gainDb).toFixed(0))
                   .arg(Math.round(edit.lowHz)).arg(Math.round(edit.highHz)).arg(range)
                   .arg(edit.mask ? qsTr(" · mask") : "")
        const names = { silence: qsTr("Silence"), "delete": qsTr("Delete"), crop: qsTr("Crop") }
        return qsTr("%1 · %2%3").arg(names[edit.operation] || edit.operation).arg(range).arg(channel)
    }
    function applyEdit(edit) {
        if (!root.resourceId.length) return false
        const result = session.edits.apply(root.resourceId, edit)
        root.status = result.ok
            ? qsTr("%1 · revision %2 · %3 frames").arg(root.describe(edit)).arg(result.revision).arg(result.frames)
            : qsTr("Refused: %1").arg(result.error)
        return result.ok
    }
    function requestSpectral(id, start, end, low, high, parameters) {
        if (!root.resourceId.length) return false
        const edit = Object.assign({ operation: "spectralGain", channel: -1, startSample: start, endSample: end,
                                     lowHz: low, highHz: high, gainDb: root.bandGainDb, fftSize: 2048 },
                                   parameters || {})
        const answer = session.edits.preview(id, root.resourceId, edit)
        if (answer.ok === false) {
            root.status = qsTr("Spectral preview refused: %1").arg(answer.error)
            return false
        }
        root.pendingPreview = id
        root.status = qsTr("Computing %1 on the thread pool…").arg(root.describe(edit))
        return true
    }
    function commitSpectral() {
        const result = session.edits.commitPreview(root.pendingPreview)
        root.status = result.ok ? qsTr("Spectral edit committed · revision %1").arg(result.revision)
                                : qsTr("Commit refused: %1").arg(result.error)
        root.pendingPreview = ""
    }
    function cancelSpectral() {
        session.edits.cancelPreview(root.pendingPreview)
        root.pendingPreview = ""
    }
    // Loops the edited source. A delete or crop can end the loop before the
    // playhead, which would otherwise play on past it to the timeline end.
    function loopWholeSource() {
        if (!root.resourceId.length || !engine.transport.canLoop) return
        const end = session.edits.frames(root.resourceId)
        engine.transport.requestLoop(true, "0", end)
        if (Number(engine.transport.positionSample) >= Number(end))
            engine.transport.requestSeek("0")
    }

    Tk.AudioEngine {
        id: engine
        onConfigurationApplied: function(requestId, state, active, message) {
            root.status = message
            if (requestId === "startup" && active) {
                root.loopWholeSource()
                engine.transport.requestPlay()
            }
        }
    }
    Tk.AudioSession {
        id: session
        engine: engine
    }
    Connections {
        target: session.edits
        function onSourceChanged(resourceId, revision) {
            root.revision = Number(revision)
            root.editList = session.edits.edits(root.resourceId)
            root.loopWholeSource()
        }
        function onPreviewReady(requestId, summary) {
            if (requestId === "startup-band") {
                root.commitSpectral()
                root.status = qsTr("Ready · a generated take with four real edits · loop playing on the null device")
            } else {
                root.status = qsTr("Preview ready · heard and drawn now, not yet in the history. Commit or cancel.")
            }
        }
        function onPreviewFailed(requestId, error) {
            root.pendingPreview = ""
            root.status = qsTr("Spectral preview failed: %1").arg(error)
        }
        function onPreviewCanceled(requestId) {
            root.pendingPreview = ""
            root.status = qsTr("Spectral preview canceled; the committed samples are back")
        }
    }

    Tk.AudioWaveformSource {
        id: peaks
        session: session
        resourceId: root.resourceId
        columnCount: Math.max(1, Math.round(waveformPanel.editorWidth))
    }
    Tk.AudioSpectrogramSource {
        id: spectrum
        session: session
        resourceId: root.resourceId
        startSample: peaks.startSample
        endSample: peaks.endSample
        fftSize: 2048
        rows: 64
        columnCount: 180
        minimumHz: 60
        maximumHz: 8000
        frequencyScale: "log"
    }

    Component.onCompleted: {
        engine.applyConfiguration("startup", { deviceId: "builtin:null", sampleRate: 48000, bufferSize: 256,
                                               inputLayout: "None", outputLayout: "Stereo" })
        const track = session.addTrack(qsTr("Room take"))
        // Demo recording: 4 s at 48 kHz, five phase-continuous notes with a
        // level swell (AudioSession.generateTestTake), imported like any WAV.
        const path = session.generateTestTake("", { sampleRate: 48000, segments: [
            { frames: 30000, frequency: 220.0, amplitude: 0.55, pulse: 2 },
            { frames: 42000, frequency: 440.0, amplitude: 0.75, pulse: 4 },
            { frames: 30000, frequency: 3520.0, amplitude: 0.35, pulse: 8 },
            { frames: 48000, frequency: 1320.0, amplitude: 0.65, pulse: 3 },
            { frames: 42000, frequency: 660.0, amplitude: 0.8, pulse: 5 }] })
        const imported = session.importMedia(path, track, "0")
        if (!imported.resourceId) {
            root.status = qsTr("The recording could not be imported: %1").arg(session.lastError)
            return
        }
        root.resourceId = imported.resourceId
        // Real edits in the history, so the views open on edited samples.
        root.applyEdit({ operation: "fadeIn", channel: -1, startSample: "0", endSample: "12000", shape: "equalPower" })
        root.applyEdit({ operation: "gain", channel: -1, startSample: "42000", endSample: "66000", gainDb: -9 })
        root.applyEdit({ operation: "fadeOut", channel: -1, startSample: "174000", endSample: "192000", shape: "linear" })
        root.spectralStart = "114000"
        root.spectralEnd = "138000"
        root.spectralLow = 1000
        root.spectralHigh = 1700
        root.requestSpectral("startup-band", "114000", "138000", 1000, 1700, {})
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.md

        Tk.Flex {
            width: parent.width
            gap: Tk.Theme.space.md
            align: Tk.Flex.Center
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Room take · live waveform and spectral editing") }
                Tk.Caption {
                    objectName: "liveSourceSummary"
                    text: qsTr("AudioSession · %1 · %2 Hz mono · %3 frames (%4) · revision %5")
                          .arg(root.resourceId.length ? root.resourceId.slice(0, 13) : qsTr("no source"))
                          .arg(root.rate).arg(root.frames).arg(root.seconds(root.frames)).arg(root.revision)
                    color: Tk.Theme.color.textMuted
                }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.TransportBar {
                objectName: "liveTransport"
                transport: engine.transport
            }
        }

        Tk.Grid {
            width: parent.width
            height: parent.height
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1fr) " + (Tk.Theme.size.panelMinWidth + Tk.Theme.size.fieldWidth)
            rows: "minmax(0, 1fr) minmax(0, 1fr)"
            columnGap: Tk.Theme.space.md
            rowGap: Tk.Theme.space.md

            WaveformPanel {
                id: waveformPanel
                page: root
                session: session
                peaks: peaks
                transport: engine.transport
            }
            SidePanel {
                Tk.Grid.rowSpan: 2
                page: root
                session: session
                engine: engine
                peaks: peaks
                spectrum: spectrum
            }
            SpectrumPanel {
                page: root
                session: session
                peaks: peaks
                spectrum: spectrum
            }
        }

        Tk.Caption {
            objectName: "liveStatus"
            width: parent.width
            text: root.status
            color: Tk.Theme.color.textMuted
            elide: Text.ElideRight
        }
    }
}
