// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The sample editor of examples/waveform-live: WaveformEditor over the
// worker-thread peak pyramid of AudioWaveformSource. Buttons and keys
// request the same domain operations; Main.qml applies them to session.edits.
Tk.Panel {
    id: panel
    required property var page
    required property var session
    required property var peaks
    required property var transport
    readonly property real editorWidth: editor.width

    title: qsTr("Waveform · exact peaks")
    iconName: "audio-lines"
    collapsible: false
    padding: Tk.Theme.space.sm

    // One mark per half second of the visible range, from the real columns.
    function rulerMarks() {
        const columns = panel.peaks.columns
        if (!columns.length) return []
        const first = Number(columns[0].startSample)
        const last = Number(columns[columns.length - 1].endSample)
        const step = panel.page.rate / 2
        const marks = []
        for (let s = Math.ceil(first / step) * step; s < last && marks.length < 64; s += step)
            marks.push({ fraction: (s - first) / Math.max(1, last - first), label: (s / panel.page.rate).toFixed(1) + " s" })
        return marks
    }
    // Committed spectral edits, drawn as regions so both views agree.
    function spectralRegions() {
        const out = []
        const list = panel.page.editList
        for (let i = 0; i < list.length; ++i)
            if (list[i].operation === "spectralGain")
                out.push({ id: "band-" + i, startSample: String(list[i].startSample), endSample: String(list[i].endSample),
                           label: qsTr("band %1–%2 Hz").arg(Math.round(list[i].lowHz)).arg(Math.round(list[i].highHz)) })
        return out
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Flex {
            width: parent.width
            gap: Tk.Theme.space.xs
            align: Tk.Flex.Center
            Tk.NumberField {
                objectName: "liveGainField"
                small: true
                width: Tk.Theme.size.fieldWidth
                label: qsTr("Gain")
                suffix: " dB"
                decimals: 1
                stepSize: 1
                from: editor.minimumGainDb
                to: editor.maximumGainDb
                value: editor.gainDb
                tooltip: qsTr("Gain for the selection (Up/Down in the editor)")
                onValueModified: function(value) { editor.setGainDb(value) }
            }
            Tk.Button { objectName: "liveGain"; small: true; text: qsTr("Apply gain"); tooltip: qsTr("G: apply the gain to the selection"); onClicked: editor.requestEdit("gain") }
            Tk.ToolSeparator {}
            Tk.Button { objectName: "liveFadeIn"; small: true; text: qsTr("Fade in"); tooltip: qsTr("F: fade the selection in"); onClicked: editor.requestEdit("fadeIn") }
            Tk.Button { objectName: "liveFadeOut"; small: true; text: qsTr("Fade out"); tooltip: qsTr("Shift+F: fade the selection out"); onClicked: editor.requestEdit("fadeOut") }
            Tk.Segmented {
                small: true
                model: [{ text: qsTr("Linear"), value: "linear" }, { text: qsTr("Equal power"), value: "equalPower" }]
                currentIndex: editor.fadeShape === "equalPower" ? 1 : 0
                tooltip: qsTr("E: fade curve")
                onActivated: function(index) { editor.fadeShape = index === 1 ? "equalPower" : "linear" }
            }
            Tk.ToolSeparator {}
            Tk.Button { objectName: "liveSilence"; small: true; text: qsTr("Silence"); tooltip: qsTr("S: silence the selection"); onClicked: editor.requestEdit("silence") }
            Tk.Button { objectName: "liveDelete"; small: true; text: qsTr("Delete"); tooltip: qsTr("Delete: remove the selection"); onClicked: editor.requestEdit("delete") }
            Tk.Button { objectName: "liveCrop"; small: true; text: qsTr("Crop"); tooltip: qsTr("T: keep only the selection"); onClicked: editor.requestEdit("crop") }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button {
                small: true
                iconName: "maximize"
                text: qsTr("Show all")
                tooltip: qsTr("Show the whole source (wheel zooms, arrows scroll)")
                onClicked: panel.peaks.showAll()
            }
        }

        Item {
            width: parent.width
            height: parent.height
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0

            Tk.WaveformEditor {
                id: editor
                objectName: "liveWaveformEditor"
                anchors.fill: parent
                sourceId: panel.page.resourceId
                channelId: "all"
                columns: panel.peaks.columns
                gainDb: -6
                selectionStartSample: panel.page.selectionStart
                selectionEndSample: panel.page.selectionEnd
                regions: panel.spectralRegions()
                rulerMarks: panel.rulerMarks()
                statusText: qsTr("Peaks %1 · %2 columns · revision %3")
                            .arg(panel.peaks.state).arg(panel.peaks.columns.length).arg(panel.peaks.revision)
                tooltip: qsTr("Room take waveform: drag to select, then edit")
                onSelectionCommitted: function(id, source, channel, start, end) {
                    panel.page.selectionStart = start
                    panel.page.selectionEnd = end
                    panel.page.selectionChannel = channel
                    panel.page.status = qsTr("Selected %1–%2 (samples %3–%4)")
                        .arg(panel.page.seconds(start)).arg(panel.page.seconds(end)).arg(start).arg(end)
                }
                onEditRequested: function(id, source, channel, operation, start, end, parameters) {
                    const ok = panel.page.applyEdit(Object.assign({ operation: operation, channel: channel,
                                                                    startSample: start, endSample: end }, parameters))
                    if (ok && operation === "delete") {
                        panel.page.selectionEnd = start
                    } else if (ok && operation === "crop") {
                        panel.page.selectionStart = "0"
                        panel.page.selectionEnd = panel.session.edits.frames(panel.page.resourceId)
                    }
                }
                onZoomRequested: function(anchor, factor) { panel.peaks.zoom(anchor, factor) }
                onScrollRequested: function(fraction) { panel.peaks.scroll(fraction) }
            }
            // Engine playhead: the device thread's transport sample (the clip
            // starts at 0 and the engine runs at the source rate).
            Rectangle {
                objectName: "livePlayhead"
                readonly property real fraction: editor.fractionForSample(panel.transport.positionSample)
                visible: panel.peaks.columns.length > 0 && fraction > 0 && fraction < 1
                x: Math.round(fraction * editor.width)
                width: Tk.Theme.size.border
                height: editor.height
                color: Tk.Theme.color.warning
            }
        }
    }
}
