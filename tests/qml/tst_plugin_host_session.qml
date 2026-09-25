// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-066: PluginBrowser and PluginRack wired to a real AudioSession's plugin
// host (`host: session.plugins`). The rows come from the registry's scan of
// the test CLAP binaries (pluginFixtures, from tst_qml.cpp); every request
// runs a real CLAP instance in its own qindatk-plugin-host process.
TestCase {
    id: root
    visible: true
    width: 1100
    height: 640
    name: "PluginHostSession"
    when: windowShown
    readonly property string gainId: "org.qindatk.test.gain"
    readonly property string synthId: "org.qindatk.test.synth"
    property string channel: ""
    property var browserAcks: []
    property var rackAcks: []
    property var rackRequests: []

    Tk.AudioSession { id: session }

    Tk.PluginBrowser {
        id: browser
        width: 440
        height: 600
        visible: true
        host: session.plugins
        targetChannelId: root.channel
        onRequestAcknowledged: function(id, state, message) { root.browserAcks.push([id, state, message]) }
    }
    Tk.PluginRack {
        id: rack
        x: 460
        width: 620
        height: 600
        visible: true
        host: session.plugins
        channelId: root.channel
        onPluginActionRequested: function(id, instanceId, action, payload) { root.rackRequests.push([id, instanceId, action]) }
        onPluginActionAcknowledged: function(id, instanceId, action, state, message) {
            root.rackAcks.push([id, instanceId, action, state, message])
        }
    }

    function initTestCase() {
        const registry = session.plugins.registry
        registry.cacheFile = pluginFixtures.cacheFile
        registry.searchPaths = [pluginFixtures.clapDirectory]
        verify(registry.scanNow())
        root.channel = "track:" + session.addTrack("Voice")
        verify(root.channel.length > 6)
    }

    function row(id) {
        for (let i = 0; i < rack.instances.length; ++i)
            if (rack.instances[i].instanceId === id) return rack.instances[i]
        return null
    }
    function lastRackAck() { return root.rackAcks[root.rackAcks.length - 1] }

    function test_1_browser_lists_the_scanned_plugins() {
        compare(browser.scanState, "ready")
        const gain = browser.pluginById(root.gainId)
        verify(gain !== null)
        compare(gain.name, "Test Gain")
        compare(gain.available, true)
        verify(browser.pluginById(root.synthId) !== null)
        browser.kindFilter = "Instrument"
        verify(browser.filteredPlugins.some(function(p) { return p.pluginId === root.synthId }))
        verify(!browser.filteredPlugins.some(function(p) { return p.pluginId === root.gainId }))
        browser.kindFilter = "All"
    }

    function test_2_browser_add_instantiates_on_the_channel() {
        const add = findChild(browser, "pluginAdd_" + root.gainId)
        verify(add !== null)
        tryVerify(function() { return add.visible && add.width > 0 })
        mouseClick(add)
        compare(browser.requestState, "accepted")
        compare(root.browserAcks[root.browserAcks.length - 1][1], "accepted")
        const chain = session.plugins.chain(root.channel)
        compare(chain.length, 1)
        compare(rack.instanceCount, 1)
        compare(rack.instances[0].instanceId, chain[0])
        compare(rack.instances[0].state, "ready")
        verify(rack.instances[0].processId > 0) // a real host process
        tryVerify(function() { return findChild(rack, "pluginSlot_" + chain[0]) !== null })
        // A binary the scan could not instantiate is listed as unavailable
        // and cannot be added.
        const broken = browser.pluginById("org.qindatk.test.init-fails")
        verify(broken !== null)
        compare(broken.available, false)
        compare(browser.requestAdd(broken.pluginId, ""), "")
        // The host refuses an add with no target channel; nothing changes.
        browser.targetChannelId = ""
        verify(browser.requestAdd(root.gainId, "").length > 0)
        compare(browser.requestState, "rejected")
        verify(browser.requestMessage.length > 0)
        browser.targetChannelId = Qt.binding(function() { return root.channel })
        compare(session.plugins.chain(root.channel).length, 1)
    }

    function test_3_rack_reorders_bypasses_and_edits_through_the_host() {
        const gain = session.plugins.chain(root.channel)[0]
        verify(rack.requestAdd(root.synthId, "").length > 0)
        compare(lastRackAck()[3], "accepted")
        compare(rack.instanceCount, 2)
        const synth = rack.instances[1].instanceId
        compare(rack.instances[1].pluginId, root.synthId)

        // Reorder: the host's chain (the render graph's order) and the rows change.
        verify(rack.requestReorder(synth, 0).length > 0)
        compare(lastRackAck()[2], "reorder")
        compare(lastRackAck()[3], "accepted")
        compare(session.plugins.chain(root.channel), [synth, gain])
        compare(rack.instances[0].instanceId, synth)
        compare(rack.instances[1].instanceId, gain)

        // Bypass
        verify(rack.requestBypass(gain, true).length > 0)
        compare(lastRackAck()[3], "accepted")
        compare(session.plugins.instance(gain).bypassed, true)
        compare(row(gain).state, "bypassed")
        verify(rack.requestBypass(gain, false).length > 0)
        compare(row(gain).state, "ready")

        // Parameters: the knob edits the host's automation parameter; a
        // typed value goes through edit-parameter.
        rack.toggleParameters(gain)
        const knob = findChild(rack, "pluginParameter_" + gain + "/1")
        verify(knob !== null)
        tryVerify(function() { return knob.visible })
        const binding = session.plugins.parameterBinding(gain, 1)
        compare(knob.binding, binding)
        verify(rack.requestParameterEdit(gain, {parameterId: gain + "/1", displayValue: "0.5"}).length > 0)
        compare(lastRackAck()[3], "accepted")
        compare(binding.value, 0.5)
        compare(session.automation.parameterData(gain + "/1")["default"], 0.5)
        verify(rack.requestParameterEdit(gain, {parameterId: gain + "/1", displayValue: "loud"}).length > 0)
        compare(lastRackAck()[3], "rejected")
        compare(rack.statusState, "rejected")
        compare(binding.value, 0.5)

        // Reload of a live instance keeps its process.
        const pid = row(gain).processId
        verify(rack.requestReload(gain).length > 0)
        compare(lastRackAck()[3], "accepted")
        compare(row(gain).processId, pid)

        // Every request was emitted before the host answered it.
        compare(root.rackRequests.length, root.rackAcks.length)
    }

    function test_4_rack_remove_ends_the_instance() {
        const chain = session.plugins.chain(root.channel)
        compare(chain.length, 2)
        verify(rack.requestRemove(chain[0]).length > 0)
        compare(lastRackAck()[3], "accepted")
        compare(rack.instanceCount, 1)
        compare(session.plugins.chain(root.channel), [chain[1]])
        verify(rack.requestRemove(chain[1]).length > 0)
        compare(rack.instanceCount, 0)
        compare(session.plugins.instanceIds.length, 0)
    }
}
