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

    property var options: null
    property var rebased: null
    property var offsetArgs: null
    property var applied: null
    property var rangeRequest: null
    property var rangeAccept: null

    Tk.TrackingPanel {
        id: jobs
        y: 440
        width: 620
        height: 460
        selectedTrackId: "trk-2"
        startFrame: "3"
        endFrame: "9"
        currentFrame: "4"
        maskSourceFrame: "3"
        targets: [{maskId: "mask-a", label: "Matte"}, {maskId: "mask-b", label: "Sky"}]
        tracks: [{trackId: "trk-2", label: "Corner", referenceFrame: 3, radius: 6, searchRadius: 12,
            motion: "translation", state: "running", previewPoints: [],
            points: [{frame: 3, x: 10, y: 12, confidence: 1, state: "tracked"},
                     {frame: 4, x: 12, y: 13, confidence: 0.9, state: "tracked"}]}]
        onOptionsChangeRequested: function(id, radius, search, motion) { root.options = [id, radius, search, motion] }
        onRebaseRequested: function(id, frame) { root.rebased = [id, frame] }
        onOffsetRequested: function(id, dx, dy, first, last) { root.offsetArgs = [id, dx, dy, first, last] }
        onApplyToMaskRequested: function(id, mask, source, first, last) { root.applied = [id, mask, source, first, last] }
    }
    Tk.RotoscopeEditor {
        id: range
        x: 640
        width: 640
        height: 520
        maskId: "mask-a"
        currentFrame: "2"
        sourceFrame: "0"
        trackingTrackId: "trk-2"
        propagationFrom: "0"
        propagationTo: "4"
        masks: [{maskId: "mask-a", label: "Matte", feather: 0, keyframes: [
            {frame: 0, source: "user", points: [{pointId: "a", x: 0.1, y: 0.1}, {pointId: "b", x: 0.3, y: 0.1}, {pointId: "c", x: 0.1, y: 0.3}]},
            {frame: 2, source: "generated", trackIds: ["trk-2"], points: [{pointId: "a", x: 0.2, y: 0.1}, {pointId: "b", x: 0.4, y: 0.1}, {pointId: "c", x: 0.2, y: 0.3}]}]}]
        onPropagateRangeRequested: function(id, source, first, last, track) { root.rangeRequest = [id, source, first, last, track] }
        onAcceptRangeRequested: function(id, source, first, last, tracks) { root.rangeAccept = [id, source, first, last, tracks] }
    }

    function findChild(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = findChild(item.children[i], name)
            if (found) return found
        }
        if (item.contentItem && item.contentItem !== item) return findChild(item.contentItem, name)
        return null
    }

    function test_job_progress_options_and_apply_targets() {
        // A running job shows its progress and locks the track's edits.
        jobs.analysisProgress = 0.4
        const bar = findChild(jobs, "trackingProgress")
        verify(bar && bar.visible)
        compare(bar.value, 0.4)
        compare(findChild(jobs, "trackingProgressText").text, "Analyzing 40%")
        jobs.loadingFrames = true
        jobs.frameLoadProgress = 0.25
        compare(findChild(jobs, "trackingProgressText").text, "Decoding 25%")
        verify(!jobs.setOptions(4, 8, "similarity"))
        verify(!jobs.rebase())
        verify(!jobs.applyToMask())
        jobs.loadingFrames = false

        jobs.tracks = [{trackId: "trk-2", label: "Corner", referenceFrame: 3, radius: 6, searchRadius: 12,
            motion: "translation", state: "ready", previewPoints: [],
            points: [{frame: 3, x: 10, y: 12, confidence: 1, state: "tracked"},
                     {frame: 4, x: 12, y: 13, confidence: 0.9, state: "tracked"},
                     {frame: 5, x: 12, y: 13, confidence: 0.2, state: "lost"}]}]
        verify(!findChild(jobs, "trackingProgressRow").visible)
        compare(findChild(jobs, "trackingPatchRadius").value, 6)
        compare(findChild(jobs, "trackingSearchRadius").value, 12)
        verify(!jobs.setOptions(1, 8, "similarity"))
        verify(!jobs.setOptions(4, 8, "affine"))
        verify(jobs.setOptions(4, 8, "similarity"))
        compare(options, ["trk-2", 4, 8, "similarity"])
        verify(jobs.rebase())
        compare(rebased, ["trk-2", "4"])
        jobs.currentFrame = "5"
        verify(!jobs.rebase()) // lost points never become references
        verify(!jobs.offset(0, 0))
        verify(jobs.offset(1.5, -2))
        compare(offsetArgs, ["trk-2", 1.5, -2, "3", "9"])
        verify(!jobs.applyToMask()) // no target yet
        jobs.targetMaskId = "mask-b"
        verify(jobs.applyToMask())
        compare(applied, ["trk-2", "mask-b", "3", "3", "9"])

        // Range propagation previews generated keys, then asks the host to commit.
        compare(findChild(range, "rotoKeySource").text, "Generated key")
        range.currentFrame = "0"
        compare(range.keySource, "user")
        range.currentFrame = "1"
        compare(findChild(range, "rotoKeySource").text, "No key")
        verify(range.requestRangePropagation())
        compare(rangeRequest, ["mask-a", "0", "0", "4", "trk-2"])
        range.propagationPreview = {ok: true, state: "preview", sourceFrame: 0, fromFrame: 0, toFrame: 4,
            trackIds: ["trk-2"], keys: [{frame: 1, points: [{pointId: "a", x: 0.15, y: 0.1},
                {pointId: "b", x: 0.35, y: 0.1}, {pointId: "c", x: 0.15, y: 0.3}]}]}
        verify(range.rangePreview)
        compare(range.previewPath.length, 3)
        compare(findChild(range, "rotoStatus").text, "GENERATED PREVIEW · 1 KEYS")
        verify(range.acceptPreview())
        compare(rangeAccept, ["mask-a", "0", "0", "4", ["trk-2"]])
    }

    Tk.TrackingRotoModel { id: listModel }

    function test_range_preview_from_cpp_lists() {
        // Model results reach QML as C++ lists, not JS arrays.
        const maskId = listModel.addMask("Listed")
        verify(listModel.setMaskKeyframe(maskId, 1, [{pointId: "a", x: 0.1, y: 0.1},
            {pointId: "b", x: 0.3, y: 0.1}, {pointId: "c", x: 0.1, y: 0.3}]))
        const keys = listModel.masks[0].keyframes
        verify(!Array.isArray(keys))
        range.currentFrame = "1"
        range.propagationPreview = {ok: true, state: "preview", sourceFrame: 0, fromFrame: 0, toFrame: 4,
                                    trackIds: ["trk-2"], keys: keys}
        verify(range.rangePreview)
        compare(range.previewPath.length, 3)
        compare(findChild(range, "rotoStatus").text, "GENERATED PREVIEW · 1 KEYS")
        verify(findChild(range, "rotoAcceptPreview").enabled)
        range.propagationPreview = ({})
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
