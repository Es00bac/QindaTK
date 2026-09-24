// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    visible: true
    name: "PluginHostControls"
    when: windowShown
    property var pluginData: [
        {pluginId: "plugin:eq", name: "Sloom EQ", vendor: "Sloom", category: "Dynamics",
         kind: "Effect", layouts: ["Stereo"], available: true, presets: [{presetId: "preset:vocal"}]},
        {pluginId: "plugin:room", name: "Room Reverb", vendor: "Open Audio", category: "Reverb",
         kind: "Effect", layouts: ["Stereo"], available: true, presets: []},
        {pluginId: "plugin:synth", name: "Vector Keys", vendor: "Sloom", category: "Instrument",
         kind: "Instrument", layouts: ["Stereo"], available: true, presets: []},
        {pluginId: "plugin:missing", name: "Legacy Hall", vendor: "Old Co", category: "Reverb",
         kind: "Effect", layouts: ["Stereo"], available: false, reason: "Not installed", presets: []}
    ]
    property var instanceData: [
        {instanceId: "instance:eq", pluginId: "plugin:eq", name: "Sloom EQ", state: "ready",
         available: true, bypassed: false, latencyFrames: 0, editorAvailable: true,
         parameters: [{parameterId: "eq.frequency", name: "Frequency", displayValue: "1 kHz", available: true}]},
        {instanceId: "instance:room", pluginId: "plugin:room", name: "Room Reverb", state: "bypassed",
         available: true, bypassed: true, latencyFrames: 64, editorAvailable: false, parameters: []}
    ]
    property var lastAdd: null
    property var lastBrowserAck: null
    property var lastRackAction: null
    property var lastRackAck: null

    Tk.PluginBrowser {
        id: browser
        width: 440
        height: 320
        visible: true
        plugins: root.pluginData
        onAddRequested: function(id, pluginId, presetId) { root.lastAdd = [id, pluginId, presetId] }
        onRequestAcknowledged: function(id, state, message) { root.lastBrowserAck = [id, state, message] }
    }
    Tk.PluginRack {
        id: rack
        width: 600
        height: 360
        visible: true
        channelId: "channel:voice"
        instances: root.instanceData
        onPluginActionRequested: function(id, instanceId, action, payload) {
            root.lastRackAction = [id, instanceId, action, payload]
        }
        onPluginActionAcknowledged: function(id, instanceId, action, state, message) {
            root.lastRackAck = [id, instanceId, action, state, message]
        }
    }

    function test_browser_filters_and_waits_for_ack() {
        browser.query = "room"
        compare(browser.filteredPlugins.length, 1)
        compare(browser.filteredPlugins[0].pluginId, "plugin:room")
        browser.query = ""
        browser.kindFilter = "Instrument"
        compare(browser.filteredPlugins.length, 1)
        compare(browser.filteredPlugins[0].pluginId, "plugin:synth")
        browser.kindFilter = "All"
        browser.categoryFilter = "Reverb"
        compare(browser.filteredPlugins.length, 2) // Missing descriptors remain identifiable.
        browser.categoryFilter = "All"
        compare(browser.requestAdd("plugin:room", ""), "plugin-add:plugin:room:1")
        compare(lastAdd[1], "plugin:room")
        verify(browser.requestState === "pending")
        verify(!browser.acknowledgeAdd("stale-request", "accepted", "stale"))
        verify(browser.acknowledgeAdd(lastAdd[0], "accepted", "host added it"))
        compare(lastBrowserAck[1], "accepted")
        verify(browser.requestAdd("plugin:missing", "") === "")
    }

    function test_rack_requests_keep_host_snapshot_until_update() {
        const moveId = rack.requestReorder("instance:eq", 1)
        verify(moveId.length > 0)
        compare(lastRackAction[1], "instance:eq")
        compare(lastRackAction[2], "reorder")
        compare(lastRackAction[3].targetIndex, 1)
        compare(rack.instances[0].instanceId, "instance:eq")
        verify(rack.acknowledgeAction(moveId, "accepted", "ordered"))
        compare(rack.instances[0].instanceId, "instance:eq") // host publishes the new order separately.
        compare(lastRackAck[3], "accepted")

        const bypassId = rack.requestBypass("instance:eq", true)
        verify(bypassId.length > 0)
        compare(lastRackAction[2], "bypass")
        verify(rack.acknowledgeAction(bypassId, "error", "process unavailable"))
        compare(rack.statusState, "error")
        compare(rack.instances[0].bypassed, false)

        const parameterId = rack.requestParameterEdit("instance:eq", rack.instances[0].parameters[0])
        verify(parameterId.length > 0)
        compare(lastRackAction[2], "edit-parameter")
        compare(lastRackAction[3].parameterId, "eq.frequency")
        verify(rack.requestRemove("instance:room").length > 0)
        verify(rack.requestEditor("instance:eq").length > 0)
        verify(rack.requestAdd("plugin:room", "").length > 0)
    }
}
