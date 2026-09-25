// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Local presentation fixture. Recurrence uses the toolkit's local engine;
// mail delivery and record persistence remain host services here.
Tk.AppWindow {
    id: window
    title: qsTr("Productivity controls · local fixture")
    width: 1160
    height: 900
    property var mailRows: [
        { messageId: "msg-104", threadId: "notes", sender: "Mira Chen", subject: "Review: field notes", date: "2026-09-24T15:00:00Z", snippet: "Two comments on the draft…", bodyPlain: "I left two comments on the field notes. Could we review the interview prompts before Friday?", bodyTrustMode: "sanitized plain text", unread: true,
          attachments: [{ id: "msg-104/2", name: "field-notes.pdf", mimeType: "application/pdf", size: 48200, state: "remote" }, { id: "msg-104/3", name: "prompts.txt", mimeType: "text/plain", size: 812, state: "local" }] },
        { messageId: "msg-103", threadId: "notes", sender: "Lee Park", subject: "Field notes", date: "2026-09-24T12:00:00Z", snippet: "Added the transcript.", unread: false, attachments: [] },
        { messageId: "msg-102", sender: "Studio Ops", subject: "Room change", date: "2026-09-23T09:00:00Z", snippet: "The Thursday room is confirmed.", unread: false, flagged: true, attachments: [] }
    ]
    property var taskColumns: [
        { status: "todo", label: "To do", tasks: [{ taskId: "task-11", title: "Prepare interview kit", assignee: "Mira", dueDate: "2026-09-25", priority: "High" }, { taskId: "task-13", title: "Print consent forms", assignee: "Lee", dueDate: "2026-09-26", priority: "Normal" }] },
        { status: "doing", label: "In progress", tasks: [{ taskId: "task-12", title: "Review transcript", assignee: "Lee", dueDate: "2026-09-24", priority: "Normal", syncState: "conflict" }] },
        { status: "done", label: "Done", tasks: [{ taskId: "task-09", title: "Book field room", assignee: "Mira", dueDate: "2026-09-22", priority: "Low" }] }
    ]
    property var taskConflicts: [{ collection: "tasks", id: "task-12", remoteFields: { title: "Review transcript (edited)", status: "done" } }]
    Component.onCompleted: recurrence.requestPreview()

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.md; padding: Tk.Theme.space.md
        Tk.Overline { title: qsTr("Productivity · host-owned snapshots"); wide: true }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.md; Tk.Flex.shrink: 0
            Tk.MailView {
                id: mail; Tk.Flex.flex: 1; messages: window.mailRows; selectedMessageId: "msg-104"; offline: true
                Component.onCompleted: attachmentJobs = ({ "fixture-job": { messageId: "msg-104", attachmentId: "msg-104/2", state: "loading", message: "" } })
                onMessageSelected: function(messageId) { composer.draft = { to: "mira@example.test", cc: "", subject: "Re: " + messageId, body: "Thanks — I will review the notes.", attachments: [] } }
            }
            Tk.RichMailComposer {
                id: composer; Tk.Flex.flex: 1; online: false
                draft: ({ to: "Mira Chen <mira@example.test>, mira at example", cc: "", subject: "Re: field notes", body: "Thanks — I will review the notes.", attachments: [{ id: "att-1", name: "prompts-v2.txt", mimeType: "text/plain", size: 912, state: "local" }] })
                onDraftChangeRequested: function(field, value) { const next = Object.assign({}, draft); next[field] = value; draft = next }
                onSendRequested: function(requestId, snapshot) { acknowledge(requestId, "rejected", qsTr("Local fixture: delivery is disabled; draft preserved.")) }
                onSaveDraftRequested: function(requestId, snapshot) { acknowledge(requestId, "accepted", qsTr("Fixture acknowledged draft request; no disk write.")) }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.md; Tk.Flex.shrink: 0
            Tk.CalendarView {
                id: calendar; Tk.Flex.flex: 1; rangeLabel: qsTr("Week of Sep 21"); zone: "America/Denver"; offline: true
                events: [
                    { eventId: "event-33", occurrenceId: "event-33/2026-09-24T10:30:00", title: "Field interview · Lee", startLocal: "2026-09-24 10:30", endLocal: "2026-09-24 11:15", zone: "America/Denver", allDay: false, calendarId: "work", recurrenceRule: { frequency: "weekly" } },
                    { eventId: "event-34", title: "Studio review", startLocal: "2026-09-25", zone: "America/Denver", allDay: true, calendarId: "work" },
                    { eventId: "event-33", occurrenceId: "event-33/2026-10-08T10:30:00", title: "Field interview · Lee", startLocal: "2026-10-08T13:00:00", endLocal: "2026-10-08T13:45:00", zone: "America/Denver", allDay: false, calendarId: "work", recurrenceRule: { frequency: "weekly" }, overridden: true }
                ]
            }
            Tk.TaskBoard { id: board; Tk.Flex.flex: 1; columns: window.taskColumns; conflicts: window.taskConflicts }
        }
        Tk.RecurrenceService { id: recurrenceService }
        Tk.RecurrenceEditor {
            id: recurrence; eventId: "event-33"; zone: "America/Denver"; startLocal: "2026-09-24T10:30:00"
            rule: ({ frequency: "weekly", interval: 1, weekdays: [4], endMode: "never", count: 0, until: "", exceptions: ["event-33/2026-10-01T10:30:00"] })
            onRuleChangeRequested: function(requestId, eventId, proposed) { rule = proposed; acknowledge(requestId, "accepted", qsTr("Fixture accepted the rule proposal.")) }
            onPreviewRequested: function(requestId, eventId, proposed) {
                preview = recurrenceService.expand(eventId, "2026-09-24T10:30:00", 2700,
                    recurrence.zone, proposed, "2026-09-21T00:00:00Z", "2026-11-01T00:00:00Z", 16)
                previewState = recurrenceService.error.length ? "error" : "ready"
                acknowledge(requestId, recurrenceService.error.length ? "rejected" : "accepted", recurrenceService.error)
            }
        }
        Tk.Notice { variant: "info"; title: qsTr("Fixture boundary"); text: qsTr("Recurrence rows are computed locally. Other records are sample snapshots showing view states (a loading attachment, an invalid recipient, a moved occurrence, a sync conflict): no mail is sent, no calendar is saved, and no remote sync occurs.") }
    }
}
