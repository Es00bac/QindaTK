// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Draft fields are host snapshots. Sending is only a request; this view never sends.
// AGENT-CONTRACT (docs/controls.md, examples/local-productivity/Main.qml):
// recipients are TokenField tokens validated by MailAddressValidator (the
// rules SmtpMailProvider sends with) and published as the draft's `to`/`cc`
// strings; attachments change only through acknowledged attachment jobs.
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
    // Presentation for hosts that give the composer a window of its own
    // (QindaMail): the heading, the Save/Send row and the Bcc row are
    // optional and the body can be given more rows.
    property string title: qsTr("New message")
    property bool headerVisible: true
    property bool actionsVisible: true
    property bool bccVisible: false
    property int bodyRows: 5
    property string sendText: qsTr("Send")
    property string lastRequestId: ""
    property int _requestSerial: 0
    property bool _loadingDocument: false
    // Pending and failed attachment jobs: {requestId, kind, attachmentId, state, message}.
    property var attachmentJobs: []
    readonly property alias toField: toTokens
    readonly property alias ccField: ccTokens
    readonly property alias bccField: bccTokens
    readonly property string recipientError: recipientProblem(toTokens.tokens, ccTokens.tokens.concat(bccTokens.tokens))
    readonly property bool hasRecipient: toTokens.tokens.some(function(token) { return token.valid !== false })
    readonly property bool validDraft: hasRecipient && recipientError.length === 0
                                       && String(workingDraft.subject || "").trim().length > 0
    signal draftChangeRequested(string field, var value)
    signal saveDraftRequested(string requestId, var draft)
    signal sendRequested(string requestId, var draft)
    signal attachmentAddRequested(string requestId)
    signal attachmentRemoveRequested(string requestId, string attachmentId)

    Tk.RichTextDocument { id: bodyDocument }
    Tk.MailAddressValidator { id: addresses }

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
    function checkRecipient(text) {
        const parsed = addresses.parseOne(text)
        return { label: parsed.display, value: parsed.address, valid: parsed.valid, error: parsed.error }
    }
    function recipientTokens(text) {
        return addresses.parseList(String(text || "")).recipients.map(function(entry) {
            return { id: control.uuid(), label: entry.display, value: entry.address, valid: entry.valid }
        })
    }
    function recipientText(tokens) { return tokens.map(function(token) { return token.label }).join(", ") }
    function recipientProblem(to, cc) {
        const invalid = to.concat(cc).filter(function(token) { return token.valid === false })
        return invalid.length ? qsTr("Invalid recipient: %1").arg(invalid.map(function(token) { return token.label }).join(", ")) : ""
    }
    function snapshot() {
        return { to: String(workingDraft.to || ""), cc: String(workingDraft.cc || ""), bcc: String(workingDraft.bcc || ""),
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
        // AGENT-GUARD: republishing the same recipients must keep the token
        // objects (and the focused input) in place; only a changed list reloads.
        if (recipientText(toTokens.tokens) !== String(next.to || "")) toTokens.tokens = recipientTokens(next.to)
        if (recipientText(ccTokens.tokens) !== String(next.cc || "")) ccTokens.tokens = recipientTokens(next.cc)
        if (recipientText(bccTokens.tokens) !== String(next.bcc || "")) bccTokens.tokens = recipientTokens(next.bcc)
        if (String(next.bcc || "").length > 0) bccVisible = true
        workingDraft = { to: String(next.to || ""), cc: String(next.cc || ""), bcc: String(next.bcc || ""),
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
        if (field === "to" || field === "cc") {
            const field_ = field === "to" ? toTokens : ccTokens
            if (typeof value === "string" && recipientText(field_.tokens) !== value) field_.tokens = recipientTokens(value)
            value = recipientText(field_.tokens)
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
    function commitRecipientDrafts() {
        if (toTokens.draft.trim().length) toTokens.commitDraft()
        if (ccTokens.draft.trim().length) ccTokens.commitDraft()
        if (bccTokens.draft.trim().length) bccTokens.commitDraft()
    }
    function nextId(prefix) { return prefix + "-" + Date.now() + "-" + (++_requestSerial) }
    function requestSave() { commitRecipientDrafts(); const id = nextId("draft"); lastRequestId = id; status = "pending"; saveDraftRequested(id, snapshot()); return id }
    function requestSend() { commitRecipientDrafts(); if (!validDraft || !online) return false; const id = nextId("send"); lastRequestId = id; status = "pending"; sendRequested(id, snapshot()); return true }
    function acknowledge(id, state, message) { if (id !== lastRequestId || status !== "pending") return false; status = state; statusMessage = message || ""; return true }
    function requestAttachmentAdd() {
        const id = nextId("attachment-add")
        attachmentJobs = attachmentJobs.concat([{ requestId: id, kind: "add", attachmentId: "", state: "pending", message: "" }])
        attachmentAddRequested(id)
        return id
    }
    function requestAttachmentRemove(attachmentId) {
        if (!attachmentId) return false
        const id = nextId("attachment-remove")
        attachmentJobs = attachmentJobs.concat([{ requestId: id, kind: "remove", attachmentId: attachmentId, state: "pending", message: "" }])
        attachmentRemoveRequested(id, attachmentId)
        return id
    }
    // `attachment` is the saved metadata ({id, name, mimeType, size, state})
    // for an accepted add. A failed job leaves the draft's attachments as they were.
    function acknowledgeAttachment(requestId, state, message, attachment) {
        const job = attachmentJobs.find(function(entry) { return entry.requestId === requestId && entry.state === "pending" })
        if (!job) return false
        const rest = attachmentJobs.filter(function(entry) { return entry.requestId !== requestId })
        if (state !== "accepted") {
            attachmentJobs = rest.concat([Object.assign({}, job, { state: state, message: message || "" })])
            return true
        }
        attachmentJobs = rest
        let list = (workingDraft.attachments || []).slice()
        if (job.kind === "add" && attachment && attachment.id)
            list = list.filter(function(entry) { return entry.id !== attachment.id }).concat([attachment])
        else if (job.kind === "remove")
            list = list.filter(function(entry) { return entry.id !== job.attachmentId })
        editDraft("attachments", list)
        return true
    }
    function dismissAttachmentJob(requestId) { attachmentJobs = attachmentJobs.filter(function(entry) { return entry.requestId !== requestId }) }

    onDraftChanged: loadDraft(draft)
    Component.onCompleted: loadDraft(draft)
    Connections {
        target: bodyDocument
        function onChanged() { control.syncBodySnapshot() }
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: control.recipientError || control.statusMessage

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Heading { objectName: "composerTitle"; visible: control.headerVisible; level: 2; text: control.title }
        Tk.Notice { visible: !control.online || control.status === "rejected"; variant: !control.online ? "warning" : "danger"; title: !control.online ? qsTr("Offline draft") : qsTr("Request failed"); text: control.statusMessage || qsTr("Drafts are kept locally until the host can deliver them.") }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center; width: parent.width
            Tk.Label { text: qsTr("To"); muted: true; width: Tk.Theme.size.controlLg }
            Tk.TokenField {
                id: toTokens; objectName: "composerTo"; Tk.Flex.grow: 1; Tk.Flex.basis: 0
                standalone: true; label: qsTr("To recipients"); placeholderText: qsTr("Add recipient"); tooltip: qsTr("Recipients · press comma or Enter after each address")
                validator: control.checkRecipient
                onTokensEdited: function(tokens) { control.editDraft("to", control.recipientText(tokens)) }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center; width: parent.width
            Tk.Label { text: qsTr("Cc"); muted: true; width: Tk.Theme.size.controlLg }
            Tk.TokenField {
                id: ccTokens; objectName: "composerCc"; Tk.Flex.grow: 1; Tk.Flex.basis: 0
                standalone: true; label: qsTr("Cc recipients"); placeholderText: qsTr("Add Cc recipient"); tooltip: qsTr("Cc recipients")
                validator: control.checkRecipient
                onTokensEdited: function(tokens) { control.editDraft("cc", control.recipientText(tokens)) }
            }
        }
        Tk.Flex {
            objectName: "composerBccRow"
            visible: control.bccVisible
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center; width: parent.width
            Tk.Label { text: qsTr("Bcc"); muted: true; width: Tk.Theme.size.controlLg }
            Tk.TokenField {
                id: bccTokens; objectName: "composerBcc"; Tk.Flex.grow: 1; Tk.Flex.basis: 0
                standalone: true; label: qsTr("Bcc recipients"); placeholderText: qsTr("Add Bcc recipient"); tooltip: qsTr("Blind copies: delivered, never shown to the other recipients")
                validator: control.checkRecipient
                onTokensEdited: function(tokens) { control.editDraft("bcc", control.recipientText(tokens)) }
            }
        }
        Tk.Caption { objectName: "composerRecipientError"; visible: control.recipientError.length > 0; text: control.recipientError; color: Tk.Theme.color.danger; wrapMode: Text.Wrap; width: parent.width }
        Tk.TextField { objectName: "composerSubject"; width: parent.width; placeholderText: qsTr("Subject"); text: String(control.workingDraft.subject || ""); tooltip: qsTr("Message subject"); Accessible.name: qsTr("Subject"); onTextChanged: if (activeFocus) control.editDraft("subject", text) }
        Tk.FormatBar { objectName: "composerFormatBar"; target: composerBody; compact: true; width: parent.width }
        Tk.RichTextEditor { id: composerBody; objectName: "composerBody"; width: parent.width; rows: control.bodyRows; placeholderText: qsTr("Write a message"); document: bodyDocument; tooltip: qsTr("Formatted message body"); Accessible.name: qsTr("Message body") }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center; width: parent.width
            Tk.Label { objectName: "composerAttachments"; text: qsTr("Attachments · %1").arg((control.workingDraft.attachments || []).length); muted: true; Tk.Flex.grow: 1 }
            Tk.Button { objectName: "composerAttach"; small: true; iconName: "file-plus"; text: qsTr("Attach"); tooltip: qsTr("Request an attachment from the host"); onClicked: control.requestAttachmentAdd() }
        }
        Repeater {
            model: control.workingDraft.attachments || []
            delegate: Tk.Flex {
                required property var modelData
                objectName: "composerAttachment_" + String(modelData.id || "")
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center; width: parent ? parent.width : 0
                Tk.Icon { name: "file"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
                Tk.Label { text: String(modelData.name || modelData.id || ""); elide: Text.ElideMiddle; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                Tk.Caption { text: [modelData.mimeType, modelData.size ? qsTr("%1 bytes").arg(modelData.size) : "", modelData.state].filter(Boolean).join(" · "); color: Tk.Theme.color.textMuted }
                Tk.IconButton { objectName: "composerAttachmentRemove"; small: true; iconName: "x"; tooltip: qsTr("Remove %1").arg(String(modelData.name || "")); onClicked: control.requestAttachmentRemove(String(modelData.id || "")) }
            }
        }
        Repeater {
            model: control.attachmentJobs
            delegate: Tk.Flex {
                required property var modelData
                objectName: "composerAttachmentJob"
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center; width: parent ? parent.width : 0
                Tk.Spinner { visible: modelData.state === "pending"; size: Tk.Theme.size.iconSm }
                Tk.Caption {
                    Tk.Flex.grow: 1; Tk.Flex.basis: 0; wrapMode: Text.Wrap
                    color: modelData.state === "pending" ? Tk.Theme.color.textMuted : Tk.Theme.color.danger
                    text: modelData.state === "pending" ? (modelData.kind === "add" ? qsTr("Adding attachment…") : qsTr("Removing attachment…"))
                                                         : qsTr("Attachment %1 failed: %2").arg(modelData.kind).arg(modelData.message || modelData.state)
                }
                Tk.IconButton { visible: modelData.state !== "pending"; small: true; iconName: "x"; tooltip: qsTr("Dismiss"); onClicked: control.dismissAttachmentJob(modelData.requestId) }
            }
        }
        Tk.Flex {
            visible: control.actionsVisible
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
            Tk.Label { objectName: "composerStatus"; text: control.statusMessage || control.status; muted: true; Tk.Flex.grow: 1 }
            Tk.Button { objectName: "composerSave"; text: qsTr("Save draft"); tooltip: qsTr("Keep this draft on this computer"); onClicked: control.requestSave() }
            Tk.Button { objectName: "composerSend"; text: control.sendText; iconName: "send"; variant: "accent"; tooltip: control.recipientError || qsTr("Send once every recipient is valid"); enabled: control.validDraft && control.online; onClicked: control.requestSend() }
        }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
