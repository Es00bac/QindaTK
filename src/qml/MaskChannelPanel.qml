// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.Box {
    id: control
    objectName: "maskChannelPanel"
    property string documentId: ""
    property var channels: [] // up to 200 {id,name,kind,enabled,visible,linkedTo}
    property string activeTargetId: ""
    property string requestState: "idle"
    property string requestMessage: ""
    property string tooltip: qsTr("Mask and channel targets")
    property string lastRequestId: ""
    property int _requestSerial: 0

    signal targetRequested(string requestId, string documentId, string targetId)
    signal channelStateRequested(string requestId, string documentId, string channelId, string propertyName, bool value)
    signal relationshipChangeRequested(string requestId, string documentId, string channelId, string relationship, string targetId)

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: requestMessage

    function requestTarget(targetId) {
        if (!documentId || !channels.some(function(row) { return String(row.id) === targetId })) return false
        const requestId = documentId + ":target:" + (++_requestSerial)
        lastRequestId = requestId; requestState = "pending"; requestMessage = ""
        targetRequested(requestId, documentId, targetId)
        return requestId
    }
    function requestChannelState(channelId, propertyName) {
        const row = channels.find(function(item) { return String(item.id) === channelId })
        if (!row || (propertyName !== "enabled" && propertyName !== "visible")) return false
        const requestId = documentId + ":channel:" + (++_requestSerial)
        lastRequestId = requestId; requestState = "pending"; requestMessage = ""
        channelStateRequested(requestId, documentId, channelId, propertyName, !row[propertyName])
        return requestId
    }
    function acknowledge(requestId, state, message) {
        if (requestId !== lastRequestId || requestState !== "pending") return false
        requestState = state; requestMessage = message || ""
        return true
    }
    function requestRelationship(channelId, relationship, targetId) {
        const row = channels.find(function(item) { return String(item.id) === channelId })
        const targetExists = channels.some(function(item) { return String(item.id) === targetId })
        if (!row || !documentId || (relationship !== "clipping" && relationship !== "unlink")
                || (relationship === "clipping" && (!targetExists || targetId === channelId))) return false
        const requestId = documentId + ":relationship:" + (++_requestSerial)
        lastRequestId = requestId; requestState = "pending"; requestMessage = ""
        relationshipChangeRequested(requestId, documentId, channelId, relationship, targetId)
        return requestId
    }

    Tk.Flex {
        width: parent.width
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            Tk.Overline { title: qsTr("Mask and channels"); wide: true }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Badge { text: control.activeTargetId || qsTr("No paint target"); variant: "info" }
        }
        ListView {
            id: channelList
            objectName: "maskChannelList"
            width: parent.width
            height: Math.min(Tk.Theme.size.control * 6,
                             Math.max(Tk.Theme.size.control, channelList.model.length * Tk.Theme.size.control))
            implicitHeight: height
            clip: true
            model: (control.channels || []).slice(0, 200)
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Document masks and channels")
            delegate: Tk.Flex {
                required property var modelData
                width: channelList.width
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                Tk.Button {
                    objectName: "maskTarget_" + String(modelData.id)
                    Tk.Flex.grow: 1
                    text: String(modelData.name || modelData.id) + " · " + String(modelData.kind || qsTr("channel"))
                    variant: control.activeTargetId === String(modelData.id) ? "accent" : "outline"
                    tooltip: qsTr("Set %1 as the active paint target").arg(String(modelData.name || modelData.id))
                    Accessible.role: Accessible.RadioButton
                    Accessible.checked: control.activeTargetId === String(modelData.id)
                    onClicked: control.requestTarget(String(modelData.id))
                }
                Tk.Button {
                    objectName: "maskEnabled_" + String(modelData.id)
                    text: modelData.enabled === false ? qsTr("Off") : qsTr("On")
                    variant: modelData.enabled === false ? "ghost" : "outline"
                    tooltip: qsTr("Request channel enabled state")
                    Accessible.name: qsTr("%1 enabled").arg(String(modelData.name || modelData.id))
                    onClicked: control.requestChannelState(String(modelData.id), "enabled")
                }
                Tk.Button {
                    objectName: "maskVisible_" + String(modelData.id)
                    text: modelData.visible === false ? qsTr("Hidden") : qsTr("Shown")
                    variant: "ghost"
                    tooltip: qsTr("Request mask or channel visibility")
                    Accessible.name: qsTr("%1 visibility").arg(String(modelData.name || modelData.id))
                    onClicked: control.requestChannelState(String(modelData.id), "visible")
                }
                Tk.Button {
                    objectName: "maskRelationship_" + String(modelData.id)
                    text: modelData.linkedTo ? qsTr("Unlink") : qsTr("Clip to target")
                    tooltip: qsTr("Request clipping relationship with the active target")
                    enabled: !!modelData.linkedTo || (control.activeTargetId.length > 0 && control.activeTargetId !== String(modelData.id))
                    onClicked: control.requestRelationship(String(modelData.id), modelData.linkedTo ? "unlink" : "clipping", modelData.linkedTo ? "" : control.activeTargetId)
                }
            }
        }
        Tk.Caption { objectName: "maskChannelStatus"; text: control.requestState + (control.requestMessage.length ? " · " + control.requestMessage : ""); color: control.requestState === "rejected" ? Tk.Theme.color.warning : Tk.Theme.color.textMuted; wrapMode: Text.Wrap }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.hovered }
}
