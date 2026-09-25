// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A live plugin host (WP-066): PluginBrowser and PluginRack are wired to a
// real AudioSession's plugin host (`host: session.plugins`). The browser lists
// the CLAP plugins found on this machine (CLAP_PATH, ~/.clap, /usr/lib/clap…);
// Add, reorder, bypass, remove, reload, editor and knob edits run real CLAP
// instances, each in its own qindatk-plugin-host process, on the Voice track.
// The smoke test points CLAP_PATH at the test plugins (examples/CMakeLists.txt).
Rectangle {
    id: root
    width: 1180
    height: 720
    color: Tk.Theme.color.canvas
    property string channel: ""
    readonly property var host: session.plugins
    readonly property var clap: root.host.formats.length ? root.host.formats[0] : ({})

    function formatSummary() {
        return root.host.formats.map(function(format) {
            return format.format + (format.available ? qsTr(" available") : qsTr(" unavailable"))
        }).join(" · ")
    }

    Tk.AudioSession { id: session }

    Component.onCompleted: {
        root.channel = "track:" + session.addTrack(qsTr("Voice"))
        // AGENT-NOTE: a blocking scan keeps the example deterministic for
        // --grab; apps call registry.scan() (asynchronous) instead.
        root.host.registry.scanNow()
        // Start with the first available effect on the track, so the rack
        // shows a live instance with its parameters.
        const rows = root.host.browserPlugins
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].available && rows[i].kind === "Effect") {
                const answer = root.host.rackAction("add", "", {channelId: root.channel, pluginId: rows[i].pluginId})
                if (answer.state === "accepted") rack.toggleParameters(answer.result.instanceId)
                break
            }
        }
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Label {
            text: qsTr("Plugin browser and insert chain")
            font.pixelSize: Tk.Theme.font.title
            Accessible.role: Accessible.Heading
        }
        Tk.Label {
            text: qsTr("Plugins found on this machine. Add one to the Voice track, drag to reorder, bypass it, turn its knobs or open its editor: each request goes to the session's plugin host and runs the real plugin in its own process.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            Tk.Flex.grow: 1
            gap: Tk.Theme.space.md
            Tk.PluginBrowser {
                id: browser
                objectName: "livePluginBrowser"
                Tk.Flex.grow: 1
                height: parent.height
                host: root.host
                targetChannelId: root.channel
                tooltip: qsTr("CLAP plugins installed on this machine")
            }
            Tk.PluginRack {
                id: rack
                objectName: "livePluginRack"
                Tk.Flex.grow: 1
                height: parent.height
                host: root.host
                channelId: root.channel
                tooltip: qsTr("Voice track insert chain")
            }
        }
        Tk.StatusField {
            objectName: "liveHostStatus"
            text: root.host.lastError.length
                  ? root.host.lastError
                  : qsTr("%1 · inserts on Voice: %2 · host %3").arg(root.formatSummary())
                        .arg(rack.instanceCount)
                        .arg(root.clap.available ? qsTr("ready") : String(root.clap.reason || ""))
            muted: true
        }
    }
}
