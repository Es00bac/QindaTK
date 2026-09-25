// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The host side of the productivity views: it owns the local store, the
// attachment bytes and the three standard-protocol providers, and turns each
// view request into a store edit and/or provider operation whose result
// comes back through the view's acknowledge(). Records that belong to a
// configured account (remoteAccount) are written through to the server
// before the view is acknowledged; local records are acknowledged after the
// local save. With no server configured, provider requests are rejected by
// the provider itself ("No server host and port are configured").
// AGENT-CONTRACT (tests/cpp/tst_productivity_seam.cpp): objectNames
// productivityStore, smtpProvider, imapProvider, caldavProvider; settings
// maps carry the provider properties; secrets go through setSecrets().
Item {
    id: host
    visible: false
    property string storePath: ""
    property string attachmentPath: ""
    property url attachmentSource: Qt.resolvedUrl("agenda-notes.txt")
    property var smtpSettings: ({})
    property var imapSettings: ({})
    property var caldavSettings: ({})
    property Item mail: null
    property Item composer: null
    property Item calendar: null
    property Item board: null
    property Item recurrence: null
    property string activeDraftId: "draft-local-1"
    property string recurrenceEventId: ""
    property string message: qsTr("Loading local snapshot…")
    property string rangeStart: "2026-09-21T00:00:00Z"
    property string rangeEnd: "2026-10-05T00:00:00Z"
    property var pendingRemote: ({})   // requestId -> view that waits for a CalDAV write-through
    readonly property alias store: store
    readonly property alias projector: projector
    readonly property alias caldav: caldav
    readonly property alias imap: imap
    readonly property var boardColumns: [
        { status: "todo", label: qsTr("To do"), tasks: tasksFor("todo") },
        { status: "doing", label: qsTr("In progress"), tasks: tasksFor("doing") },
        { status: "done", label: qsTr("Done"), tasks: tasksFor("done") }]

    Tk.LocalProductivityStore { id: store; objectName: "productivityStore"; filePath: host.storePath || defaultPath + ".local-productivity" }
    Tk.LocalAttachmentStore { id: attachments; objectName: "attachmentStore"; rootPath: host.attachmentPath || defaultPath + "/local-productivity" }
    Tk.RecurrenceService { id: recurrenceService }
    Tk.LocalCalendarProjectionModel { id: projector; store: store; recurrenceService: recurrenceService }
    Tk.SmtpMailProvider { id: smtp; objectName: "smtpProvider"; attachmentStore: attachments }
    Tk.ImapMailProvider { id: imap; objectName: "imapProvider"; store: store; attachmentStore: attachments }
    Tk.CalDavProvider { id: caldav; objectName: "caldavProvider"; store: store }

    function apply(target, settings) { for (const key in settings) target[key] = settings[key] }
    // Secrets come from the desktop keyring in a real host; they stay in memory.
    function setSecrets(smtpSecret, imapSecret, caldavSecret, mechanisms) {
        const m = mechanisms || ({})
        if (smtpSecret) smtp.setSecret(smtpSecret, m.smtp || "auto")
        if (imapSecret) imap.setSecret(imapSecret, m.imap || "auto")
        if (caldavSecret) caldav.setSecret(caldavSecret, m.caldav || "basic")
    }
    function tasksFor(status) {
        return store.tasks.filter(function(task) { return task.status === status })
            .sort(function(a, b) { return Number(a.order || 0) - Number(b.order || 0) })
            .map(function(task) { return Object.assign({}, task, { taskId: task.id }) })
    }
    function record(collection, id) { return store[collection].find(function(row) { return String(row.id) === id }) || null }
    function saved(result) { return result.ok && store.save() }
    function failure(result) { return result.error || store.lastError }
    function seed() {
        store.upsert("messages", { id: "message-welcome", messageId: "message-welcome", threadId: "welcome", sender: "Local notebook", subject: "Offline welcome", snippet: "This message is stored on this device.", bodyPlain: "Your messages, drafts, calendar events, attachment bytes and task order are saved locally.", bodyTrustMode: "plain", unread: true, date: "2026-09-24T08:00:00Z", attachments: [] })
        store.upsert("messages", { id: "message-welcome-2", messageId: "message-welcome-2", threadId: "welcome", sender: "Mira Chen", subject: "Re: Offline welcome", snippet: "Replies group into one conversation.", bodyPlain: "Replies group into one conversation, and attachments load only when you ask.", bodyTrustMode: "plain", unread: false, date: "2026-09-24T09:30:00Z", attachments: [] })
        store.upsert("drafts", { id: activeDraftId, to: "", cc: "", subject: "Design review notes", body: "Hi team,\n\nHere are the notes from today's review.", attachments: [] })
        store.upsert("events", { id: "event-review", eventId: "event-review", title: "Design review", startLocal: "2026-09-24T10:00:00", endLocal: "2026-09-24T10:45:00", zone: "America/Denver", allDay: false, calendarId: "personal", recurrenceRule: { frequency: "weekly", interval: 1, weekdays: [4], endMode: "never" }, exceptions: [], overrides: [] })
        store.upsert("events", { id: "event-offsite", eventId: "event-offsite", title: "Studio offsite", startLocal: "2026-09-25", endLocal: "2026-09-26", zone: "America/Denver", allDay: true, calendarId: "work", recurrenceRule: null, exceptions: [] })
        store.upsert("tasks", { id: "task-inbox", title: "Review inbox model", assignee: "Ari", dueDate: "2026-09-25", priority: "High", status: "todo", order: 0 })
        store.upsert("tasks", { id: "task-fixture", title: "Check local fixture", assignee: "Sam", dueDate: "2026-09-26", priority: "Normal", status: "todo", order: 1 })
        store.upsert("tasks", { id: "task-calendar", title: "Prepare event notes", assignee: "Mina", dueDate: "2026-09-27", priority: "Low", status: "doing", order: 0 })
        message = store.save() ? qsTr("Seeded and saved local records") : store.lastError
    }
    function reload() {
        if (store.load()) {
            const drafts = store.drafts.filter(function(row) { return !row.sent })
            activeDraftId = drafts.length ? String(drafts[drafts.length - 1].id) : "draft-local-1"
            message = store.status === "recovered" ? qsTr("Recovered previous atomic snapshot: %1").arg(store.lastError) : qsTr("Loaded local snapshot")
        } else {
            seed()
        }
        if (composer) composer.draft = draftData()
        projector.project(rangeStart, rangeEnd)
        if (!recurrenceEventId) {
            const recurring = store.events.find(function(row) { return row.recurrenceRule && row.recurrenceRule.frequency })
            if (recurring) selectRecurrence(String(recurring.eventId || recurring.id))
        }
    }
    function draftData() { return record("drafts", activeDraftId) || { to: "", cc: "", subject: "", body: "", attachments: [] } }
    function persistDraft(acknowledgeSave) {
        const result = store.upsert("drafts", Object.assign({}, draftData(), composer.snapshot(), { id: activeDraftId }))
        const ok = saved(result)
        if (acknowledgeSave) composer.acknowledge(composer.lastRequestId, ok ? "accepted" : "rejected", ok ? qsTr("Draft saved locally") : failure(result))
        message = ok ? qsTr("Draft autosaved · store revision %1").arg(store.revision) : failure(result)
        return ok
    }

    // Mail ------------------------------------------------------------------
    function refreshMail(requestId) { imap.sync(requestId) }
    function imapFetch(requestId, messageId, attachmentId) { imap.fetchAttachment(requestId, messageId, attachmentId) }
    function mailAction(requestId, messageId, action) {
        const row = record("messages", messageId)
        if (row && row.remoteAccount && row.remoteAccount === imap.accountId) { imap.applyAction(requestId, messageId, action); return }
        let result = { ok: false, error: qsTr("Message not found") }
        if (row && action === "delete") result = store.remove("messages", messageId)
        else if (row) result = store.upsert("messages", Object.assign({}, row, action === "archive" ? { archived: true }
            : action === "read" || action === "unread" ? { unread: action === "unread" } : { flagged: action === "flag" }))
        const ok = saved(result)
        mail.acknowledge(requestId, ok ? "accepted" : "rejected", ok ? qsTr("Message %1 saved locally").arg(action) : failure(result))
    }
    function reply(messageId) {
        const row = record("messages", messageId)
        if (!row) return
        persistDraft(false)
        activeDraftId = "draft-reply-" + Date.now()
        const subject = String(row.subject || "")
        store.upsert("drafts", { id: activeDraftId, to: String(row.senderAddress || row.sender || ""), cc: "",
            subject: subject.startsWith("Re:") ? subject : "Re: " + subject, body: "",
            inReplyTo: String(row.internetMessageId || ""), references: String(row.internetMessageId || ""), attachments: [] })
        store.save()
        composer.draft = draftData()
        message = qsTr("Reply draft started for %1").arg(subject)
    }
    function openAttachment(messageId, attachmentId) {
        const bytes = attachments.read(attachmentId)
        message = bytes.ok ? qsTr("%1 is saved on this device (%2 bytes)").arg(bytes.name).arg(bytes.size) : bytes.error
    }

    // Composer ----------------------------------------------------------------
    function send(requestId, draft) {
        if (!persistDraft(false)) { composer.acknowledge(requestId, "rejected", store.lastError); return }
        smtp.send(requestId, Object.assign({ id: activeDraftId }, draftData(), draft))
    }
    function addAttachment(requestId) {
        const id = activeDraftId + "/attachment-" + Date.now()
        const imported = attachments.importFile(id, attachmentSource)
        if (!imported.ok) { composer.acknowledgeAttachment(requestId, "rejected", imported.error); return }
        const payload = attachments.read(id)
        composer.acknowledgeAttachment(requestId, "accepted", "", { id: id, name: payload.name, mimeType: payload.mimeType, size: payload.size, state: "local" })
        persistDraft(false)
    }
    function removeAttachment(requestId, attachmentId) {
        const removed = attachments.remove(attachmentId)
        composer.acknowledgeAttachment(requestId, removed.ok ? "accepted" : "rejected", removed.error || "")
        if (removed.ok) persistDraft(false)
    }

    // Calendar ------------------------------------------------------------------
    function moveRange(direction) {
        const delta = direction === "next" ? 14 : -14
        const start = new Date(rangeStart), end = new Date(rangeEnd)
        start.setUTCDate(start.getUTCDate() + delta)
        end.setUTCDate(end.getUTCDate() + delta)
        rangeStart = start.toISOString(); rangeEnd = end.toISOString()
        return projector.project(rangeStart, rangeEnd)
    }
    function rangeLabel() { return rangeStart.slice(0, 10) + " – " + new Date(new Date(rangeEnd).getTime() - 86400000).toISOString().slice(0, 10) }
    function remoteBacked(collection, id) {
        const row = record(collection, id)
        return !!row && !!caldav.collectionUrl && row.remoteAccount === caldav.accountId
    }
    // Local edit first, then (for account records) a CalDAV sync whose result
    // is the acknowledgment; a failed local save is undone.
    function writeThrough(view, requestId, collection, id, result, undo, done) {
        if (!saved(result)) {
            if (result.ok) undo()
            view.acknowledge(requestId, "rejected", failure(result))
            return
        }
        if (remoteBacked(collection, id)) {
            const pending = Object.assign({}, pendingRemote)
            pending[requestId] = view
            pendingRemote = pending
            caldav.sync(requestId)
            return
        }
        view.acknowledge(requestId, "accepted", done)
    }
    function moveEvent(requestId, eventId, proposed) {
        if (proposed.kind !== "shift-local") { calendar.acknowledge(requestId, "rejected", qsTr("Unsupported calendar change")); return }
        const result = store.shiftEventLocal(eventId, Number(proposed.minutes), String(proposed.scope || "series"), String(proposed.occurrenceId || ""))
        writeThrough(calendar, requestId, "events", String(result.eventId || eventId), result, function() { store.undoEventChange() },
                     qsTr("Moved (%1) and saved; undo is available").arg(String(proposed.scope || "series")))
    }
    function selectRecurrence(eventId) {
        const row = record("events", eventId) || store.events.find(function(entry) { return String(entry.eventId) === eventId })
        if (!row || !row.recurrenceRule || !row.recurrenceRule.frequency) return false
        recurrenceEventId = String(row.id)
        if (recurrence) {
            recurrence.rule = Object.assign({ interval: 1, weekdays: [], endMode: "never", count: 0, until: "" }, row.recurrenceRule, { exceptions: row.exceptions || [] })
            previewRecurrence("")
        }
        return true
    }
    function previewRecurrence(requestId) {
        const row = record("events", recurrenceEventId)
        if (!row || !recurrence) return
        const rule = Object.assign({}, row.recurrenceRule, { exceptions: row.exceptions || [] })
        const duration = Math.max(0, recurrenceService.durationSeconds(String(row.startLocal), String(row.endLocal), String(row.zone)))
        recurrence.preview = recurrenceService.expand(String(row.eventId || row.id), String(row.startLocal), duration, String(row.zone), rule,
                                                      rangeStart, new Date(new Date(rangeStart).getTime() + 70 * 86400000).toISOString(), 8)
        recurrence.previewState = recurrenceService.error.length ? "error" : "ready"
        if (requestId) recurrence.acknowledge(requestId, recurrenceService.error.length ? "rejected" : "accepted", recurrenceService.error)
    }
    function changeRule(requestId, eventId, proposed) {
        writeThrough(recurrence, requestId, "events", recurrenceEventId, store.setEventRecurrence(recurrenceEventId, proposed),
                     function() { store.undoEventChange() }, qsTr("Rule saved"))
        selectRecurrence(recurrenceEventId)
    }
    function excludeOccurrence(requestId, eventId, occurrenceId, excluded) {
        writeThrough(recurrence, requestId, "events", recurrenceEventId, store.setOccurrenceExcluded(recurrenceEventId, occurrenceId, excluded),
                     function() { store.undoEventChange() }, excluded ? qsTr("Occurrence skipped") : qsTr("Occurrence restored"))
        selectRecurrence(recurrenceEventId)
    }

    // Tasks ---------------------------------------------------------------------
    function moveTask(requestId, taskId, fromStatus, toStatus, targetIndex) {
        writeThrough(board, requestId, "tasks", taskId, store.moveTask(taskId, toStatus, targetIndex), function() { store.undoTaskMove() },
                     qsTr("Moved and saved · undo is available"))
    }
    function resolveTask(requestId, taskId, choice) { caldav.resolveConflict(requestId, "tasks", taskId, choice) }
    function syncCalendar(requestId) { caldav.sync(requestId) }

    Connections {
        target: imap
        function onSyncFinished(requestId, result) { host.mail.acknowledge(requestId, result.state, result.ok ? qsTr("Mailbox synced") : result.error) }
        function onActionFinished(requestId, result) { host.mail.acknowledge(requestId, result.state, result.ok ? qsTr("Server confirmed") : result.error) }
        function onAttachmentFinished(requestId, result) { host.mail.acknowledgeAttachment(requestId, result.state, result.error) }
    }
    Connections {
        target: smtp
        function onSendFinished(requestId, result) {
            if (result.ok) {
                const draft = host.draftData()
                store.upsert("drafts", Object.assign({}, draft, { sent: true, sentMessageId: result.messageId }))
                store.save()
            }
            host.composer.acknowledge(requestId, result.state, result.ok ? qsTr("Sent · %1").arg(result.messageId) : result.error)
        }
    }
    Connections {
        target: caldav
        function onSyncFinished(requestId, result) {
            const view = host.pendingRemote[requestId]
            const pending = Object.assign({}, host.pendingRemote)
            delete pending[requestId]
            host.pendingRemote = pending
            const text = result.ok ? qsTr("Saved here and on the server") : result.error || qsTr("The server copy changed; choose a version")
            if (view) view.acknowledge(requestId, result.state, text)
            host.message = text
        }
        function onChangeFinished(requestId, result) { host.board.acknowledge(requestId, result.state, result.ok ? qsTr("Conflict resolved (%1)").arg(result.resolved || "local") : result.error) }
    }
    Component.onCompleted: {
        apply(smtp, smtpSettings)
        apply(imap, imapSettings)
        apply(caldav, caldavSettings)
    }
}
