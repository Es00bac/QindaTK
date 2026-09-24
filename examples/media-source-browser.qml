// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtCore
import QindaTK as Tk

// WP-070 fixture on real media. On start it writes two short test signals
// (colour bars, a burned-in binary frame code and a white flash with a 1 kHz
// pop once a second, starting mid-second so the first frame, which the monitors
// and thumbnails show, is bars) into <temp>/qindatk-media-demo, imports them into a
// MediaCatalog and cuts them into a two-clip program. The source monitor
// plays the selected bin asset; the program monitor plays the TimelineModel
// composite; Export queues a verified FFV1/PCM Matroska render. The files are
// rewritten in place on every start.
Item {
    id: page
    width: 1120
    height: 680
    readonly property string mediaFolder: StandardPaths.writableLocation(StandardPaths.TempLocation)
                                          + "/qindatk-media-demo"
    readonly property string second: "705600000"
    property string status: qsTr("Writing test signals…")
    property var signalJobs: ({})
    property string harborResource: ""
    property string interviewResource: ""
    property bool queueOpen: false

    Tk.ResourceResolver { id: mediaResolver }
    Tk.AssetProvider { id: thumbnails; resolver: mediaResolver }
    Tk.SelectionModel { id: selection }
    Tk.MediaCatalog {
        id: mediaCatalog
        resolver: mediaResolver
        proxyDirectory: page.mediaFolder + "/proxies"
        onJobFinished: function(jobId, kind, assetId, ok, error) { page.jobFinished(jobId, kind, ok, error) }
    }
    Tk.TimeMap { id: sequenceTimeMap; Component.onCompleted: setFrameRate(25, 1) }
    Tk.TimelineModel { id: sequence; timeMap: sequenceTimeMap }
    Tk.MediaPlayer {
        id: sourcePlayer
        resolver: mediaResolver
        catalog: mediaCatalog
        resourceId: selection.selectedIds.length ? mediaCatalog.asset(selection.selectedIds[0]).resourceId || "" : ""
    }
    Tk.MediaPlayer {
        id: programPlayer
        resolver: mediaResolver
        catalog: mediaCatalog
        timeline: sequence
        frameWidth: 320
        frameHeight: 180
    }
    Tk.RenderQueueModel { id: renderQueue }

    Component.onCompleted: {
        const jobs = {}
        // 75 frames (3 s) each: the program uses harbor 0-2 s and interview 1-3 s.
        jobs[mediaCatalog.generateTestSignal(page.mediaFolder + "/harbor.mkv",
                                             ({frames: 75, popInterval: 25, popPhase: 12}))]
            = {path: page.mediaFolder + "/harbor.mkv", label: qsTr("Harbor bars"), role: "harbor"}
        jobs[mediaCatalog.generateTestSignal(page.mediaFolder + "/interview.mkv",
                                             ({frames: 75, popInterval: 50, popPhase: 25}))]
            = {path: page.mediaFolder + "/interview.mkv", label: qsTr("Interview bars"), role: "interview"}
        page.signalJobs = jobs
    }

    function jobFinished(jobId, kind, ok, error) {
        if (kind === "proxy") {
            page.status = ok ? qsTr("Proxy ready") : qsTr("Proxy failed: %1").arg(error)
            return
        }
        if (kind !== "testSignal" || !page.signalJobs[jobId]) return
        if (!ok) {
            page.status = qsTr("Test signal failed: %1").arg(error)
            return
        }
        const job = page.signalJobs[jobId]
        const assetId = mediaCatalog.importMedia(job.path, "", job.label)
        if (!assetId.length) {
            page.status = mediaCatalog.lastError
            return
        }
        const resourceId = mediaCatalog.asset(assetId).resourceId
        if (job.role === "harbor") {
            page.harborResource = resourceId
            selection.select(assetId) // the source monitor opens on the harbor clip
        } else {
            page.interviewResource = resourceId
            if (!selection.selectedIds.length) selection.select(assetId)
        }
        page.buildProgram()
    }

    function clipRecord(n, track, start, duration, offset, resourceId, kind, label) {
        return ({id: "00000000-0000-4000-8000-0000000000" + n, trackId: track, start: start, duration: duration,
                 sourceOffset: offset, resourceId: resourceId, kind: kind, label: label})
    }
    // The program cuts from harbor (0-2 s) to interview (2-4 s). Each clip joins
    // as soon as its media is imported, so the program monitor presents a frame
    // as early as the source monitor does.
    function buildProgram() {
        const v1 = "00000000-0000-4000-8000-000000000001"
        const a1 = "00000000-0000-4000-8000-000000000002"
        const two = String(2 * Number(page.second))
        const clips = []
        if (page.harborResource.length) {
            clips.push(page.clipRecord(11, v1, "0", two, "0", page.harborResource, "video", qsTr("Harbor")))
            clips.push(page.clipRecord(21, a1, "0", two, "0", page.harborResource, "audio", qsTr("Harbor audio")))
        }
        if (page.interviewResource.length) {
            clips.push(page.clipRecord(12, v1, two, two, page.second, page.interviewResource, "video",
                                       qsTr("Interview")))
            clips.push(page.clipRecord(22, a1, two, two, page.second, page.interviewResource, "audio",
                                       qsTr("Interview audio")))
        }
        const result = sequence.loadContent({
            timebase: {numerator: page.second, denominator: "1"},
            tracks: [{id: v1, name: "V1", kind: "video"}, {id: a1, name: "A1", kind: "audio"}],
            clips: clips,
            markers: []
        })
        page.status = !result.ok ? result.error
                    : clips.length === 4 ? qsTr("Program ready: 4 s, two cuts")
                                         : qsTr("Program: waiting for the second clip")
    }
    // RenderQueueModel.open() takes a plain absolute path, not a file URL.
    function localPath(url) {
        return decodeURIComponent(String(url).replace(/^file:\/\//, ""))
    }
    function exportProgram() {
        if (!page.queueOpen) page.queueOpen = renderQueue.open(page.localPath(page.mediaFolder + "/export-queue.json"))
        // The queue publishes a directory holding program.mkv and its manifest.
        const snapshot = programPlayer.exportJob(page.mediaFolder + "/export-" + Date.now(),
                                                 qsTr("Demo program"))
        if (!snapshot.providerId) {
            page.status = programPlayer.lastError
            return
        }
        const id = renderQueue.enqueueJob(snapshot)
        page.status = id.length ? qsTr("Export queued: %1").arg(snapshot.outputPath) : renderQueue.lastError
    }

    Tk.Box {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
        padding: Tk.Theme.space.md
        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            Tk.Flex {
                width: parent.width
                gap: Tk.Theme.space.sm
                Tk.Overline { title: qsTr("SOURCE / PROGRAM MONITOR + MEDIA BIN") }
                Tk.Spacer { }
                Tk.Button {
                    objectName: "fixtureExport"
                    text: qsTr("Export program")
                    enabled: programPlayer.state === "ready"
                    tooltip: qsTr("Queue an FFV1/PCM Matroska render of the program")
                    onClicked: page.exportProgram()
                }
            }
            Tk.Flex {
                width: parent.width
                height: parent.height - Tk.Theme.size.row * 3
                Tk.Flex.grow: 1
                gap: Tk.Theme.space.md
                Tk.SourceProgramMonitor {
                    objectName: "fixtureSourceMonitor"
                    width: parent.width * 0.3
                    height: parent.height
                    Tk.Flex.grow: 1
                    role: "source"
                    viewId: "fixture-source"
                    player: sourcePlayer
                }
                Tk.SourceProgramMonitor {
                    objectName: "fixtureProgramMonitor"
                    width: parent.width * 0.3
                    height: parent.height
                    Tk.Flex.grow: 1
                    role: "program"
                    viewId: "fixture-program"
                    player: programPlayer
                }
                Tk.MediaBin {
                    objectName: "fixtureMediaBin"
                    width: parent.width * 0.34
                    height: parent.height
                    Tk.Flex.grow: 1
                    catalog: mediaCatalog
                    selectionModel: selection
                    provider: thumbnails
                    viewId: "fixture-media-bin"
                    selectedAssetId: selection.selectedIds.length ? selection.selectedIds[0] : ""
                    onRelinkRequested: function(id) {
                        const count = mediaCatalog.relinkMissingFrom(page.mediaFolder)
                        page.status = qsTr("Relinked %1 offline assets from the demo folder").arg(count)
                    }
                }
            }
            Tk.Caption {
                objectName: "fixtureHostStatus"
                text: [page.status, renderQueue.lastError].filter(function(v) { return v.length > 0 }).join(" · ")
                color: Tk.Theme.color.textMuted
            }
        }
    }
}
