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
    readonly property alias richDocument: bodyDocument
    property bool online: false
    property string status: "draft" // draft, pending, accepted, rejected, offline
    property string statusMessage: ""
    property string tooltip: qsTr("Compose message")
    property string lastRequestId: ""
    property int _requestSerial: 0
    property bool _loadingDocument: false
    signal draftChangeRequested(string field, var value)
    signal saveDraftRequested(string requestId, var draft)
    signal sendRequested(string requestId, var draft)

    Tk.RichTextDocument {
        id: bodyDocument
    }

    function uuid() {
        return "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".replace(/[xy]/g, function(c) {
            const r = Math.random() * 16 | 0
            return (c === "x" ? r : (r & 3 | 8)).toString(16)
        })
    }
    function plainDocumentJson(text) {
        const paragraphs = String(text || "").replace(/\r\n?/g, "\n").split("\n")
        const blocks = paragraphs.map(function(value) {
            return { paragraph: { id: control.uuid(), text: value,
                                  spans: value.length ? [[value.length, 0]] : [], mark: 0 } }
        })
        return JSON.stringify({ schema: "qindatk.richtext", version: 1, documentId: control.uuid(),
            page: { widthPt: 612, heightPt: 792, marginsPt: [72, 72, 72, 72] },
            defaults: { family: Tk.Theme.font.family, sizePt: Tk.Theme.font.body * 72 / 96 },
            formats: [{}], lists: [], objects: [], blocks: blocks })
    }
    function snapshot() {
        return { to: String(workingDraft.to || ""), cc: String(workingDraft.cc || ""),
                 subject: String(workingDraft.subject || ""), body: bodyDocument.text(),
                 richDocumentJson: bodyDocument.toJson(), attachments: workingDraft.attachments || [] }
    }
    function loadDraft(value) {
        const next = value || ({})
        const json = String(next.richDocumentJson || "")
        const incomingBody = String(next.body || "")
        const currentJson = bodyDocument.toJson()
        _loadingDocument = true
        if (json.length > 0 && json !== currentJson) {
            if (!bodyDocument.loadJson(json)) bodyDocument.loadJson(plainDocumentJson(incomingBody))
        } else if (json.length === 0 && bodyDocument.text() !== incomingBody) {
            bodyDocument.loadJson(plainDocumentJson(incomingBody))
        }
        _loadingDocument = false
        workingDraft = { to: String(next.to || ""), cc: String(next.cc || ""),
                         subject: String(next.subject || ""), body: bodyDocument.text(),
                         richDocumentJson: bodyDocument.toJson(), attachments: next.attachments || [] }
    }
    function editDraft(field, value) {
        if (field === "body" && String(value) !== bodyDocument.text()) {
            _loadingDocument = true
            bodyDocument.loadJson(plainDocumentJson(String(value)))
            _loadingDocument = false
            value = bodyDocument.text()
        }
        const next = snapshot()
        next[field] = value
        workingDraft = next
        draftChangeRequested(field, value)
    }
    function syncBodySnapshot() {
        if (_loadingDocument) return
        const next = snapshot()
        workingDraft = next
        draftChangeRequested("body", next.body)
    }
    function requestSave() { const id = "draft-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; status = "pending"; saveDraftRequested(id, snapshot()); return id }
    function requestSend() { if (!validDraft || !online) return false; const id = "send-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; status = "pending"; sendRequested(id, snapshot()); return true }
    function acknowledge(id, state, message) { if (id !== lastRequestId || status !== "pending") return false; status = state; statusMessage = message || ""; return true }

    onDraftChanged: loadDraft(draft)
    Component.onCompleted: loadDraft(draft)
    Connections {
        target: bodyDocument
        function onChanged() { control.syncBodySnapshot() }
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: control.statusMessage
    readonly property bool hasRecipient: String(workingDraft.to || "").trim().length > 0
    readonly property bool validDraft: hasRecipient && String(workingDraft.subject || "").trim().length > 0

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Heading { level: 2; text: qsTr("New message") }
        Tk.Notice { visible: !control.online || control.status === "rejected"; variant: !control.online ? "warning" : "danger"; title: !control.online ? qsTr("Offline draft") : qsTr("Request failed"); text: control.statusMessage || qsTr("This local fixture stores drafts only; it cannot deliver mail.") }
        Tk.TextField { objectName: "composerTo"; width: parent.width; placeholderText: qsTr("To · separate addresses with commas"); text: String(control.workingDraft.to || ""); tooltip: qsTr("Recipients"); Accessible.name: qsTr("To recipients"); onTextChanged: if (activeFocus) control.editDraft("to", text) }
        Tk.TextField { objectName: "composerCc"; width: parent.width; placeholderText: qsTr("Cc"); text: String(control.workingDraft.cc || ""); tooltip: qsTr("Cc recipients"); Accessible.name: qsTr("Cc recipients"); onTextChanged: if (activeFocus) control.editDraft("cc", text) }
        Tk.TextField { objectName: "composerSubject"; width: parent.width; placeholderText: qsTr("Subject"); text: String(control.workingDraft.subject || ""); tooltip: qsTr("Message subject"); Accessible.name: qsTr("Subject"); onTextChanged: if (activeFocus) control.editDraft("subject", text) }
        Tk.FormatBar { objectName: "composerFormatBar"; target: composerBody; compact: true; width: parent.width }
        Tk.RichTextEditor { id: composerBody; objectName: "composerBody"; width: parent.width; rows: 5; placeholderText: qsTr("Write a message"); document: bodyDocument; tooltip: qsTr("Formatted message body"); Accessible.name: qsTr("Message body") }
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
