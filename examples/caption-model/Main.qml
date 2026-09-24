// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 1080
    height: 760
    color: Tk.Theme.color.canvas
    property string exportText: ""
    property string hostStatus: qsTr("Caption model ready")
    property string vttFixture: "WEBVTT\n\ncue-opening\n00:00:01.200 --> 00:00:04.300\nA quiet signal crosses the harbor.\n\ncue-response\n00:00:03.900 --> 00:00:06.100\nWe have one more stop.\n"

    Tk.CaptionTrackModel {
        id: track
        sourceId: "source-harbor-film"
        trackId: "captions-en"
        language: "en-US"
        Component.onCompleted: {
            setDisplayRange("0", "10000")
            importWebVtt(root.vttFixture, "en-US")
        }
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Label { text: qsTr("Caption track workbench"); font.pixelSize: Tk.Theme.font.title; Accessible.role: Accessible.Heading }
            Tk.Badge { text: qsTr("VTT · SRT · JSON"); variant: "accent" }
        }
        Tk.Label {
            text: qsTr("The editor sends cue requests to CaptionTrackModel. Times cross the boundary as exact decimal milliseconds; same-lane overlaps are rejected.")
            muted: true; wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Button { text: qsTr("Load VTT fixture"); tooltip: qsTr("Import the bundled WebVTT sample"); onClicked: { track.importWebVtt(root.vttFixture, "en-US"); root.hostStatus = track.acknowledgement.message } }
            Tk.Button { text: qsTr("Export WebVTT"); onClicked: { root.exportText = track.exportWebVtt(); root.hostStatus = qsTr("WebVTT exported from the current host snapshot") } }
            Tk.Button { text: qsTr("Export SRT"); onClicked: { root.exportText = track.exportSrt(); root.hostStatus = qsTr("SRT exported; language and style remain in JSON metadata") } }
            Tk.Button { text: qsTr("Save JSON"); variant: "accent"; tooltip: track.storagePath; onClicked: { const ok = track.saveToFile(); root.hostStatus = ok ? qsTr("Saved to %1").arg(track.storagePath) : track.error } }
            Tk.Button { text: qsTr("Reload JSON"); onClicked: { const ok = track.loadFromFile(); root.hostStatus = ok ? qsTr("Track reloaded from JSON storage") : track.error } }
            Tk.Button { text: qsTr("Undo"); variant: "outline"; onClicked: { root.hostStatus = track.undo() ? track.acknowledgement.message : qsTr("No edit to undo") } }
            Tk.Button {
                text: qsTr("Split")
                tooltip: qsTr("Split the selected cue at its midpoint")
                onClicked: {
                    const middle = Math.floor((Number(track.selectedCueInMs) + Number(track.selectedCueOutMs)) / 2)
                    track.splitCue(track.selectedCueId, String(middle))
                    root.hostStatus = track.acknowledgement.message
                }
            }
            Tk.Button {
                text: qsTr("Merge next")
                tooltip: qsTr("Merge the selected cue with the next cue")
                onClicked: {
                    const cues = track.editorCues
                    let next = ""
                    for (let i = 0; i + 1 < cues.length; ++i)
                        if (String(cues[i].id) === track.selectedCueId) next = String(cues[i + 1].id)
                    track.mergeCues(track.selectedCueId, next)
                    root.hostStatus = track.acknowledgement.message
                }
            }
            Tk.Button {
                text: qsTr("Shift +100 ms")
                tooltip: qsTr("Move the selected cue 100 ms later")
                onClicked: { track.shiftCues([track.selectedCueId], "100"); root.hostStatus = track.acknowledgement.message }
            }
        }
        Item {
            objectName: "captionModelEditorFrame"
            Tk.Flex.grow: 1
            Tk.CaptionEditor {
                id: editor
                anchors.fill: parent
                sourceId: track.sourceId
                trackId: track.trackId
                cues: track.editorCues
                timeMarks: track.timeMarks
                moveTargets: track.moveTargets
                styles: track.styles
                selectedCueId: track.selectedCueId
                selectedCueText: track.selectedCueText
                selectedCueStyleId: track.selectedCueStyleId
                selectedCueLanguage: track.selectedCueLanguage
                editAcknowledgement: track.acknowledgement
                warningText: track.warningText
                tooltip: qsTr("Caption track editor bound to a persistent host model")
                onCueSelectionRequested: function(source, trackId, cueId) { track.selectCue(cueId) }
                onCueEditPreviewRequested: function(requestId, source, trackId, cueId, operation, inTime, outTime, text, styleId) {
                    track.previewCueEdit(requestId, cueId, operation, inTime, outTime, text, styleId)
                }
                onCueEditCommitRequested: function(requestId, source, trackId, cueId, operation, inTime, outTime, text, styleId) {
                    const accepted = track.commitCueEdit(requestId, cueId, operation, inTime, outTime, text, styleId)
                    root.hostStatus = accepted ? track.acknowledgement.message : track.error
                }
                onCueEditCanceled: function(requestId, source, trackId, cueId) { track.cancelCueEdit(requestId, cueId); root.hostStatus = track.acknowledgement.message }
                onNavigationRequested: function(source, cueId, direction) {
                    const cues = track.editorCues
                    let index = -1
                    for (let i = 0; i < cues.length; ++i) if (String(cues[i].id) === cueId) index = i
                    const next = index + (direction === "next" ? 1 : -1)
                    if (next >= 0 && next < cues.length) track.selectCue(String(cues[next].id))
                }
                onExportRequested: function(source, trackId, formatId) {
                    root.exportText = formatId === "srt" ? track.exportSrt() : track.exportWebVtt()
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Label { text: root.hostStatus; color: track.error.length ? Tk.Theme.color.warning : Tk.Theme.color.textMuted; Tk.Flex.grow: 1 }
            Tk.Mono { text: qsTr("Selected exact ms: %1–%2").arg(track.selectedCueInMs).arg(track.selectedCueOutMs); color: Tk.Theme.color.textMuted }
            Tk.Label { text: qsTr("JSON path: %1").arg(track.storagePath); color: Tk.Theme.color.textMuted; elide: Text.ElideMiddle }
        }
        Tk.TextArea {
            objectName: "captionExportPreview"
            text: root.exportText
            readOnly: true
            mono: true
            rows: 4
            tooltip: qsTr("Export preview; Save JSON persists canonical cue records")
        }
    }
}
