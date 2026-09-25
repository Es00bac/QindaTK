// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Host-owned fixture for the UI contract. Requests update these mock snapshots
// to exercise acknowledgment paths; no plugin ABI, editor or audio processing runs.
Rectangle {
    id: root
    width: 1180
    height: 720
    color: Tk.Theme.color.canvas
    property string fixtureStatus: qsTr("Fixture host ready · no plugin processing")
    property int instanceSerial: 3
    property var descriptors: [
        {pluginId: "urn:fixture:sloom:equalizer", name: "Sloom EQ 4", vendor: "Sloom Labs",
         category: "Dynamics", kind: "Effect", layouts: ["Mono", "Stereo"], available: true,
         state: "available", presets: [{presetId: "preset:vocal-presence", name: "Vocal Presence"}]},
        {pluginId: "urn:fixture:open:plate-reverb", name: "Plate Space", vendor: "Open Audio",
         category: "Reverb", kind: "Effect", layouts: ["Mono→Stereo", "Stereo"], available: true,
         state: "available", presets: [{presetId: "preset:small-room", name: "Small Room"}]},
        {pluginId: "urn:fixture:sloom:synth", name: "Vector Synth", vendor: "Sloom Labs",
         category: "Instrument", kind: "Instrument", layouts: ["Stereo"], available: true,
         state: "available", presets: [{presetId: "preset:soft-keys", name: "Soft Keys"}]},
        {pluginId: "urn:legacy:missing:room", name: "Vintage Hall 32", vendor: "Old Company",
         category: "Reverb", kind: "Effect", layouts: ["Stereo"], available: false,
         state: "missing", reason: qsTr("Not installed; saved session identity retained"), presets: []}
    ]
    property var instanceSnapshot: [
        {instanceId: "inst:voice:eq-main", pluginId: "urn:fixture:sloom:equalizer", name: "Sloom EQ 4",
         vendor: "Sloom Labs", state: "ready", bypassed: false, available: true, latencyFrames: 0,
         editorAvailable: true, parameters: [
             {parameterId: "sloom.eq.band3.frequency", name: "Band 3 frequency", displayValue: "2.4 kHz", available: true},
             {parameterId: "sloom.eq.band3.gain", name: "Band 3 gain", displayValue: "+1.5 dB", available: true}
         ]},
        {instanceId: "inst:voice:plate", pluginId: "urn:fixture:open:plate-reverb", name: "Plate Space",
         vendor: "Open Audio", state: "bypassed", bypassed: true, available: true, latencyFrames: 64,
         editorAvailable: false, parameters: [
             {parameterId: "open.plate.decay", name: "Decay", displayValue: "1.80 s", available: true}
         ]},
        {instanceId: "inst:voice:legacy", pluginId: "urn:legacy:missing:room", name: "Vintage Hall 32",
         vendor: "Old Company", state: "missing", error: qsTr("Plugin not installed; serialized identity retained"),
         bypassed: true, available: false, latencyFrames: 0, editorAvailable: false, parameters: []}
    ]

    function descriptor(pluginId) {
        for (let i = 0; i < descriptors.length; ++i)
            if (String(descriptors[i].pluginId) === String(pluginId)) return descriptors[i]
        return null
    }
    function newInstance(pluginId, presetId) {
        const info = descriptor(pluginId)
        if (!info || !info.available) return null
        ++instanceSerial
        return {
            instanceId: "inst:voice:fixture-" + instanceSerial,
            pluginId: String(info.pluginId), name: String(info.name), vendor: String(info.vendor),
            state: "ready", bypassed: false, available: true,
            latencyFrames: info.kind === "Instrument" ? 32 : 0, editorAvailable: false,
            loadedPresetId: String(presetId || ""), parameters: []
        }
    }
    function appendInstance(pluginId, presetId) {
        const instance = newInstance(pluginId, presetId)
        if (!instance) return false
        const next = instanceSnapshot.slice(); next.push(instance); instanceSnapshot = next
        return true
    }
    function browserAdd(requestId, pluginId, presetId) {
        const accepted = appendInstance(pluginId, presetId)
        browser.acknowledgeAdd(requestId, accepted ? "accepted" : "rejected",
                               accepted ? qsTr("Fixture host published a new instance snapshot")
                                        : qsTr("Descriptor unavailable"))
    }
    function applyRackAction(requestId, instanceId, action, payload) {
        if (action === "add") {
            const added = appendInstance(payload.pluginId, payload.presetId)
            rack.acknowledgeAction(requestId, added ? "accepted" : "rejected",
                                   added ? qsTr("Fixture snapshot updated; no audio processing") : qsTr("Descriptor unavailable"))
            return
        }
        const next = instanceSnapshot.map(function(entry) { return Object.assign({}, entry) })
        const index = next.findIndex(function(entry) { return String(entry.instanceId) === String(instanceId) })
        if (index < 0) { rack.acknowledgeAction(requestId, "rejected", qsTr("Instance no longer exists")); return }
        if (action === "reload" && next[index].available === false) {
            rack.acknowledgeAction(requestId, "rejected", qsTr("%1 is still not installed").arg(next[index].name))
            return
        }
        if (action === "reorder") {
            const moved = next.splice(index, 1)[0]
            next.splice(Math.max(0, Math.min(next.length, payload.targetIndex)), 0, moved)
        } else if (action === "bypass") {
            if (next[index].available === false) { rack.acknowledgeAction(requestId, "error", qsTr("Unavailable plugin cannot be changed")); return }
            next[index].bypassed = !!payload.bypassed
            next[index].state = next[index].bypassed ? "bypassed" : "ready"
        } else if (action === "remove") {
            if (next[index].available === false) { rack.acknowledgeAction(requestId, "rejected", qsTr("Unavailable instance retained for recovery")); return }
            next.splice(index, 1)
        } else if (action === "edit-parameter") {
            next[index].parameters = (next[index].parameters || []).map(function(parameter) {
                const copy = Object.assign({}, parameter)
                if (String(copy.parameterId) === payload.parameterId)
                    copy.displayValue = String(copy.displayValue) + " · host fixture update"
                return copy
            })
        } else if (action === "open-editor") {
            fixtureStatus = qsTr("Editor request received · native editor unavailable in fixture")
        }
        instanceSnapshot = next
        rack.acknowledgeAction(requestId, "accepted", qsTr("Fixture host updated its snapshot for %1").arg(action))
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
            text: qsTr("Search available descriptors, drag or add one to the rack, and request host actions. All plugin metadata and instance state below are fixture snapshots.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            Tk.Flex.grow: 1
            gap: Tk.Theme.space.md
            Tk.PluginBrowser {
                id: browser
                objectName: "fixturePluginBrowser"
                Tk.Flex.grow: 1
                height: parent.height
                plugins: root.descriptors
                scanState: "ready"
                scanMessage: qsTr("4 descriptors · one unavailable saved plugin retained")
                tooltip: qsTr("Installed plugin descriptor browser")
                onAddRequested: function(requestId, pluginId, presetId) {
                    root.browserAdd(requestId, pluginId, presetId)
                }
                onPresetPreviewRequested: function(requestId, pluginId, presetId) {
                    root.fixtureStatus = qsTr("Preset preview requested · %1 · %2 · fixture only")
                        .arg(pluginId).arg(presetId)
                }
            }
            Tk.PluginRack {
                id: rack
                objectName: "fixturePluginRack"
                Tk.Flex.grow: 1
                height: parent.height
                channelId: "channel:voice"
                instances: root.instanceSnapshot
                tooltip: qsTr("Voice channel insert chain")
                onPluginActionRequested: function(requestId, instanceId, action, payload) {
                    root.applyRackAction(requestId, instanceId, action, payload)
                }
                Component.onCompleted: rack.toggleParameters("inst:voice:eq-main")
            }
        }
        Tk.StatusField { objectName: "fixtureHostStatus"; text: root.fixtureStatus; muted: true }
    }
}
