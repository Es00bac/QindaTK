// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 1120
    height: 720
    color: Tk.Theme.color.bg
    property string trackId: ""
    property string maskId: ""
    property var maskPreview: ({})
    property bool initialAnalysisAccepted: false

    Tk.TrackingRotoModel { id: model }

    Component.onCompleted: {
        if (!model.loadFrame(0, Qt.resolvedUrl("frame-0.svg"))
                || !model.loadFrame(1, Qt.resolvedUrl("frame-1.svg"))
                || !model.loadFrame(2, Qt.resolvedUrl("frame-2.svg"))) {
            console.error("Could not load tracking frames:", model.lastError)
            return
        }
        trackId = model.addTrack("Moving feature", 0, 180, 112, 5, 16)
        maskId = model.addMask("Subject matte")
        model.setMaskKeyframe(maskId, 0, [
            {pointId: "top", x: 0.375, y: 0.20},
            {pointId: "right", x: 0.55, y: 0.45},
            {pointId: "bottom", x: 0.42, y: 0.82},
            {pointId: "left", x: 0.25, y: 0.47}
        ])
        model.setMaskFeather(maskId, 4)
        model.startAnalysis(trackId, 0, 2)
    }

    Connections {
        target: model
        function onChanged() {
            const track = model.tracks.find(function(item) { return item.trackId === root.trackId })
            if (!track || track.state !== "ready" || root.initialAnalysisAccepted) return
            root.initialAnalysisAccepted = true
            if (model.acceptAnalysis(root.trackId))
                root.maskPreview = model.previewTrackedMask(root.maskId, 0, 2, root.trackId)
        }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg
        Tk.Overline { title: qsTr("Live tracking and rotoscoping"); wide: true }
        Tk.Flex {
            width: parent.width
            Tk.Flex.grow: 1
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.TrackingPanel {
                objectName: "liveTrackingPanel"
                Tk.Flex.grow: 1
                tracks: model.tracks
                selectedTrackId: root.trackId
                startFrame: "0"
                endFrame: "2"
                currentFrame: "2"
                onTrackSelected: function(id) { root.trackId = id }
                onStartRequested: function(id, first, last, occlusion) {
                    root.maskPreview = ({})
                    model.startAnalysis(id, Number(first), Number(last), occlusion)
                }
                onCancelRequested: model.cancelAnalysis()
                onAcceptRequested: function(id) {
                    if (model.acceptAnalysis(id))
                        root.maskPreview = model.previewTrackedMask(root.maskId, 0, 2, id)
                }
                onCorrectionRequested: function(id, frame, x, y) {
                    model.correctPoint(id, Number(frame), x, y)
                }
            }
            Tk.RotoscopeEditor {
                objectName: "liveRotoscopeEditor"
                Tk.Flex.grow: 2
                masks: model.masks
                maskId: root.maskId
                currentFrame: "2"
                sourceFrame: "0"
                sourceFrameUrl: Qt.resolvedUrl("frame-2.svg")
                sourceAspectRatio: 16 / 9
                trackingTrackId: root.trackId
                propagationPreview: root.maskPreview
                onMaskSelected: function(id) { root.maskId = id }
                onPropagateRequested: function(id, source, target, track) {
                    root.maskPreview = model.previewTrackedMask(id, Number(source), Number(target), track)
                }
                onAcceptPreviewRequested: function(id, frame, points) {
                    if (model.setMaskKeyframe(id, Number(frame), points)) root.maskPreview = ({})
                }
                onCancelPreviewRequested: root.maskPreview = ({})
                onKeyframeEditRequested: function(id, frame, points) {
                    model.setMaskKeyframe(id, Number(frame), points)
                }
                onKeyframeRemoveRequested: function(id, frame) {
                    model.removeMaskKeyframe(id, Number(frame))
                }
                onFeatherChangeRequested: function(id, feather) {
                    model.setMaskFeather(id, feather)
                }
            }
        }
        Tk.Caption {
            text: qsTr("Real patch tracking across three local frames · generated mask preview requires acceptance")
        }
    }
}
