// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (WP-082): presence anchors are stable document IDs or
// positions. The host maps them to this overlay's coordinates and increments
// viewRevision whenever scrolling, zoom or layout changes that mapping.
Item {
    id: control
    objectName: "presenceLayer"
    clip: true
    property var client: null
    property var mapper: null
    property int viewRevision: 0
    property int staleAfterMs: Tk.Theme.motion.toastTimeout * 6
    property string tooltip: qsTr("Remote collaborators")
    property var peers: []
    readonly property var mappedPeers: {
        const source = peers
        if (!mapper) return []
        const result = []
        for (const peer of source) {
            const cursor = resolve(peer.presence.cursor, "cursor", peer.presence)
            const selection = resolve(peer.presence.selection, "selection", peer.presence)
            if (cursor || selection)
                result.push({actorId: peer.actorId, label: peer.presence.label,
                             activity: peer.presence.activity, cursor: cursor,
                             selection: selection, mappedRevision: viewRevision})
        }
        return result
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    onClientChanged: peers = []

    function resolve(anchor, kind, presence) {
        if (!anchor || typeof mapper !== "function") return null
        const rect = mapper(anchor, kind, presence)
        if (!rect || !Number.isFinite(Number(rect.x)) || !Number.isFinite(Number(rect.y)))
            return null
        const width = Number(rect.width || 0)
        const height = Number(rect.height || 0)
        if (!Number.isFinite(width) || !Number.isFinite(height)
            || width < 0 || height < 0) return null
        return {x: Number(rect.x), y: Number(rect.y), width: width, height: height}
    }

    function updatePeer(actorId, presence) {
        if (!actorId || !presence || typeof presence !== "object") return
        const next = peers.filter(peer => peer.actorId !== actorId)
        if (presence.cursor || presence.selection) {
            next.push({actorId: actorId, presence: presence, seenAt: Date.now()})
            if (next.length > 32) next.shift()
        }
        peers = next
    }

    function removePeer(actorId) {
        peers = peers.filter(peer => peer.actorId !== actorId)
    }

    function expireStale(now) {
        peers = peers.filter(peer => now - peer.seenAt < staleAfterMs)
    }

    Connections {
        target: control.client
        ignoreUnknownSignals: true
        function onPresenceChanged(actorId, presence) { control.updatePeer(actorId, presence) }
        function onPresenceLeft(actorId) { control.removePeer(actorId) }
        function onChanged() {
            if (!control.client || !control.client.connected) control.peers = []
        }
        function onDisconnected() { control.peers = [] }
    }
    Timer {
        running: control.peers.length > 0
        repeat: true
        interval: Math.max(Tk.Theme.motion.base, control.staleAfterMs / 2)
        onTriggered: control.expireStale(Date.now())
    }

    Repeater {
        model: control.mappedPeers
        delegate: Item {
            id: marker
            required property var modelData
            objectName: "presencePeer_" + modelData.actorId
            anchors.fill: parent
            readonly property var cursorRect: modelData.cursor
            readonly property var selectionRect: modelData.selection
            readonly property var badgeAnchor: cursorRect || selectionRect
            Accessible.role: Accessible.StaticText
            Accessible.name: (modelData.label || qsTr("Remote editor"))
                             + (modelData.activity ? ": " + modelData.activity : "")

            Rectangle {
                objectName: "remoteSelection"
                visible: !!marker.selectionRect
                x: visible ? marker.selectionRect.x : 0
                y: visible ? marker.selectionRect.y : 0
                width: visible ? marker.selectionRect.width : 0
                height: visible ? marker.selectionRect.height : 0
                color: Tk.Theme.alpha(Tk.Theme.color.info, Tk.Theme.opacity.hidden)
                border.color: Tk.Theme.color.info
                border.width: Tk.Theme.size.border
                radius: Tk.Theme.radius.xs
            }
            Rectangle {
                objectName: "remoteCursor"
                visible: !!marker.cursorRect
                x: visible ? marker.cursorRect.x : 0
                y: visible ? marker.cursorRect.y : 0
                width: Tk.Theme.size.focusRing
                height: visible ? Math.max(marker.cursorRect.height, Tk.Theme.size.control) : 0
                color: Tk.Theme.color.info
            }
            Rectangle {
                id: badge
                objectName: "remoteActivity"
                visible: !!marker.badgeAnchor
                x: visible ? Math.max(0, Math.min(marker.badgeAnchor.x, marker.width - width)) : 0
                y: visible ? (marker.badgeAnchor.y >= height + Tk.Theme.space.xs
                              ? marker.badgeAnchor.y - height - Tk.Theme.space.xs
                              : marker.badgeAnchor.y + Math.max(marker.badgeAnchor.height,
                                                               Tk.Theme.size.control) + Tk.Theme.space.xs) : 0
                implicitWidth: badgeBody.implicitWidth + Tk.Theme.space.sm * 2
                implicitHeight: badgeBody.implicitHeight + Tk.Theme.space.xs * 2
                width: implicitWidth
                height: implicitHeight
                color: Tk.Theme.color.panel
                border.color: Tk.Theme.color.info
                border.width: Tk.Theme.size.border
                radius: Tk.Theme.radius.sm
                Tk.Flex {
                    id: badgeBody
                    anchors.centerIn: parent
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    Tk.Label { text: marker.modelData.label || qsTr("Remote editor") }
                    Tk.Caption {
                        text: marker.modelData.activity || ""
                        visible: text.length > 0
                    }
                }
            }
        }
    }
}
