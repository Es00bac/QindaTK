// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Host owns message data and applies each explicit operation request.
// AGENT-CONTRACT (docs/controls.md, ImapMailProvider): messages carry
// `threadId` (conversation), `attachments` [{id, name, mimeType, size,
// state "remote"|"local"}], `unread`, `flagged` and UTC `date`. The view
// groups conversations, asks for attachment bytes lazily and tracks each
// attachment job by request ID, so a late answer only changes the
// attachment it was asked for, never the reader of another message.
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
    property bool threaded: true
    property var expandedThreadIds: []
    property string syncState: "idle" // idle, syncing, offline, error
    property string errorText: ""
    property bool offline: false
    property string tooltip: qsTr("Mail messages")
    property string lastRequestId: ""
    property int _requestSerial: 0
    property string requestState: "idle"
    property string requestMessage: ""
    // Attachment jobs keyed by request ID: {messageId, attachmentId, state, message}.
    property var attachmentJobs: ({})
    signal messageSelected(string messageId)
    signal refreshRequested(string requestId)
    signal actionRequested(string requestId, string messageId, string action)
    signal replyRequested(string messageId)
    signal attachmentRequested(string requestId, string messageId, string attachmentId)
    signal attachmentOpenRequested(string messageId, string attachmentId)

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: offline ? qsTr("Offline. Cached messages remain available.") : errorText

    // Conversations, newest activity first; messages inside oldest first.
    readonly property var threads: {
        const groups = ({}), order = []
        for (let i = 0; i < messages.length; ++i) {
            const key = String(messages[i].threadId || messages[i].messageId || "")
            if (!groups[key]) { groups[key] = []; order.push(key) }
            groups[key].push(messages[i])
        }
        const byDate = function(a, b) { return String(a.date || "").localeCompare(String(b.date || "")) }
        return order.map(function(key, index) {
            const list = groups[key].slice().sort(byDate)
            const senders = []
            list.forEach(function(message) { const s = String(message.sender || ""); if (s && senders.indexOf(s) < 0) senders.push(s) })
            return { threadId: key, messages: list, order: index, subject: String(list[0].subject || ""),
                     latest: String(list[list.length - 1].date || ""), senders: senders,
                     unread: list.filter(function(message) { return !!message.unread }).length,
                     flagged: list.some(function(message) { return !!message.flagged }) }
        }).sort(function(a, b) { return a.latest === b.latest ? a.order - b.order : b.latest.localeCompare(a.latest) })
    }
    readonly property var rows: {
        const out = []
        if (!threaded) {
            messages.forEach(function(message) { out.push({ kind: "message", message: message, depth: 0 }) })
            return out
        }
        threads.forEach(function(thread) {
            if (thread.messages.length === 1) { out.push({ kind: "message", message: thread.messages[0], depth: 0 }); return }
            out.push({ kind: "thread", thread: thread })
            const holdsSelection = thread.messages.some(function(message) { return String(message.messageId || "") === control.selectedMessageId })
            if (holdsSelection || control.expandedThreadIds.indexOf(thread.threadId) >= 0)
                thread.messages.forEach(function(message) { out.push({ kind: "message", message: message, depth: 1 }) })
        })
        return out
    }

    function nextId(prefix) { return prefix + "-" + Date.now() + "-" + (++_requestSerial) }
    function formatDate(text) { const date = new Date(String(text || "")); return isNaN(date.getTime()) ? String(text || "") : Qt.formatDateTime(date, "yyyy-MM-dd hh:mm") }
    function toggleThread(threadId) {
        const thread = threads.find(function(entry) { return entry.threadId === threadId })
        // Collapsing a conversation that holds the selection clears the selection.
        if (thread && thread.messages.some(function(message) { return String(message.messageId || "") === selectedMessageId })) selectedMessageId = ""
        const next = expandedThreadIds.slice()
        const at = next.indexOf(threadId)
        if (at < 0) next.push(threadId); else next.splice(at, 1)
        expandedThreadIds = next
    }
    function selectMessage(messageId) {
        const message = messages.find(function(entry) { return String(entry.messageId || "") === messageId })
        if (!message) return false
        const threadId = String(message.threadId || message.messageId || "")
        if (expandedThreadIds.indexOf(threadId) < 0) expandedThreadIds = expandedThreadIds.concat([threadId])
        selectedMessageId = messageId
        messageSelected(messageId)
        return true
    }
    function requestRefresh() {
        if (offline) return false
        const id = nextId("mail-refresh")
        lastRequestId = id; requestState = "pending"; requestMessage = ""
        refreshRequested(id)
        return id
    }
    function requestAction(messageId, action) {
        if (!messageId || offline) return false
        const id = nextId("mail-" + action)
        lastRequestId = id; requestState = "pending"; requestMessage = ""
        actionRequested(id, messageId, action)
        return true
    }
    function acknowledge(requestId, state, message) {
        if (requestId !== lastRequestId || requestState !== "pending") return false
        requestState = state; requestMessage = message || ""
        return true
    }
    function attachmentState(messageId, attachment) {
        const jobs = Object.keys(attachmentJobs).map(function(key) { return attachmentJobs[key] })
            .filter(function(job) { return job.messageId === messageId && job.attachmentId === attachment.id })
        const job = jobs.length ? jobs[jobs.length - 1] : null
        if (attachment.state === "local") return "local"
        return job ? job.state : String(attachment.state || "remote")
    }
    // Asks the host for one attachment's bytes (lazy load). Local bytes open.
    function requestAttachment(messageId, attachmentId) {
        const message = messages.find(function(entry) { return String(entry.messageId || "") === messageId })
        const attachment = message ? (message.attachments || []).find(function(entry) { return entry.id === attachmentId }) : null
        if (!attachment) return false
        if (attachment.state === "local") { attachmentOpenRequested(messageId, attachmentId); return "open" }
        if (offline || attachmentState(messageId, attachment) === "loading") return false
        const id = nextId("mail-attachment")
        const jobs = Object.assign({}, attachmentJobs)
        jobs[id] = { messageId: messageId, attachmentId: attachmentId, state: "loading", message: "" }
        attachmentJobs = jobs
        attachmentRequested(id, messageId, attachmentId)
        return id
    }
    function acknowledgeAttachment(requestId, state, message) {
        const job = attachmentJobs[requestId]
        if (!job || job.state !== "loading") return false
        const jobs = Object.assign({}, attachmentJobs)
        jobs[requestId] = Object.assign({}, job, { state: state === "accepted" ? "local" : "failed", message: message || state })
        attachmentJobs = jobs
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
        Tk.Notice { objectName: "mailError"; visible: control.offline || control.syncState === "error" || control.requestState === "rejected" || control.requestState === "offline" || control.requestState === "conflict"; variant: control.offline || control.requestState === "offline" ? "warning" : "danger"; title: control.offline || control.requestState === "offline" ? qsTr("Offline") : control.requestState === "conflict" ? qsTr("Mailbox changed") : qsTr("Mail operation failed"); text: control.requestMessage || control.errorText || qsTr("Refresh is unavailable. Cached messages are still readable.") }
        ListView {
            id: list; objectName: "mailMessageList"; width: parent.width
            implicitHeight: Math.min(Tk.Theme.size.fieldWidth * 1.2, Math.max(Tk.Theme.size.control, contentHeight)); height: implicitHeight
            Tk.Flex.basis: implicitHeight; Tk.Flex.shrink: 0
            clip: true; model: control.rows
            Accessible.role: Accessible.List; Accessible.name: qsTr("Conversations")
            delegate: Tk.ListRow {
                required property var modelData
                readonly property bool isThread: modelData.kind === "thread"
                readonly property var message: isThread ? modelData.thread.messages[modelData.thread.messages.length - 1] : modelData.message
                readonly property string messageId: String(message.messageId || "")
                readonly property bool expanded: isThread && (control.expandedThreadIds.indexOf(modelData.thread.threadId) >= 0
                    || modelData.thread.messages.some(function(entry) { return String(entry.messageId || "") === control.selectedMessageId }))
                objectName: isThread ? "mailThread_" + modelData.thread.threadId : "mailMessage_" + messageId
                width: list.width
                indent: isThread ? 0 : modelData.depth * Tk.Theme.space.lg
                iconName: isThread ? (expanded ? "chevron-down" : "chevron-right") : (message.flagged ? "flag" : "")
                selected: !isThread && control.selectedMessageId === messageId
                active: isThread ? modelData.thread.unread > 0 : !!message.unread
                text: isThread ? qsTr("%1 · %2 messages").arg(modelData.thread.subject).arg(modelData.thread.messages.length)
                               : (message.unread ? "● " : "") + String(message.sender || "") + " · " + String(message.subject || "")
                secondaryText: isThread ? modelData.thread.senders.join(", ") + (modelData.thread.unread ? qsTr(" · %1 unread").arg(modelData.thread.unread) : "")
                                        : String(message.snippet || "")
                tooltip: isThread ? qsTr("Conversation: %1").arg(modelData.thread.subject) : String(message.subject || qsTr("Open message"))
                Accessible.role: isThread ? Accessible.TreeItem : Accessible.ListItem
                onClicked: isThread ? control.toggleThread(modelData.thread.threadId) : control.selectMessage(messageId)
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
                Tk.Label { objectName: "mailReaderSender"; text: String(control.selectedMessage ? control.selectedMessage.sender || "" : "") + (control.selectedMessage && control.selectedMessage.date ? " · " + control.formatDate(control.selectedMessage.date) : ""); muted: true }
                Tk.Label { text: String(control.selectedMessage ? control.selectedMessage.subject || "" : ""); font.weight: Font.DemiBold }
                Tk.Label {
                    objectName: "mailReaderBody"; width: parent.width
                    text: String(control.selectedMessage ? control.selectedMessage.bodyPlain || control.selectedMessage.snippet || "" : "")
                    wrapMode: Text.Wrap; Accessible.role: Accessible.StaticText
                }
                Repeater {
                    model: control.selectedMessage ? control.selectedMessage.attachments || [] : []
                    delegate: Tk.Flex {
                        id: attachmentRow
                        required property var modelData
                        readonly property string messageId: String(control.selectedMessage ? control.selectedMessage.messageId || "" : "")
                        readonly property string jobState: control.attachmentState(messageId, modelData)
                        objectName: "mailAttachment_" + String(modelData.id || "")
                        direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center; width: parent ? parent.width : 0
                        Tk.Icon { name: attachmentRow.jobState === "failed" ? "circle-alert" : "file"; size: Tk.Theme.size.iconSm; color: attachmentRow.jobState === "failed" ? Tk.Theme.color.danger : Tk.Theme.color.textMuted }
                        Tk.Label { text: String(modelData.name || modelData.id || ""); elide: Text.ElideMiddle; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                        Tk.Caption { objectName: "mailAttachmentState"; color: Tk.Theme.color.textMuted; text: [modelData.size ? qsTr("%1 bytes").arg(modelData.size) : "", attachmentRow.jobState === "remote" ? qsTr("on server") : attachmentRow.jobState === "loading" ? qsTr("loading…") : attachmentRow.jobState === "local" ? qsTr("saved") : qsTr("failed")].filter(Boolean).join(" · ") }
                        Tk.Button {
                            objectName: "mailAttachmentLoad"; small: true
                            text: attachmentRow.jobState === "local" ? qsTr("Open") : attachmentRow.jobState === "failed" ? qsTr("Retry") : qsTr("Download")
                            busy: attachmentRow.jobState === "loading"
                            available: attachmentRow.jobState === "local" || !control.offline
                            tooltip: attachmentRow.jobState === "local" ? qsTr("Open the saved attachment") : qsTr("Download only this attachment")
                            onClicked: control.requestAttachment(attachmentRow.messageId, String(modelData.id || ""))
                        }
                    }
                }
                Tk.Caption { text: qsTr("Body supplied as plain text by host · %1").arg(String(control.selectedMessage ? control.selectedMessage.bodyTrustMode || "untrusted" : "")) }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.xs; wrap: Tk.Flex.Wrap; width: parent.width
            Tk.Button { objectName: "mailReply"; text: qsTr("Reply"); tooltip: qsTr("Start a reply draft"); enabled: !!control.selectedMessageId; onClicked: control.replyRequested(control.selectedMessageId) }
            Tk.Button { objectName: "mailToggleRead"; text: control.selectedMessage && control.selectedMessage.unread ? qsTr("Mark read") : qsTr("Mark unread"); tooltip: qsTr("Request a read-state change"); enabled: !!control.selectedMessageId && !control.offline; onClicked: control.requestAction(control.selectedMessageId, control.selectedMessage.unread ? "read" : "unread") }
            Tk.Button { objectName: "mailToggleFlag"; text: control.selectedMessage && control.selectedMessage.flagged ? qsTr("Unflag") : qsTr("Flag"); tooltip: qsTr("Request a flag change"); enabled: !!control.selectedMessageId && !control.offline; onClicked: control.requestAction(control.selectedMessageId, control.selectedMessage.flagged ? "unflag" : "flag") }
            Tk.Button { objectName: "mailArchive"; text: qsTr("Archive"); tooltip: qsTr("Request archive for selected message"); enabled: !!control.selectedMessageId && !control.offline; onClicked: control.requestAction(control.selectedMessageId, "archive") }
            Tk.Button { objectName: "mailDelete"; text: qsTr("Delete"); tooltip: qsTr("Request delete for selected message"); enabled: !!control.selectedMessageId && !control.offline; onClicked: control.requestAction(control.selectedMessageId, "delete") }
            Tk.Label { objectName: "mailRequestState"; text: control.requestMessage || control.requestState; muted: true; Tk.Flex.grow: 1 }
        }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
