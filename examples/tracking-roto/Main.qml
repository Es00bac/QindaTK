// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 1120
    height: 720
    color: Tk.Theme.color.bg
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg
        Tk.Overline { title: qsTr("Tracking and rotoscoping"); wide: true }
        Tk.Flex {
            width: parent.width
            height: parent.height - Tk.Theme.size.control * 2
            Tk.Flex.grow: 1
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.TrackingPanel {
                objectName: "trackingPanelFixture"
                Tk.Flex.grow: 1
                selectedTrackId: "track-subject"
                startFrame: "100"
                endFrame: "148"
                tracks: [{trackId: "track-subject", label: "Subject / center", referenceFrame: "100", state: "lost",
                    points: [
                        {frame: "100", x: 470, y: 220, confidence: 0.98, state: "corrected", source: "manual"},
                        {frame: "112", x: 540, y: 250, confidence: 0.91, state: "tracked", source: "flow"},
                        {frame: "124", x: 610, y: 285, confidence: 0.24, state: "lost", source: "flow"}
                    ]}]
                onStartRequested: console.log("start", trackId, startFrame, endFrame)
                onAcceptRequested: console.log("accept", trackId)
            }
            Tk.RotoscopeEditor {
                objectName: "rotoscopeEditorFixture"
                Tk.Flex.grow: 2
                maskId: "mask-subject"
                currentFrame: "124"
                sourceFrame: "100"
                sourceFrameUrl: Qt.resolvedUrl("source.svg")
                sourceAspectRatio: 16 / 9
                trackingTrackId: "track-subject"
                propagationPreview: ({ok: true, state: "preview", points: [
                    {pointId: "p-top", x: 0.60, y: 0.18}, {pointId: "p-right", x: 0.81, y: 0.42},
                    {pointId: "p-bottom", x: 0.64, y: 0.88}, {pointId: "p-left", x: 0.43, y: 0.46}]})
                masks: [{maskId: "mask-subject", label: "Subject matte", feather: 8,
                    keyframes: [
                        {frame: "100", points: [
                            {pointId: "p-top", x: 0.52, y: 0.16}, {pointId: "p-right", x: 0.72, y: 0.39},
                            {pointId: "p-bottom", x: 0.56, y: 0.86}, {pointId: "p-left", x: 0.36, y: 0.44}]},
                        {frame: "124", points: [
                            {pointId: "p-top", x: 0.56, y: 0.18}, {pointId: "p-right", x: 0.77, y: 0.42},
                            {pointId: "p-bottom", x: 0.60, y: 0.88}, {pointId: "p-left", x: 0.39, y: 0.46}]}
                    ]}]
                onKeyframeEditRequested: function(maskId, frame, points) { console.log("edit mask", maskId, frame, points.length) }
                onPropagateRequested: function(maskId, sourceFrame, targetFrame, trackId) { console.log("preview propagation", maskId, sourceFrame, targetFrame, trackId) }
                onFeatherChangeRequested: function(maskId, value) { console.log("feather", maskId, value) }
            }
        }
        Tk.Caption { text: qsTr("Host-owned tracking snapshots · stable frame and point IDs · lost tracking stays visible") }
    }
}
