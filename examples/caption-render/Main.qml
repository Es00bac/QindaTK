// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 1440
    height: 900
    property string frameTimeMs: "3500"
    property string statusText: qsTr("Burned captions use committed cue intervals at [in, out) millisecond boundaries.")

    function renderAtCurrentTime() {
        const result = renderer.renderFixture(track, frameTimeMs)
        if (result.ok) statusText = qsTr("Frame %1 ms · active cues: %2").arg(frameTimeMs).arg(result.activeCueIds.join(", ") || qsTr("none"))
        else statusText = result.error
    }

    Tk.CaptionTrackModel {
        id: track
        sourceId: "demo-clip"
        trackId: "captions-main"
        language: "en-US"
        Component.onCompleted: {
            track.setDisplayRange("0", "8000")
            track.loadJson(JSON.stringify({version: 1, trackId: "captions-main", language: "en-US", cues: [
                {id: "bridge-en", inMs: "1000", outMs: "5000", text: "Beyond the old stone bridge", language: "en-US", styleId: "default", lane: 0},
                {id: "river-fr", inMs: "3000", outMs: "7000", text: "La lumière revient sur la rivière", language: "fr-FR", styleId: "emphasis", lane: 1}]}))
            track.selectCue("bridge-en")
        }
    }
    Tk.CaptionFrameRenderer { id: renderer }
    Connections {
        target: track
        function onChanged() { root.renderAtCurrentTime() }
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Caption frame renderer") }
                Tk.Caption { text: qsTr("CaptionTrackModel · exact millisecond timing · real burned pixels"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Segmented {
                model: [
                    {text: qsTr("Before"), value: "999"},
                    {text: qsTr("English"), value: "2000"},
                    {text: qsTr("Overlap"), value: "3500"},
                    {text: qsTr("French"), value: "6000"},
                    {text: qsTr("After"), value: "7000"}]
                currentIndex: ["999", "2000", "3500", "6000", "7000"].indexOf(root.frameTimeMs)
                tooltip: qsTr("Render at exact timeline milliseconds")
                onActivated: function(index) { root.frameTimeMs = String(currentValue); root.renderAtCurrentTime() }
            }
            Tk.Button {
                objectName: "saveRenderedCaptionFrame"
                text: qsTr("Save PNG")
                iconName: "download"
                tooltip: qsTr("Atomically save the current rendered frame to the app data directory")
                onClicked: root.statusText = renderer.saveRenderedFrame(renderer.defaultOutputPath)
                    ? qsTr("Rendered PNG saved to %1").arg(renderer.defaultOutputPath) : renderer.error
            }
        }
        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1fr) minmax(0, 1.3fr)"
            columnGap: Tk.Theme.space.sm
            Tk.Panel {
                title: qsTr("Cue track")
                iconName: "captions"
                Tk.CaptionEditor {
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
                    warningText: track.error
                    tooltip: qsTr("Edit caption cues. Text remains available separately from video pixels.")
                    onCueSelectionRequested: function(sourceId, trackId, cueId) { track.selectCue(cueId) }
                    onCueEditPreviewRequested: function(requestId, sourceId, trackId, cueId, operation, inTime, outTime, text, styleId) {
                        track.previewCueEdit(requestId, cueId, operation, inTime, outTime, text, styleId)
                    }
                    onCueEditCommitRequested: function(requestId, sourceId, trackId, cueId, operation, inTime, outTime, text, styleId) {
                        if (track.commitCueEdit(requestId, cueId, operation, inTime, outTime, text, styleId)) root.renderAtCurrentTime()
                    }
                    onCueEditCanceled: function(requestId, sourceId, trackId, cueId) { track.cancelCueEdit(requestId, cueId) }
                }
            }
            Tk.Panel {
                title: qsTr("Rendered frame")
                iconName: "film"
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Item {
                        width: parent.width
                        height: parent.height
                        Tk.Flex.grow: 1
                        Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas; radius: Tk.Theme.radius.sm }
                        Image {
                            objectName: "burnedCaptionPixels"
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            source: renderer.imageDataUrl
                            cache: false
                            Accessible.role: Accessible.Graphic
                            Accessible.name: qsTr("Video frame with captions burned into the image")
                            Accessible.description: qsTr("The rendered pixels are separate from the accessible caption text in the cue editor.")
                        }
                    }
                    Tk.Caption { objectName: "captionRenderStatus"; text: root.statusText; color: renderer.error.length ? Tk.Theme.color.warning : Tk.Theme.color.textMuted; wrapMode: Text.Wrap }
                    Tk.Mono { text: qsTr("%1 × %2 · %3 ms · %4 active cue(s)").arg(960).arg(540).arg(renderer.lastTimeMs || root.frameTimeMs).arg(renderer.activeCueIds.length); color: Tk.Theme.color.textMuted }
                }
            }
        }
    }
    Component.onCompleted: root.renderAtCurrentTime()
}
