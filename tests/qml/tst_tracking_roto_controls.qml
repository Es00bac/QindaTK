// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    visible: true
    name: "TrackingRotoControls"
    when: windowShown
    property var start: null
    property var correction: null
    property var edit: null
    property var preview: null
    property var propagation: null
    property var acceptedPreview: null
    property var cancelledPreview: null
    property var accepted: ""

    Tk.TrackingPanel {
        id: tracking
        width: 620
        height: 420
        selectedTrackId: "trk-1"
        startFrame: "10"
        endFrame: "40"
        tracks: [{trackId: "trk-1", label: "Camera", referenceFrame: "10", state: "lost",
            points: [{frame: "22", x: 0.4, y: 0.5, confidence: 0.1, state: "lost", source: "flow"}]}]
        onStartRequested: function(trackId, first, last, occlusion) { root.start = [trackId, first, last, occlusion] }
        onCorrectionRequested: function(trackId, frame, x, y) { root.correction = [trackId, frame, x, y] }
        onAcceptRequested: function(trackId) { root.accepted = trackId }
    }
    Tk.RotoscopeEditor {
        id: roto
        width: 640
        height: 520
        maskId: "mask-a"
        currentFrame: "22"
        sourceFrame: "10"
        trackingTrackId: "trk-1"
        propagationPreview: ({ok: true, state: "preview", points: [
            {pointId: "left", x: 0.25, y: 0.5}, {pointId: "top", x: 0.55, y: 0.2},
            {pointId: "right", x: 0.85, y: 0.5}, {pointId: "bottom", x: 0.55, y: 0.8}]})
        masks: [{maskId: "mask-a", label: "Matte", feather: 6,
            keyframes: [{frame: "22", points: [
                {pointId: "left", x: 0.2, y: 0.5}, {pointId: "top", x: 0.5, y: 0.2},
                {pointId: "right", x: 0.8, y: 0.5}, {pointId: "bottom", x: 0.5, y: 0.8}]}]}]
        onKeyframeEditRequested: function(maskId, frame, points) { root.edit = [maskId, frame, points] }
        onPointEditPreviewRequested: function(maskId, frame, points) { root.preview = [maskId, frame, points] }
        onPropagateRequested: function(maskId, sourceFrame, targetFrame, trackId) { root.propagation = [maskId, sourceFrame, targetFrame, trackId] }
        onAcceptPreviewRequested: function(maskId, frame, points) { root.acceptedPreview = [maskId, frame, points] }
        onCancelPreviewRequested: function(maskId, frame) { root.cancelledPreview = [maskId, frame] }
    }

    function test_host_snapshots_and_stable_edit_identity() {
        verify(tracking.track.state === "lost")
        compare(tracking.features[0].frame, "22")
        tracking.occlusionFrames = 3
        verify(tracking.rerun())
        compare(start, ["trk-1", "10", "40", 3])
        verify(tracking.correctPoint("22", 103.75, 42.5))
        compare(correction[0], "trk-1")
        compare(correction[1], "22")
        compare(correction[2], 103.75)
        compare(correction[3], 42.5)
        verify(!tracking.accept())

        roto._dragging = true
        verify(roto.previewPoint("right", 0.7, 0.6))
        compare(preview[0], "mask-a")
        verify(roto.finishDrag())
        compare(edit[0], "mask-a")
        compare(edit[1], "22")
        compare(edit[2].length, 4)
        compare(edit[2][2].pointId, "right")
        compare(edit[2][2].x, 0.7)
        verify(roto.removePoint("left"))
        compare(edit[2].length, 3)
        roto.masks = [{maskId: "mask-a", label: "Matte", feather: 6,
            keyframes: [{frame: "22", points: [
                {pointId: "top", x: 0.5, y: 0.2}, {pointId: "right", x: 0.7, y: 0.6},
                {pointId: "bottom", x: 0.5, y: 0.8}]}]}]
        verify(!roto.removePoint("top"))
        verify(roto.requestPropagation())
        compare(propagation, ["mask-a", "10", "22", "trk-1"])
        compare(roto.previewVisible, true)
        verify(roto.acceptPreview())
        compare(acceptedPreview[0], "mask-a")
        compare(acceptedPreview[1], "22")
        verify(roto.cancelPreview())
        compare(cancelledPreview, ["mask-a", "22"])

        tracking.tracks = [{trackId: "trk-1", label: "Camera", referenceFrame: 0,
            state: "ready", previewPoints: [],
            points: [{frame: 0, x: 12, y: 15, confidence: 1, state: "tracked"}]}]
        compare(tracking.features.length, 1)
        verify(!tracking.accept())
        tracking.tracks = [{trackId: "trk-1", label: "Camera", referenceFrame: 0,
            state: "ready", points: [{frame: 0, x: 12, y: 15, confidence: 1}],
            previewPoints: [{frame: 1, x: 18, y: 15, confidence: 0.9}]}]
        compare(tracking.features[0].frame, 1)
        verify(tracking.accept())
        compare(accepted, "trk-1")
    }
}
