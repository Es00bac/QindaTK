// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Local presentation fixture. Mail delivery, recurrence expansion and storage
// are host services; this page performs none of those operations.
Tk.AppWindow {
    id: window
    title: qsTr("Productivity controls · local fixture")
    width: 1160
    height: 900
    property var mailRows: [
        { messageId: "msg-104", sender: "Mira Chen", subject: "Review: field notes", snippet: "Two comments on the draft…", bodyPlain: "I left two comments on the field notes. Could we review the interview prompts before Friday?", bodyTrustMode: "sanitized plain text", unread: true },
        { messageId: "msg-102", sender: "Studio Ops", subject: "Room change", snippet: "The Thursday room is confirmed.", unread: false }
    ]
    property var taskColumns: [
        { status: "todo", label: "To do", tasks: [{ taskId: "task-11", title: "Prepare interview kit", assignee: "Mira", dueDate: "2026-09-25", priority: "High" }] },
        { status: "doing", label: "In progress", tasks: [{ taskId: "task-12", title: "Review transcript", assignee: "Lee", dueDate: "2026-09-24", priority: "Normal" }] },
        { status: "done", label: "Done", tasks: [{ taskId: "task-09", title: "Book field room", assignee: "Mira", dueDate: "2026-09-22", priority: "Low" }] }
    ]

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.md; padding: Tk.Theme.space.md
        Tk.Overline { title: qsTr("Productivity · host-owned snapshots"); wide: true }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.md; Tk.Flex.shrink: 0
            Tk.MailView {
                id: mail; Tk.Flex.flex: 1; messages: window.mailRows; selectedMessageId: "msg-104"; offline: true
                onMessageSelected: function(messageId) { composer.draft = { to: "mira@example.test", cc: "", subject: "Re: " + messageId, body: "Thanks — I will review the notes.", attachments: [] } }
            }
            Tk.RichMailComposer {
                id: composer; Tk.Flex.flex: 1; online: false
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
                    { eventId: "event-33", title: "Field interview · Lee", startLocal: "2026-09-24 10:30", endLocal: "2026-09-24 11:15", zone: "America/Denver", allDay: false, calendarId: "work" },
                    { eventId: "event-34", title: "Studio review", startLocal: "2026-09-25", zone: "America/Denver", allDay: true, calendarId: "work" }
                ]
            }
            Tk.TaskBoard { id: board; Tk.Flex.flex: 1; columns: window.taskColumns }
        }
        Tk.RecurrenceEditor {
            id: recurrence; eventId: "event-33"; zone: "America/Denver"
            rule: ({ frequency: "weekly", interval: 1, weekdays: [4], endMode: "never", count: 0, until: "", exceptions: ["occurrence-2026-10-01"] })
            onRuleChangeRequested: function(requestId, eventId, proposed) { rule = proposed; acknowledge(requestId, "accepted", qsTr("Fixture accepted the rule proposal.")) }
            onPreviewRequested: function(requestId, eventId, proposed) { preview = [{ localStart: "2026-09-24 10:30", zone: "America/Denver" }, { localStart: "2026-10-01 10:30", zone: "America/Denver" }]; previewState = "ready"; acknowledge(requestId, "accepted", "") }
        }
        Tk.Notice { variant: "info"; title: qsTr("Fixture boundary"); text: qsTr("All records are local sample snapshots. Requests are displayed or acknowledged by this page; no mail is sent, no calendar is saved, and no remote sync occurs.") }
    }
}
