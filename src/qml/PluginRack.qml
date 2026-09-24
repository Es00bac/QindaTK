// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// `instances` is an ordered host snapshot. Every button, switch and drag
// emits a request; the rack waits for host acknowledgement and a new snapshot.
T.Control {
    id: root
    property var instances: [] // {instanceId,pluginId,name,vendor,state,bypassed,latencyFrames,error,parameters,editorAvailable}
    property string channelId: ""
    property string tooltip: ""
    property string emptyText: qsTr("Drop a plugin here or add one from the browser")
    property real reorderDistance: Tk.Theme.size.row + Tk.Theme.space.xs
    property var _pendingActions: ({})
    property string _serial: "0"
    property string _status: ""
    property string _statusState: "idle"
    property string _expandedInstanceId: ""
    readonly property string statusText: _status
    readonly property string statusState: _statusState
    readonly property int instanceCount: instances ? instances.length : 0

    signal pluginActionRequested(string requestId, string instanceId, string action, var payload)
    signal pluginActionAcknowledged(string requestId, string instanceId, string action,
                                    string state, string message)

    objectName: "pluginRack"
    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    padding: Tk.Theme.space.sm
    hoverEnabled: true
    Accessible.role: Accessible.List
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Plugin rack")
    Accessible.description: qsTr("%1 ordered plugin instances. %2")
        .arg(instanceCount).arg(_status)

    function indexOf(instanceId) {
        for (let i = 0; i < root.instances.length; ++i)
            if (String(root.instances[i].instanceId) === String(instanceId)) return i
        return -1
    }
    function instanceById(instanceId) {
        const index = root.indexOf(instanceId)
        return index < 0 ? null : root.instances[index]
    }
    function isActionPending(instanceId, action) {
        const ids = Object.keys(root._pendingActions)
        for (let i = 0; i < ids.length; ++i) {
            const p = root._pendingActions[ids[i]]
            if (p.instanceId === String(instanceId) && p.action === String(action)) return true
        }
        return false
    }
    function nextRequestId(instanceId, action) {
        root._serial = String(Number(root._serial) + 1)
        return "plugin-rack:" + (root.channelId || "chain") + ":" + String(instanceId || "new")
                + ":" + String(action) + ":" + root._serial
    }
    function requestAction(instanceId, action, payload) {
        const id = root.nextRequestId(instanceId, action)
        const next = Object.assign({}, root._pendingActions)
        next[id] = {instanceId: String(instanceId || ""), action: String(action)}
        root._pendingActions = next
        root._statusState = "pending"
        root._status = qsTr("Waiting for host · %1").arg(action)
        root.pluginActionRequested(id, String(instanceId || ""), String(action), payload || ({}))
        return id
    }
    function acknowledgeAction(requestId, state, message) {
        const key = String(requestId || ""), pending = root._pendingActions[key]
        if (!pending) return false
        const outcome = String(state)
        if (["accepted", "rejected", "error"].indexOf(outcome) < 0) return false
        const next = Object.assign({}, root._pendingActions)
        delete next[key]
        root._pendingActions = next
        root._statusState = outcome
        root._status = String(message || (outcome === "accepted" ? qsTr("Host accepted %1").arg(pending.action)
                                                 : qsTr("Host %1 %2").arg(outcome).arg(pending.action)))
        root.pluginActionAcknowledged(key, pending.instanceId, pending.action, outcome, root._status)
        return true
    }
    function requestAdd(pluginId, presetId) {
        if (!root.enabled || !String(pluginId).length) return ""
        return root.requestAction("", "add", {pluginId: String(pluginId), presetId: String(presetId || ""), channelId: root.channelId})
    }
    function requestReorder(instanceId, targetIndex) {
        const from = root.indexOf(instanceId), to = Math.max(0, Math.min(root.instanceCount - 1, Math.trunc(targetIndex)))
        if (!root.enabled || from < 0 || from === to || root.isActionPending(instanceId, "reorder")) return ""
        return root.requestAction(instanceId, "reorder", {fromIndex: from, targetIndex: to})
    }
    function requestBypass(instanceId, bypassed) {
        const instance = root.instanceById(instanceId)
        if (!root.enabled || !instance || root.isActionPending(instanceId, "bypass")) return ""
        return root.requestAction(instanceId, "bypass", {bypassed: !!bypassed})
    }
    function requestRemove(instanceId) {
        if (!root.enabled || !root.instanceById(instanceId) || root.isActionPending(instanceId, "remove")) return ""
        return root.requestAction(instanceId, "remove", {})
    }
    function requestEditor(instanceId) {
        const instance = root.instanceById(instanceId)
        if (!root.enabled || !instance || instance.editorAvailable === false) return ""
        return root.requestAction(instanceId, "open-editor", {pluginId: String(instance.pluginId)})
    }
    function requestParameterEdit(instanceId, parameter) {
        if (!root.enabled || !root.instanceById(instanceId) || !parameter || parameter.available === false) return ""
        return root.requestAction(instanceId, "edit-parameter", {
            parameterId: String(parameter.parameterId), displayValue: String(parameter.displayValue || "")
        })
    }
    function toggleParameters(instanceId) {
        root._expandedInstanceId = root._expandedInstanceId === String(instanceId) ? "" : String(instanceId)
    }
    function dropTargetIndex(y) {
        const box = list.contentItem.mapToItem(list, 0, Number(y))
        return Math.max(0, Math.min(root.instanceCount, Math.floor(box.y / Math.max(1, root.reorderDistance))))
    }
    function stateColor(state) {
        const value = String(state || "")
        if (["failed", "error", "missing", "unavailable"].indexOf(value) >= 0) return Tk.Theme.color.danger
        if (["loading", "scanning", "pending"].indexOf(value) >= 0) return Tk.Theme.color.warning
        return Tk.Theme.color.success
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
            Tk.Overline { title: qsTr("INSERT CHAIN") }
            Tk.Spacer { }
            Tk.Caption { text: root.channelId || qsTr("Unassigned chain"); color: Tk.Theme.color.textMuted }
        }
        Tk.Caption {
            objectName: "pluginRackStatus"
            visible: root._status.length > 0
            text: root._status
            color: root.stateColor(root._statusState)
        }
        Item {
            id: listFrame
            objectName: "pluginRackListFrame"
            width: parent.width
            height: parent.height
            implicitHeight: Tk.Theme.size.panelMinHeight
            Tk.Flex.grow: 1
            ListView {
                id: list
                objectName: "pluginRackInstances"
                anchors.fill: parent
                clip: true
                model: root.instances || []
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                Accessible.role: Accessible.List
                Accessible.name: root.Accessible.name
                delegate: Item {
                    id: slot
                    required property int index
                    required property var modelData
                    property real dragDelta: 0
                    readonly property string instanceId: String(modelData.instanceId)
                    readonly property bool expanded: root._expandedInstanceId === instanceId
                    readonly property int baseHeight: Tk.Theme.size.control * 2 + Tk.Theme.space.xs * 2
                    objectName: "pluginSlot_" + instanceId
                    width: list.width
                    height: baseHeight + (expanded ? Tk.Theme.size.control * (modelData.parameters || []).length : 0)
                    Rectangle {
                        anchors.fill: parent
                        color: slot.index % 2 ? Tk.Theme.color.surface : Tk.Theme.color.panelAlt
                        radius: Tk.Theme.radius.sm
                        border.width: modelData.error ? Tk.Theme.size.border : 0
                        border.color: Tk.Theme.color.danger
                    }
                    Tk.Flex {
                        anchors.fill: parent
                        padding: Tk.Theme.space.xs
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Flex {
                            width: parent.width
                            gap: Tk.Theme.space.sm
                            Tk.Icon {
                                name: "grip-vertical"
                                size: Tk.Theme.size.iconSm
                                color: Tk.Theme.color.textMuted
                                Accessible.ignored: true
                            }
                            Tk.Label {
                                width: Tk.Theme.size.fieldWidth
                                text: String(modelData.name || modelData.pluginId || instanceId)
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }
                            Tk.Caption {
                                width: Tk.Theme.size.fieldWidth
                                text: String(modelData.vendor || "")
                                color: Tk.Theme.color.textMuted
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }
                            Tk.Spacer { }
                            Tk.Badge {
                                text: String(modelData.state || "ready")
                                variant: root.stateColor(modelData.state) === Tk.Theme.color.danger ? "danger"
                                       : root.stateColor(modelData.state) === Tk.Theme.color.warning ? "warning" : "success"
                                tooltip: modelData.error ? String(modelData.error) : qsTr("Plugin host state")
                            }
                            Tk.Mono {
                                text: qsTr("%1 frames").arg(modelData.latencyFrames === undefined ? "—" : String(modelData.latencyFrames))
                                color: Tk.Theme.color.textMuted
                                Accessible.description: qsTr("Host-reported plugin latency")
                            }
                        }
                        Tk.Flex {
                            width: parent.width
                            gap: Tk.Theme.space.xs
                            Tk.Switch {
                                objectName: "pluginBypass_" + slot.instanceId
                                text: qsTr("Bypass")
                                tooltip: qsTr("Request host bypass for %1").arg(modelData.name || instanceId)
                                checked: !!modelData.bypassed
                                enabled: modelData.available !== false && !root.isActionPending(slot.instanceId, "bypass")
                                onToggled: function(checked) { root.requestBypass(slot.instanceId, checked) }
                            }
                            Tk.Button {
                                text: qsTr("Parameters (%1)").arg((modelData.parameters || []).length)
                                tooltip: qsTr("Show host-supplied parameter IDs and values")
                                onClicked: root.toggleParameters(slot.instanceId)
                            }
                            Tk.Button {
                                text: qsTr("Editor")
                                tooltip: qsTr("Request the host-owned plugin editor")
                                enabled: modelData.editorAvailable !== false
                                onClicked: root.requestEditor(slot.instanceId)
                            }
                            Tk.Spacer { }
                            Tk.Button {
                                text: qsTr("↑")
                                tooltip: qsTr("Move plugin earlier in the host chain")
                                enabled: slot.index > 0
                                onClicked: root.requestReorder(slot.instanceId, slot.index - 1)
                            }
                            Tk.Button {
                                text: qsTr("↓")
                                tooltip: qsTr("Move plugin later in the host chain")
                                enabled: slot.index + 1 < root.instanceCount
                                onClicked: root.requestReorder(slot.instanceId, slot.index + 1)
                            }
                            Tk.Button {
                                text: qsTr("Remove")
                                tooltip: qsTr("Request host removal; saved state remains host-owned")
                                variant: "danger"
                                onClicked: root.requestRemove(slot.instanceId)
                            }
                        }
                        Repeater {
                            model: slot.expanded ? (modelData.parameters || []) : []
                            delegate: Tk.Flex {
                                required property var modelData
                                width: parent.width
                                gap: Tk.Theme.space.sm
                                Tk.Label {
                                    width: Tk.Theme.size.fieldWidth
                                    text: String(modelData.name || modelData.parameterId)
                                    elide: Text.ElideRight
                                    Accessible.ignored: true
                                }
                                Tk.Mono {
                                    text: String(modelData.displayValue || "—")
                                    color: Tk.Theme.color.textMuted
                                    Accessible.ignored: true
                                }
                                Tk.Spacer { }
                                Tk.Button {
                                    text: qsTr("Edit")
                                    tooltip: qsTr("Request an edit using this stable parameter ID")
                                    enabled: modelData.available !== false
                                    onClicked: root.requestParameterEdit(slot.instanceId, modelData)
                                }
                            }
                        }
                    }
                    DragHandler {
                        id: reorderDrag
                        target: null
                        enabled: root.enabled && !root.isActionPending(slot.instanceId, "reorder")
                        onActiveChanged: {
                            if (active) slot.dragDelta = 0
                            else if (Math.abs(slot.dragDelta) >= root.reorderDistance)
                                root.requestReorder(slot.instanceId,
                                    slot.index + Math.round(slot.dragDelta / Math.max(1, root.reorderDistance)))
                        }
                        onTranslationChanged: if (active) slot.dragDelta = translation.y
                    }
                    Accessible.role: Accessible.ListItem
                    Accessible.name: String(modelData.name || instanceId)
                    Accessible.description: qsTr("State %1, latency %2 frames%3. Stable plugin instance %4.")
                        .arg(modelData.state || "ready")
                        .arg(modelData.latencyFrames === undefined ? "unknown" : String(modelData.latencyFrames))
                        .arg(modelData.error ? qsTr(", error: %1").arg(modelData.error) : "")
                        .arg(instanceId)
                }
            }
            DropArea {
                objectName: "pluginRackDropTarget"
                anchors.fill: parent
                keys: ["application/x-qindatk-plugin-id"]
                onDropped: function(drop) {
                    if (drop.source && drop.source.pluginId)
                        root.requestAdd(drop.source.pluginId, drop.source.presetId)
                }
            }
            Tk.EmptyState {
                anchors.fill: parent
                visible: root.instanceCount === 0
                text: root.emptyText
                title: qsTr("No plugins")
                iconName: "audio-lines"
            }
        }
    }
    Tk.ToolTip { text: root.tooltip; visible: root.hovered && root.tooltip.length > 0 }
}
