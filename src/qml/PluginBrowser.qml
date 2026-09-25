// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Registry metadata and scan results are snapshots from the host. Search and
// filtering are local presentation state; add/preview actions remain requests.
// AGENT-CONTRACT (WP-066, AudioPluginHost, docs/audio-plugins.md): with `host`
// set (session.plugins) the rows and scan status come from the real registry,
// and an add goes to host.rackAction("add") on `targetChannelId` and is
// acknowledged with its answer. addRequested is still emitted first.
T.Control {
    id: root
    property Tk.AudioPluginHost host: null
    property string targetChannelId: ""
    property var plugins: root.host ? root.host.browserPlugins : [] // {pluginId,name,vendor,category,kind,layouts,available,state,reason,presets}
    property string query: ""
    property string kindFilter: "All"
    property string categoryFilter: "All"
    property string scanState: root.host ? root.host.registry.scanState : "idle"
    property string scanMessage: root.host ? root.host.registry.scanMessage : ""
    property string tooltip: ""
    property string _serial: "0"
    property string _lastRequestId: ""
    property string _lastPluginId: ""
    property string _requestState: "idle"
    property string _requestMessage: ""
    readonly property var categories: root.categoryOptions()
    readonly property var filteredPlugins: root.filterPlugins()
    readonly property string requestState: _requestState
    readonly property string requestMessage: _requestMessage
    signal addRequested(string requestId, string pluginId, string presetId)
    signal presetPreviewRequested(string requestId, string pluginId, string presetId)
    signal requestAcknowledged(string requestId, string state, string message)

    objectName: "pluginBrowser"
    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    padding: Tk.Theme.space.sm
    hoverEnabled: true
    Accessible.role: Accessible.List
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Plugin browser")
    Accessible.description: qsTr("%1 plugin descriptors match. Registry scan: %2. %3")
        .arg(filteredPlugins.length).arg(scanState).arg(_requestMessage || scanMessage)

    function pluginById(pluginId) {
        for (let i = 0; i < plugins.length; ++i)
            if (String(plugins[i].pluginId) === String(pluginId)) return plugins[i]
        return null
    }
    function categoryOptions() {
        const found = { "All": true }
        for (let i = 0; i < root.plugins.length; ++i) {
            const category = String(root.plugins[i].category || "Other")
            found[category] = true
        }
        return Object.keys(found)
    }
    function filterPlugins() {
        const needle = root.query.trim().toLocaleLowerCase()
        const results = []
        for (let i = 0; i < root.plugins.length; ++i) {
            const p = root.plugins[i]
            const kind = String(p.kind || "Effect")
            const category = String(p.category || "Other")
            const text = [p.name, p.vendor, p.pluginId, p.category, p.kind,
                          (p.layouts || []).join(" "), p.reason].join(" ").toLocaleLowerCase()
            if ((root.kindFilter !== "All" && kind !== root.kindFilter)
                || (root.categoryFilter !== "All" && category !== root.categoryFilter)
                || (needle.length && text.indexOf(needle) < 0)) continue
            results.push(p)
        }
        return results
    }
    function requestAdd(pluginId, presetId) {
        const plugin = root.pluginById(pluginId)
        if (!root.enabled || !plugin || plugin.available === false || root._requestState === "pending") return ""
        root._serial = String(Number(root._serial) + 1)
        const id = "plugin-add:" + String(pluginId) + ":" + root._serial
        root._lastRequestId = id
        root._lastPluginId = String(pluginId)
        root._requestState = "pending"
        root._requestMessage = qsTr("Waiting for host instantiation")
        root.addRequested(id, String(pluginId), String(presetId || ""))
        if (root.host) {
            const answer = root.host.rackAction("add", "", {channelId: root.targetChannelId, pluginId: String(pluginId)},
                                                root.Window.window)
            root.acknowledgeAdd(id, answer.state, answer.state === "accepted"
                                ? qsTr("Added %1").arg(String(plugin.name || pluginId)) : answer.message)
        }
        return id
    }
    function acknowledgeAdd(requestId, state, message) {
        if (!requestId || String(requestId) !== root._lastRequestId || root._requestState !== "pending") return false
        const outcome = String(state)
        if (["accepted", "rejected", "error"].indexOf(outcome) < 0) return false
        root._requestState = outcome
        root._requestMessage = String(message || (outcome === "accepted" ? qsTr("Plugin added by host")
                                                    : qsTr("Plugin add %1").arg(outcome)))
        root.requestAcknowledged(String(requestId), outcome, root._requestMessage)
        return true
    }
    function requestPresetPreview(pluginId, presetId) {
        const plugin = root.pluginById(pluginId)
        if (!root.enabled || !plugin || plugin.available === false || !presetId) return ""
        root._serial = String(Number(root._serial) + 1)
        const id = "plugin-preview:" + String(pluginId) + ":" + root._serial
        root.presetPreviewRequested(id, String(pluginId), String(presetId))
        return id
    }
    function presetFor(plugin) {
        return plugin.presets && plugin.presets.length ? String(plugin.presets[0].presetId || "") : ""
    }
    function statusFor(plugin) {
        if (plugin.available === false) return String(plugin.reason || qsTr("Unavailable"))
        return String(plugin.state || plugin.scanState || qsTr("Available"))
    }

    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        anchors.fill: parent
        padding: root.padding
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            width: parent.width
            Tk.Overline { title: qsTr("PLUGIN REGISTRY") }
            Tk.Spacer { }
            Tk.Caption { text: qsTr("%1 results").arg(root.filteredPlugins.length); color: Tk.Theme.color.textMuted }
        }
        Tk.Flex {
            width: parent.width
            gap: Tk.Theme.space.xs
            Tk.SearchField {
                id: search
                objectName: "pluginSearch"
                Tk.Flex.grow: 1
                placeholderText: qsTr("Search name, vendor, capability…")
                tooltip: qsTr("Filter installed plugin descriptors")
                onSearched: function(text) { root.query = String(text) }
            }
            Tk.ComboBox {
                objectName: "pluginKindFilter"
                width: Tk.Theme.size.fieldWidth
                model: [qsTr("All"), qsTr("Effect"), qsTr("Instrument")]
                currentIndex: ["All", "Effect", "Instrument"].indexOf(root.kindFilter)
                tooltip: qsTr("Filter plugin kind")
                onActivated: function(index) { root.kindFilter = ["All", "Effect", "Instrument"][index] }
            }
            Tk.ComboBox {
                objectName: "pluginCategoryFilter"
                width: Tk.Theme.size.fieldWidth
                model: root.categories
                currentIndex: root.categories.indexOf(root.categoryFilter)
                tooltip: qsTr("Filter plugin category")
                onActivated: function(index) { root.categoryFilter = String(root.categories[index]) }
            }
        }
        Tk.Caption {
            objectName: "pluginBrowserStatus"
            text: root._requestMessage.length ? root._requestMessage
                : (root.scanMessage.length ? root.scanMessage : qsTr("Registry: %1").arg(root.scanState))
            color: root._requestState === "error" || root._requestState === "rejected"
                   ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        }
        ListView {
            id: results
            objectName: "pluginResults"
            width: parent.width
            implicitHeight: Tk.Theme.size.panelMinHeight
            Tk.Flex.grow: 1
            clip: true
            model: root.filteredPlugins
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Plugin search results")
            delegate: Item {
                id: pluginRow
                required property int index
                required property var modelData
                property string pluginId: String(modelData.pluginId)
                property string presetId: root.presetFor(modelData)
                width: results.width
                height: Tk.Theme.size.control * 2 + Tk.Theme.space.sm
                objectName: "pluginResult_" + pluginId
                Rectangle {
                    anchors.fill: parent
                    color: pluginRow.index % 2 ? Tk.Theme.color.surface : Tk.Theme.color.panelAlt
                    radius: Tk.Theme.radius.sm
                }
                Tk.Flex {
                    anchors.fill: parent
                    padding: Tk.Theme.space.xs
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Flex {
                        width: parent.width
                        Tk.Label {
                            text: String(modelData.name || pluginRow.pluginId)
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                        Tk.Spacer { }
                        Tk.Button {
                            objectName: "pluginAdd_" + pluginRow.pluginId
                            text: qsTr("Add")
                            tooltip: qsTr("Request host instantiation of %1").arg(modelData.name || pluginRow.pluginId)
                            enabled: modelData.available !== false && root._requestState !== "pending"
                            onClicked: root.requestAdd(pluginRow.pluginId, root.presetFor(modelData))
                        }
                    }
                    Tk.Flex {
                        width: parent.width
                        Tk.Caption {
                            text: qsTr("%1 · %2 · %3 · %4")
                                .arg(modelData.vendor || qsTr("Unknown vendor"))
                                .arg(modelData.category || qsTr("Other"))
                                .arg(modelData.kind || qsTr("Effect"))
                                .arg((modelData.layouts || []).join(" / ") || qsTr("Any layout"))
                            color: Tk.Theme.color.textMuted
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                        Tk.Spacer { }
                        Tk.Caption {
                            text: root.statusFor(modelData)
                            color: modelData.available === false ? Tk.Theme.color.warning : Tk.Theme.color.success
                            Accessible.ignored: true
                        }
                        Tk.Button {
                            visible: !!root.presetFor(modelData)
                            text: qsTr("Preview preset")
                            tooltip: qsTr("Ask the host to preview the listed preset")
                            enabled: modelData.available !== false
                            onClicked: root.requestPresetPreview(pluginRow.pluginId, root.presetFor(modelData))
                        }
                    }
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: String(modelData.name || pluginId)
                Accessible.description: root.statusFor(modelData) + qsTr(". Drag this entry to the plugin rack or use Add.")
                Drag.keys: ["application/x-qindatk-plugin-id"]
                Drag.mimeData: ({"application/x-qindatk-plugin-id": pluginId})
                Drag.active: pluginDrag.active && modelData.available !== false
                Drag.supportedActions: Qt.CopyAction
                Drag.proposedAction: Qt.CopyAction
                DragHandler {
                    id: pluginDrag
                    objectName: "pluginDrag_" + pluginRow.pluginId
                    target: null
                    enabled: modelData.available !== false
                }
            }
        }
    }
    Tk.ToolTip { text: root.tooltip; visible: root.hovered && root.tooltip.length > 0 }
}
