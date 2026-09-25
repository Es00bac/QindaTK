// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtCore
import QindaTK as Tk

// The real backend of the sequence-edit-live example (docs/sequence-edits.md):
// five generated test signals imported into a MediaCatalog, one TimelineModel
// with linked picture (V1) and sound (A1), a SequenceEditor for trims and
// transitions, a MulticamModel over the same tracks, two MediaPlayers on the
// timeline (program and cut monitors) and a render queue for export.
//
// AGENT-NOTE: the sequence is loaded as content (not built by edits), so the
// history starts empty and every undo step is one the viewer made. The
// multicam program clips sit at their sync positions, which is how
// MulticamModel recovers the program angle from each clip's resource.
Item {
    id: live
    visible: false

    readonly property string mediaFolder: StandardPaths.writableLocation(StandardPaths.TempLocation)
                                          + "/qindatk-sequence-edit-live"
    readonly property real second: 705600000
    readonly property real frame: 28224000 // 25 fps
    readonly property string videoTrack: "00000000-0000-4000-8000-000000000001"
    readonly property string audioTrack: "00000000-0000-4000-8000-000000000002"
    // Shots and camera angles; barRotation tells the colour bars apart on sight.
    readonly property var mediaPlan: [
        {role: "harbor", label: qsTr("Harbor"), frames: 125, rotation: 5},
        {role: "interview", label: qsTr("Interview"), frames: 125, rotation: 3},
        {role: "wide", label: qsTr("Cam A · Wide"), frames: 250, rotation: 0},
        {role: "close", label: qsTr("Cam B · Close"), frames: 250, rotation: 2},
        {role: "gimbal", label: qsTr("Cam C · Gimbal"), frames: 250, rotation: 4}
    ]
    readonly property var angleOffsets: ({wide: 0, close: 3, gimbal: 5}) // frames of sync offset
    property var resources: ({})
    property var pendingJobs: ({})
    property bool ready: false
    property bool queueOpen: false
    property bool programParked: false
    property bool cutParked: false
    property string status: qsTr("Writing five test signals…")

    readonly property alias resolver: mediaResolver
    readonly property alias assets: frameAssets
    readonly property alias catalog: mediaCatalog
    readonly property alias timeMap: sequenceTimeMap
    readonly property alias timeline: sequence
    readonly property alias programPlayer: programPlayer
    readonly property alias cutPlayer: cutPlayer
    readonly property alias editor: sequenceEditor
    readonly property alias multicam: multicamModel
    readonly property alias peaks: timelinePeaks
    readonly property alias renderQueue: renderQueue

    Tk.ResourceResolver { id: mediaResolver }
    Tk.AssetProvider { id: frameAssets; resolver: mediaResolver }
    Tk.MediaCatalog {
        id: mediaCatalog
        resolver: mediaResolver
        proxyDirectory: live.mediaFolder + "/proxies"
        onJobFinished: function(jobId, kind, assetId, ok, error) { live.signalWritten(jobId, kind, ok, error) }
    }
    Tk.TimeMap { id: sequenceTimeMap; sampleRate: 48000; Component.onCompleted: setFrameRate(25, 1) }
    Tk.TimelineModel { id: sequence; timeMap: sequenceTimeMap }
    Tk.TimelineMediaProvider { id: timelinePeaks; decodedBackend: true; resolver: mediaResolver }
    Tk.MediaPlayer {
        id: programPlayer
        resolver: mediaResolver
        catalog: mediaCatalog
        timeline: sequence
        frameWidth: 320
        frameHeight: 180
        onStateChanged: live.park()
    }
    // The cut monitor: the TransitionEditor's Preview loops the window here.
    Tk.MediaPlayer {
        id: cutPlayer
        resolver: mediaResolver
        catalog: mediaCatalog
        timeline: sequence
        frameWidth: 320
        frameHeight: 180
        onStateChanged: live.park()
    }
    Tk.SequenceEditor {
        id: sequenceEditor
        timeline: sequence
        resolver: mediaResolver
        player: cutPlayer
    }
    Tk.MulticamModel {
        id: multicamModel
        timeline: sequence
        resolver: mediaResolver
        player: programPlayer
        trackId: live.videoTrack
        audioTrackId: live.audioTrack
        syncStartTicks: "0"
        audioFollowsVideo: true
    }
    Tk.RenderQueueModel { id: renderQueue }

    function ticks(seconds) { return String(Math.round(seconds * live.second)) }
    function frames(n) { return String(Math.round(n * live.frame)) }
    function uid(n) { return "00000000-0000-4000-8000-" + String(n).padStart(12, "0") }

    Component.onCompleted: {
        const jobs = {}
        for (let i = 0; i < live.mediaPlan.length; ++i) {
            const plan = live.mediaPlan[i]
            const path = live.mediaFolder + "/" + plan.role + ".mkv"
            const job = mediaCatalog.generateTestSignal(path, ({frames: plan.frames, popInterval: 25, popPhase: 12,
                                                                barRotation: plan.rotation}))
            if (job.length) jobs[job] = {path: path, plan: plan}
            else live.status = mediaCatalog.lastError
        }
        live.pendingJobs = jobs
    }

    // Once the sequence first renders, the program monitor parks inside the
    // multicam program and the cut monitor on the dissolve's cut.
    function park() {
        if (!live.ready) return
        if (!live.programParked && programPlayer.state === "ready") {
            live.programParked = true
            programPlayer.seekTicks(live.frames(150))
        }
        if (!live.cutParked && cutPlayer.state === "ready") {
            live.cutParked = true
            cutPlayer.seekTicks(live.frames(75))
        }
    }

    function signalWritten(jobId, kind, ok, error) {
        if (kind !== "testSignal" || !live.pendingJobs[jobId]) return
        if (!ok) {
            live.status = qsTr("Test signal failed: %1").arg(error)
            return
        }
        const job = live.pendingJobs[jobId]
        const assetId = mediaCatalog.importMedia(job.path, "", job.plan.label)
        if (!assetId.length) {
            live.status = mediaCatalog.lastError
            return
        }
        const next = Object.assign({}, live.resources)
        next[job.plan.role] = mediaCatalog.asset(assetId).resourceId
        live.resources = next
        const count = Object.keys(next).length
        live.status = qsTr("Imported %1 of %2 test signals").arg(count).arg(live.mediaPlan.length)
        if (count === live.mediaPlan.length) live.buildSequence()
    }

    function clip(n, track, kind, role, start, duration, offset, sourceFrames, link, label, transition) {
        const record = {id: live.uid(n), trackId: track, kind: kind, start: live.frames(start),
                        duration: live.frames(duration), sourceOffset: live.frames(offset),
                        sourceDuration: live.frames(sourceFrames), resourceId: live.resources[role],
                        linkId: live.uid(link), label: label}
        if (transition) record.transition = transition
        return record
    }
    function pair(n, role, start, duration, offset, sourceFrames, label, transition) {
        return [live.clip(10 + n, live.videoTrack, "video", role, start, duration, offset, sourceFrames, 90 + n,
                          label, transition),
                live.clip(20 + n, live.audioTrack, "audio", role, start, duration, offset, sourceFrames, 90 + n,
                          label + qsTr(" sound"), null)]
    }
    // Harbor 0-3 s dissolves into Interview 3-5 s; the multicam program runs
    // 5-9 s: Wide, then Gimbal from 7 s, each at its sync position.
    function buildSequence() {
        const dissolve = {type: "dissolve", beforeCut: live.frames(13), afterCut: live.frames(12),
                          alignment: "center", parameters: {curve: "linear"}}
        const clips = [].concat(live.pair(1, "harbor", 0, 75, 25, 125, qsTr("Harbor")),
                                live.pair(2, "interview", 75, 50, 50, 125, qsTr("Interview"), dissolve),
                                live.pair(3, "wide", 125, 50, 125 + live.angleOffsets.wide, 250, qsTr("Cam A · Wide")),
                                live.pair(4, "gimbal", 175, 50, 175 + live.angleOffsets.gimbal, 250,
                                          qsTr("Cam C · Gimbal")))
        const loaded = sequence.loadContent({
            timebase: {numerator: String(live.second), denominator: "1"},
            tracks: [{id: live.videoTrack, name: "V1", kind: "video"}, {id: live.audioTrack, name: "A1", kind: "audio"}],
            clips: clips,
            markers: [{id: live.uid(301), position: live.frames(75), label: qsTr("Dissolve")},
                      {id: live.uid(302), position: live.frames(125), label: qsTr("Multicam")}]
        })
        if (!loaded.ok) {
            live.status = loaded.error
            return
        }
        for (const role of ["wide", "close", "gimbal"]) {
            const plan = live.mediaPlan.filter(function(p) { return p.role === role })[0]
            multicamModel.addAngle({angleId: role, label: plan.label, resourceId: live.resources[role],
                                    syncOffsetTicks: live.frames(live.angleOffsets[role]),
                                    sourceDurationTicks: live.frames(plan.frames)})
        }
        sequenceEditor.selectEditPoint(live.uid(11), live.uid(12))
        sequenceEditor.transitionClipId = live.uid(12)
        live.ready = true
        live.park()
        live.status = qsTr("Sequence ready · 9 s · 4 shots, linked sound · %1 unsynced multicam clips")
                          .arg(multicamModel.unsyncedClipIds().length)
    }

    // The V1 cuts, in order: each is {outgoing, incoming, ticks, label}.
    function cuts() {
        void sequence.revision
        const list = []
        let id = sequence.edgeClip(live.videoTrack, false)
        while (id.length) {
            const next = sequence.adjacentClip(id, 1)
            if (!next.length) break
            const a = sequence.clipData(id), b = sequence.clipData(next)
            if (sequence.compareTicks(sequence.addTicks(a.start, a.duration), b.start) === 0)
                list.push({outgoing: id, incoming: next, ticks: b.start, label: a.label + " | " + b.label})
            id = next
        }
        return list
    }
    function selectCut(index) {
        const list = live.cuts()
        if (index < 0 || index >= list.length) return false
        sequenceEditor.selectEditPoint(list[index].outgoing, list[index].incoming)
        sequenceEditor.transitionClipId = list[index].incoming
        cutPlayer.seekTicks(list[index].ticks)
        return true
    }

    // RenderQueueModel.open() takes a plain absolute path, not a file URL.
    function localPath(url) { return decodeURIComponent(String(url).replace(/^file:\/\//, "")) }
    function exportProgram() {
        if (!live.queueOpen) live.queueOpen = renderQueue.open(live.localPath(live.mediaFolder + "/export-queue.json"))
        const snapshot = programPlayer.exportJob(live.mediaFolder + "/export-" + Date.now(), qsTr("Sequence edit"))
        if (!snapshot.providerId) {
            live.status = programPlayer.lastError
            return
        }
        const id = renderQueue.enqueueJob(snapshot)
        live.status = id.length ? qsTr("Export queued: %1").arg(snapshot.outputPath) : renderQueue.lastError
    }
}
