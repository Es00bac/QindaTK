// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Draft fields are host snapshots. Sending is only a request; this view never sends.
Tk.Box {
    id: control
    objectName: "richMailComposer"
    property var draft: ({ to: "", cc: "", subject: "", body: "", attachments: [] })
    property var workingDraft: ({})
    property bool online: false
    property string status: "draft" // draft, pending, accepted, rejected, offline
    property string statusMessage: ""
    property string tooltip: qsTr("Compose message")
    property string lastRequestId: ""
    property int _requestSerial: 0
    signal draftChangeRequested(string field, var value)
    signal saveDraftRequested(string requestId, var draft)
    signal sendRequested(string requestId, var draft)

    onDraftChanged: workingDraft = ({ to: String(draft.to || ""), cc: String(draft.cc || ""), subject: String(draft.subject || ""), body: String(draft.body || ""), attachments: draft.attachments || [] })
    Component.onCompleted: workingDraft = ({ to: String(draft.to || ""), cc: String(draft.cc || ""), subject: String(draft.subject || ""), body: String(draft.body || ""), attachments: draft.attachments || [] })

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: control.statusMessage
    readonly property bool hasRecipient: String(workingDraft.to || "").trim().length > 0
    readonly property bool validDraft: hasRecipient && String(workingDraft.subject || "").trim().length > 0
    function snapshot() { return { to: String(workingDraft.to || ""), cc: String(workingDraft.cc || ""), subject: String(workingDraft.subject || ""), body: String(workingDraft.body || ""), attachments: workingDraft.attachments || [] } }
    function editDraft(field, value) { const next = snapshot(); next[field] = value; workingDraft = next; draftChangeRequested(field, value) }
    function requestSave() { const id = "draft-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; status = "pending"; saveDraftRequested(id, snapshot()); return id }
    function requestSend() { if (!validDraft || !online) return false; const id = "send-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; status = "pending"; sendRequested(id, snapshot()); return true }
    function acknowledge(id, state, message) { if (id !== lastRequestId || status !== "pending") return false; status = state; statusMessage = message || ""; return true }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Heading { level: 2; text: qsTr("New message") }
        Tk.Notice { visible: !control.online || control.status === "rejected"; variant: !control.online ? "warning" : "danger"; title: !control.online ? qsTr("Offline draft") : qsTr("Request failed"); text: control.statusMessage || qsTr("This local fixture stores drafts only; it cannot deliver mail.") }
        Tk.TextField { objectName: "composerTo"; width: parent.width; placeholderText: qsTr("To · separate addresses with commas"); text: String(control.workingDraft.to || ""); tooltip: qsTr("Recipients"); Accessible.name: qsTr("To recipients"); onTextChanged: if (activeFocus) control.editDraft("to", text) }
        Tk.TextField { objectName: "composerCc"; width: parent.width; placeholderText: qsTr("Cc"); text: String(control.workingDraft.cc || ""); tooltip: qsTr("Cc recipients"); Accessible.name: qsTr("Cc recipients"); onTextChanged: if (activeFocus) control.editDraft("cc", text) }
        Tk.TextField { objectName: "composerSubject"; width: parent.width; placeholderText: qsTr("Subject"); text: String(control.workingDraft.subject || ""); tooltip: qsTr("Message subject"); Accessible.name: qsTr("Subject"); onTextChanged: if (activeFocus) control.editDraft("subject", text) }
        Tk.TextArea { objectName: "composerBody"; width: parent.width; rows: 5; placeholderText: qsTr("Write a message"); text: String(control.workingDraft.body || ""); tooltip: qsTr("Message body"); Accessible.name: qsTr("Message body"); onTextChanged: if (activeFocus) control.editDraft("body", text) }
        Tk.Label { objectName: "composerAttachments"; text: qsTr("Attachments: %1 · host managed").arg((control.workingDraft.attachments || []).length); muted: true }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
            Tk.Label { text: control.statusMessage || control.status; muted: true; Tk.Flex.grow: 1 }
            Tk.Button { objectName: "composerSave"; text: qsTr("Save draft"); tooltip: qsTr("Request local draft save"); onClicked: control.requestSave() }
            Tk.Button { objectName: "composerSend"; text: qsTr("Request send"); variant: "accent"; tooltip: qsTr("Request host delivery after validation"); enabled: control.validDraft && control.online; onClicked: control.requestSend() }
        }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
