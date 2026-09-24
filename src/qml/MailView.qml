// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Host owns message data and applies each explicit operation request.
Tk.Box {
    id: control
    objectName: "mailView"
    property var messages: []
    property string selectedMessageId: ""
    readonly property var selectedMessage: {
        for (let i = 0; i < messages.length; ++i)
            if (String(messages[i].messageId || "") === selectedMessageId) return messages[i]
        return null
    }
    property string syncState: "idle" // idle, syncing, offline, error
    property string errorText: ""
    property bool offline: false
    property string tooltip: qsTr("Mail messages")
    property string lastRequestId: ""
    property int _requestSerial: 0
    property string requestState: "idle"
    property string requestMessage: ""
    signal messageSelected(string messageId)
    signal refreshRequested(string requestId)
    signal actionRequested(string requestId, string messageId, string action)

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: offline ? qsTr("Offline. Cached messages remain available.") : errorText

    function requestRefresh() {
        if (offline) return false
        const id = "mail-refresh-" + Date.now() + "-" + (++_requestSerial)
        lastRequestId = id; requestState = "pending"; requestMessage = ""
        refreshRequested(id)
        return id
    }
    function requestAction(messageId, action) {
        if (!messageId || offline) return false
        const id = "mail-" + action + "-" + Date.now() + "-" + (++_requestSerial)
        lastRequestId = id; requestState = "pending"; requestMessage = ""
        actionRequested(id, messageId, action)
        return true
    }
    function acknowledge(requestId, state, message) {
        if (requestId !== lastRequestId || requestState !== "pending") return false
        requestState = state; requestMessage = message || ""
        return true
    }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Heading { level: 2; text: qsTr("Mail"); Tk.Flex.grow: 1 }
            Tk.Badge { text: control.offline ? qsTr("Offline · cached") : control.syncState; variant: control.offline ? "warning" : "default" }
            Tk.Button { objectName: "mailRefresh"; text: qsTr("Refresh"); tooltip: qsTr("Request mail refresh"); enabled: !control.offline && control.syncState !== "syncing"; onClicked: control.requestRefresh() }
        }
        Tk.Notice { objectName: "mailError"; visible: control.offline || control.syncState === "error" || control.requestState === "rejected"; variant: control.offline ? "warning" : "danger"; title: control.offline ? qsTr("Offline") : qsTr("Mail operation failed"); text: control.requestMessage || control.errorText || qsTr("Refresh is unavailable. Cached messages are still readable.") }
        ListView {
            id: list; objectName: "mailMessageList"; width: parent.width
            implicitHeight: Math.min(Tk.Theme.size.fieldWidth * 1.2, Math.max(Tk.Theme.size.control, contentHeight)); height: implicitHeight
            Tk.Flex.basis: implicitHeight; Tk.Flex.shrink: 0
            clip: true; spacing: Tk.Theme.space.xs; model: control.messages
            Accessible.role: Accessible.List; Accessible.name: qsTr("Messages")
            delegate: Tk.Button {
                required property var modelData
                property string messageId: String(modelData.messageId || "")
                objectName: "mailMessage_" + messageId
                width: list.width; text: (modelData.unread ? "● " : "") + String(modelData.sender || "") + " · " + String(modelData.subject || "")
                tooltip: String(modelData.snippet || modelData.subject || qsTr("Open message")); variant: control.selectedMessageId === messageId ? "outline" : "ghost"
                Accessible.role: Accessible.ListItem; Accessible.name: text; Accessible.description: tooltip
                onClicked: { control.selectedMessageId = messageId; control.messageSelected(messageId) }
            }
        }
        Tk.Box {
            objectName: "mailReader"
            visible: !!control.selectedMessage
            width: parent.width; padding: Tk.Theme.space.sm
            color: Tk.Theme.color.panelAlt; borderColor: Tk.Theme.color.divider
            Accessible.role: Accessible.Document
            Accessible.name: control.selectedMessage ? String(control.selectedMessage.subject || qsTr("Message")) : qsTr("No message selected")
            Accessible.description: qsTr("Message body rendered as host-provided plain text.")
            Tk.Flex {
                direction: Tk.Flex.Column; gap: Tk.Theme.space.xs; width: parent.width
                Tk.Label { text: String(control.selectedMessage ? control.selectedMessage.sender || "" : ""); muted: true }
                Tk.Label { text: String(control.selectedMessage ? control.selectedMessage.subject || "" : ""); font.weight: Font.DemiBold }
                Tk.Label {
                    objectName: "mailReaderBody"
                    text: String(control.selectedMessage ? control.selectedMessage.bodyPlain || control.selectedMessage.snippet || "" : "")
                    wrapMode: Text.Wrap; Accessible.role: Accessible.StaticText
                }
                Tk.Caption { text: qsTr("Body supplied as plain text by host · %1").arg(String(control.selectedMessage ? control.selectedMessage.bodyTrustMode || "untrusted" : "")) }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.xs
            Tk.Button { objectName: "mailArchive"; text: qsTr("Archive"); tooltip: qsTr("Request archive for selected message"); enabled: !!control.selectedMessageId && !control.offline; onClicked: control.requestAction(control.selectedMessageId, "archive") }
            Tk.Button { objectName: "mailDelete"; text: qsTr("Delete"); tooltip: qsTr("Request delete for selected message"); enabled: !!control.selectedMessageId && !control.offline; onClicked: control.requestAction(control.selectedMessageId, "delete") }
            Tk.Label { objectName: "mailRequestState"; text: control.requestMessage || control.requestState; muted: true; Tk.Flex.grow: 1 }
        }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
