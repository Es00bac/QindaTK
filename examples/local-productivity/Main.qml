// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 1440
    height: 980
    property string activeDraftId: "draft-local-1"
    property string hostMessage: qsTr("Loading local snapshot…")
    readonly property var boardColumns: [
        {status: "todo", label: qsTr("To do"), tasks: tasksFor("todo")},
        {status: "doing", label: qsTr("In progress"), tasks: tasksFor("doing")},
        {status: "done", label: qsTr("Done"), tasks: tasksFor("done")}]

    function tasksFor(status) {
        return store.tasks.filter(function(task) { return task.status === status })
            .sort(function(a, b) { return Number(a.order || 0) - Number(b.order || 0) })
            .map(function(task) { return Object.assign({}, task, {taskId: task.id}) })
    }
    function seedStore() {
        store.upsert("messages", {id: "message-welcome", messageId: "message-welcome", sender: "Local notebook", subject: "Offline welcome", snippet: "This message is stored on this device.", bodyPlain: "Your messages, drafts, calendar events, attachments metadata and task order are saved locally.", bodyTrustMode: "plain", unread: true})
        store.upsert("drafts", {id: activeDraftId, to: "", cc: "", subject: "", body: "", attachments: [{id: "attachment-map", name: "route-map.pdf", mimeType: "application/pdf", size: 48200, state: "local"}]})
        store.upsert("events", {id: "event-review", eventId: "event-review", title: "Design review", startLocal: "2026-09-24 10:00", endLocal: "2026-09-24 10:45", zone: "America/Denver", allDay: false, calendarId: "personal", recurrenceRule: {frequency: "weekly", interval: 1, weekdays: [4], end: "never"}, exceptions: []})
        store.upsert("events", {id: "event-demo", eventId: "event-demo", title: "Local prototype demo", startLocal: "2026-09-25 14:00", endLocal: "2026-09-25 14:30", zone: "America/Denver", allDay: false, calendarId: "work", recurrenceRule: null, exceptions: []})
        store.upsert("tasks", {id: "task-inbox", title: "Review inbox model", assignee: "Ari", dueDate: "2026-09-25", priority: "High", status: "todo", order: 0})
        store.upsert("tasks", {id: "task-fixture", title: "Check local fixture", assignee: "Sam", dueDate: "2026-09-26", priority: "Normal", status: "todo", order: 1})
        store.upsert("tasks", {id: "task-calendar", title: "Prepare event notes", assignee: "Mina", dueDate: "2026-09-27", priority: "Low", status: "doing", order: 0})
        if (store.save()) hostMessage = qsTr("Seeded and saved local records")
        else hostMessage = store.lastError
    }
    function reloadStore() {
        if (store.load()) {
            activeDraftId = store.drafts.length ? String(store.drafts[0].id) : "draft-local-1"
            composer.draft = draftData()
            hostMessage = store.status === "recovered" ? qsTr("Recovered previous atomic snapshot: %1").arg(store.lastError) : qsTr("Loaded local snapshot")
        } else {
            seedStore()
            composer.draft = draftData()
        }
    }
    function draftData() {
        for (let i = 0; i < store.drafts.length; ++i)
            if (String(store.drafts[i].id) === activeDraftId) return store.drafts[i]
        return {to: "", cc: "", subject: "", body: "", attachments: []}
    }
    function persistDraft(message) {
        const row = Object.assign({id: activeDraftId}, composer.snapshot())
        const result = store.upsert("drafts", row)
        if (!result.ok || !store.save()) {
            composer.acknowledge(composer.lastRequestId, "rejected", result.error || store.lastError)
            hostMessage = result.error || store.lastError
            return false
        }
        if (message) composer.acknowledge(composer.lastRequestId, "accepted", qsTr("Draft saved locally · %1 attachments").arg(row.attachments.length))
        hostMessage = qsTr("Draft autosaved · store revision %1").arg(store.revision)
        return true
    }
    function handleTaskMove(requestId, taskId, fromStatus, toStatus) {
        const result = store.moveTask(taskId, toStatus)
        if (result.ok && store.save()) {
            board.acknowledge(requestId, "accepted", qsTr("Moved locally · order saved · undo available"))
        } else board.acknowledge(requestId, "rejected", result.error || store.lastError)
        hostMessage = board.requestMessage
    }

    Tk.LocalProductivityStore {
        id: store
        filePath: defaultPath
        Component.onCompleted: root.reloadStore()
    }
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Local productivity") }
                Tk.Caption { text: qsTr("Messages · drafts · events · tasks are stored on this device"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button { text: qsTr("Reload from disk"); tooltip: qsTr("Reload persisted local records"); onClicked: root.reloadStore() }
            Tk.Button { text: qsTr("Undo event move"); tooltip: qsTr("Undo the last local calendar move"); enabled: store.canUndoEventChange; onClicked: { const result = store.undoEventChange(); if (result.ok && store.save()) root.hostMessage = qsTr("Calendar move undone and saved") } }
            Tk.Button { text: qsTr("Undo task move"); tooltip: qsTr("Undo the last local task move"); enabled: store.canUndoTaskMove; onClicked: { const result = store.undoTaskMove(); if (result.ok && store.save()) { board.acknowledge(board.lastRequestId, "accepted", qsTr("Task move undone and saved")); root.hostMessage = board.requestMessage } } }
            Tk.Badge { text: qsTr("Local only"); variant: "info" }
        }
        Tk.Caption { objectName: "localStoreStatus"; text: root.hostMessage + " · " + store.status + " · revision " + store.revision; color: store.lastError.length ? Tk.Theme.color.warning : Tk.Theme.color.textMuted; wrapMode: Text.Wrap }
        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1fr) minmax(0, 1fr)"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.sm
            Tk.Panel {
                title: qsTr("Messages"); iconName: "mail"
                Tk.MailView {
                    id: mail
                    anchors.fill: parent
                    messages: store.messages.filter(function(row) { return !row.archived })
                    selectedMessageId: messages.length ? String(messages[0].messageId) : ""
                    offline: false
                    syncState: "local"
                    onRefreshRequested: function(requestId) { mail.acknowledge(requestId, "rejected", qsTr("Remote refresh is not configured; local messages remain available.")) }
                    onActionRequested: function(requestId, messageId, action) {
                        const row = store.messages.find(function(message) { return message.messageId === messageId })
                        const result = action === "delete" ? store.remove("messages", messageId)
                            : row ? store.upsert("messages", Object.assign({}, row, {archived: true})) : ({ok: false, error: qsTr("Message not found")})
                        if (result.ok && store.save()) mail.acknowledge(requestId, "accepted", qsTr("Message %1 saved locally").arg(action))
                        else mail.acknowledge(requestId, "rejected", result.error || store.lastError)
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Draft composer"); iconName: "file-edit"
                Tk.RichMailComposer {
                    id: composer
                    anchors.fill: parent
                    draft: root.draftData()
                    online: true
                    statusMessage: qsTr("Drafts persist locally; no delivery provider is configured.")
                    onDraftChangeRequested: function(field, value) { root.persistDraft(false) }
                    onSaveDraftRequested: function(requestId, draft) { root.persistDraft(true) }
                    onSendRequested: function(requestId, draft) { composer.acknowledge(requestId, "rejected", qsTr("No remote mail provider is configured; nothing was sent.")) }
                }
            }
            Tk.Panel {
                title: qsTr("Calendar"); iconName: "calendar-days"
                Tk.CalendarView {
                    id: calendar
                    anchors.fill: parent
                    events: store.events
                    rangeLabel: qsTr("September 24–27, 2026")
                    zone: "America/Denver"
                    offline: false
                    onNavigateRequested: function(requestId, direction) {
                        calendar.acknowledge(requestId, "rejected")
                        hostMessage = qsTr("Calendar ranges are the current local snapshot; no remote navigation provider is configured.")
                    }
                    onEventChangeRequested: function(requestId, eventId, proposed) {
                        if (proposed.kind !== "shift-local") {
                            calendar.acknowledge(requestId, "rejected")
                            hostMessage = qsTr("This calendar change needs a provider.")
                            return
                        }
                        const result = store.shiftEventLocal(eventId, Number(proposed.minutes))
                        if (result.ok && store.save()) {
                            calendar.acknowledge(requestId, "accepted")
                            hostMessage = qsTr("Calendar move saved locally; undo is available.")
                        } else {
                            const reason = result.error || store.lastError
                            if (result.ok) store.undoEventChange()
                            calendar.acknowledge(requestId, "rejected")
                            hostMessage = reason
                        }
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Task board"); iconName: "list-checks"
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.TaskBoard {
                        id: board
                        width: parent.width
                        columns: root.boardColumns
                        onMoveRequested: function(requestId, taskId, fromStatus, toStatus) { root.handleTaskMove(requestId, taskId, fromStatus, toStatus) }
                    }
                    Tk.Caption { text: qsTr("Move and undo each persist order atomically."); color: Tk.Theme.color.textMuted }
                }
            }
        }
    }
}
