// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtCore
import QindaTK as Tk

// WP-070 fixture on real media. On start it writes two short test signals
// (colour bars, a burned-in binary frame code and a white flash with a 1 kHz
// pop once a second) into <temp>/qindatk-media-demo, imports them into a
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
    property var importedResources: []
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
        jobs[mediaCatalog.generateTestSignal(page.mediaFolder + "/harbor.mkv", ({frames: 75, popInterval: 25}))]
            = {path: page.mediaFolder + "/harbor.mkv", label: qsTr("Harbor bars")}
        jobs[mediaCatalog.generateTestSignal(page.mediaFolder + "/interview.mkv", ({frames: 100, popInterval: 50}))]
            = {path: page.mediaFolder + "/interview.mkv", label: qsTr("Interview bars")}
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
        const assetId = mediaCatalog.importMedia(page.signalJobs[jobId].path, "", page.signalJobs[jobId].label)
        if (!assetId.length) {
            page.status = mediaCatalog.lastError
            return
        }
        const resources = page.importedResources.slice()
        resources.push(mediaCatalog.asset(assetId).resourceId)
        page.importedResources = resources
        if (resources.length === 1) selection.select(assetId)
        if (resources.length === 2) page.buildProgram(resources[0], resources[1])
    }

    function clipRecord(n, track, start, duration, offset, resourceId, kind, label) {
        return ({id: "00000000-0000-4000-8000-0000000000" + n, trackId: track, start: start, duration: duration,
                 sourceOffset: offset, resourceId: resourceId, kind: kind, label: label})
    }
    function buildProgram(firstResource, secondResource) {
        const v1 = "00000000-0000-4000-8000-000000000001"
        const a1 = "00000000-0000-4000-8000-000000000002"
        const two = String(2 * Number(page.second))
        const result = sequence.loadContent({
            timebase: {numerator: page.second, denominator: "1"},
            tracks: [{id: v1, name: "V1", kind: "video"}, {id: a1, name: "A1", kind: "audio"}],
            clips: [page.clipRecord(11, v1, "0", two, "0", firstResource, "video", qsTr("Harbor")),
                    page.clipRecord(12, v1, two, two, page.second, secondResource, "video", qsTr("Interview")),
                    page.clipRecord(21, a1, "0", two, "0", firstResource, "audio", qsTr("Harbor audio")),
                    page.clipRecord(22, a1, two, two, page.second, secondResource, "audio", qsTr("Interview audio"))],
            markers: []
        })
        page.status = result.ok ? qsTr("Program ready: 4 s, two cuts") : result.error
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
