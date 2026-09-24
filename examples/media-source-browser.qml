// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-070 fixture: the mock catalog and transport stand in for a host. All
// operations update that host-side state; frame pixels remain provider-owned.
Item {
    id: page
    width: 1120
    height: 680
    property string status: qsTr("Ready")
    property string acknowledgedFrameId: "frame-000120"
    property string acknowledgedTicks: "20352000000"
    property bool mockPlaying: false
    property string mockCommand: ""

    ListModel {
        id: assets
        ListElement { assetId: "shot-001"; resourceId: "demo:shot-001"; label: "Harbor wide"; caption: "00:00:12:04" }
        ListElement { assetId: "shot-002"; resourceId: "demo:shot-002"; label: "Market close"; caption: "00:00:18:12" }
        ListElement { assetId: "shot-003"; resourceId: "demo:shot-003"; label: "Studio interview"; caption: "00:01:02:00" }
        ListElement { assetId: "shot-004"; resourceId: "demo:shot-004"; label: "B-roll forest"; caption: "00:02:11:08" }
        ListElement { assetId: "shot-005"; resourceId: "demo:shot-005"; label: "Offline archive"; caption: "00:04:20:00" }
        ListElement { assetId: "shot-006"; resourceId: "demo:shot-006"; label: "Aerial coast"; caption: "00:05:13:16" }
        ListElement { assetId: "shot-007"; resourceId: "demo:shot-007"; label: "Interview B"; caption: "00:06:02:10" }
        ListElement { assetId: "shot-008"; resourceId: "demo:shot-008"; label: "Night exterior"; caption: "00:07:44:03" }
        ListElement { assetId: "shot-009"; resourceId: "demo:shot-009"; label: "Library inserts"; caption: "00:08:09:14" }
        ListElement { assetId: "shot-010"; resourceId: "demo:shot-010"; label: "Closing aerial"; caption: "00:09:32:20" }
    }
    Tk.SelectionModel { id: selection }
    Component.onCompleted: selection.select("shot-001")
    QtObject {
        id: transport
        readonly property bool canPlay: true
        readonly property bool canSeek: true
        readonly property bool playing: page.mockPlaying
        function requestPlay() { page.mockPlaying = true; return "transport-play-1" }
        function requestStop() { page.mockPlaying = false; return "transport-stop-1" }
        function requestSeek(ticks) { return "transport-seek-" + ticks }
    }
    function metadataFor(id) {
        const offline = id === "shot-005"
        return ({ name: id, sourceTimecode: "00:00:12:04", duration: "00:00:24:00",
                  codec: "ProRes 422", resolution: "3840×2160", tags: ["interview", "selects"],
                  online: !offline, proxyStatus: offline ? "Proxy offline" : "Proxy ready",
                  proxyAvailable: !offline })
    }
    Tk.Box {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
        padding: Tk.Theme.space.md
        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            Tk.Overline { title: qsTr("SOURCE / PROGRAM MONITOR + MEDIA BIN") }
            Tk.Flex {
                width: parent.width
                height: parent.height - Tk.Theme.size.row * 2
                Tk.Flex.grow: 1
                gap: Tk.Theme.space.md
                Tk.SourceProgramMonitor {
                    id: monitor
                    objectName: "fixtureSourceMonitor"
                    width: parent.width * 0.58
                    height: parent.height
                    Tk.Flex.grow: 3
                    role: "source"
                    viewId: "fixture-source"
                    resourceId: "demo:shot-001"
                    frameId: page.acknowledgedFrameId
                    frameTimeTicks: page.acknowledgedTicks
                    previousFrameId: "frame-000119"
                    previousFrameTimeTicks: "20182560000"
                    nextFrameId: "frame-000121"
                    nextFrameTimeTicks: "20521440000"
                    transport: transport
                    onSeekRequested: function(requestId, ticks, requestedId) {
                        // Host acknowledges the exact requested identity; model is authoritative.
                        page.acknowledgedFrameId = requestedId
                        page.acknowledgedTicks = ticks
                        monitor.acknowledgeSeek(requestId, true, requestedId, ticks, "")
                    }
                    onTransportCommandRequested: function(action, requestId) { page.mockCommand = action + " · " + requestId }
                }
                Tk.MediaBin {
                    id: bin
                    width: parent.width * 0.4
                    height: parent.height
                    Tk.Flex.grow: 2
                    model: assets
                    idRole: "assetId"
                    selectionModel: selection
                    viewId: "fixture-media-bin"
                    selectedAssetId: selection.selectedIds.length ? selection.selectedIds[0] : "shot-001"
                    selectedMetadata: selectedAssetId.length ? page.metadataFor(selectedAssetId) : ({})
                    onSearchRequested: function(query) { page.status = qsTr("Host search requested: %1").arg(query) }
                    onAssetActivated: function(id) { page.status = qsTr("Host opened %1").arg(id) }
                    onRelinkRequested: function(id) {
                        const row = assets.findIndex(function(item) { return item.assetId === id })
                        if (row >= 0) assets.setProperty(row, "label", assets.get(row).label + " (relinked)")
                        page.status = qsTr("Host relinked %1").arg(id)
                    }
                    onProxyActionRequested: function(id, action) { page.status = qsTr("Host proxy request: %1 · %2").arg(action).arg(id) }
                }
            }
            Tk.Caption {
                objectName: "fixtureHostStatus"
                text: [page.status, page.mockCommand].filter(function(v) { return v.length > 0 }).join(" · ")
                color: Tk.Theme.color.textMuted
            }
        }
    }
}
