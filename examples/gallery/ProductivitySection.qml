// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Productivity"
    note: "MailView · RichMailComposer · CalendarView · CalendarGridView · RecurrenceEditor · TaskBoard"

    GalleryRow {
        label: "Cached mail"
        Tk.MailView {
            width: 480
            offline: true
            selectedMessageId: "msg-1"
            messages: [{ messageId: "msg-1", sender: "Mira Chen", subject: "Field notes", snippet: "Review the interview prompts.", bodyPlain: "Could we review the interview prompts before Friday?", bodyTrustMode: "sanitized plain text", unread: true }]
        }
    }
    GalleryRow {
        label: "Draft composition"
        Tk.RichMailComposer {
            width: 480
            draft: ({ to: "mira@example.test", cc: "", subject: "Review", body: "Thanks for the notes.", attachments: [] })
            online: false
        }
    }
    GalleryRow {
        label: "Agenda"
        Tk.CalendarView {
            width: 480
            rangeLabel: "Week of Sep 21"
            zone: "America/Denver"
            events: [{ eventId: "evt-1", title: "Studio review", startLocal: "2026-09-24 10:30", endLocal: "2026-09-24 11:15", zone: "America/Denver", allDay: false, calendarId: "work" }]
        }
    }
    GalleryRow {
        label: "Time grid"
        Tk.CalendarGridView {
            implicitWidth: 640
            implicitHeight: 420
            mode: "workweek"
            anchorDate: "2026-09-24"
            now: "2026-09-24T10:40:00"
            dayStartHour: 9
            zone: "America/Denver"
            calendars: [{ id: "work", name: "Work", color: "#4f7cff" }, { id: "home", name: "Home", color: "#22b37a" }]
            events: [
                { eventId: "standup", occurrenceId: "standup/2026-09-22T09:30:00", title: "Standup", startLocal: "2026-09-22T09:30:00", endLocal: "2026-09-22T09:45:00", zone: "America/Denver", calendarId: "work", recurrenceRule: { frequency: "daily" } },
                { eventId: "review", occurrenceId: "review/2026-09-24T10:00:00", title: "Studio review", startLocal: "2026-09-24T10:00:00", endLocal: "2026-09-24T11:15:00", zone: "America/Denver", calendarId: "work", location: "Studio B" },
                { eventId: "lunch", occurrenceId: "lunch/2026-09-24T10:30:00", title: "Lunch prep", startLocal: "2026-09-24T10:30:00", endLocal: "2026-09-24T11:30:00", zone: "America/Denver", calendarId: "home" },
                { eventId: "offsite", occurrenceId: "offsite/2026-09-25", title: "Offsite", startLocal: "2026-09-25", endLocal: "2026-09-26", zone: "America/Denver", allDay: true, calendarId: "home" }
            ]
        }
    }
    GalleryRow {
        label: "Recurrence rule"
        Tk.RecurrenceEditor {
            width: 640
            eventId: "evt-1"
            zone: "America/Denver"
            rule: ({ frequency: "weekly", interval: 1, weekdays: [4], endMode: "never", count: 0, until: "", exceptions: [] })
        }
    }
    GalleryRow {
        label: "Tasks"
        Tk.TaskBoard {
            width: 640
            columns: [
                { status: "todo", label: "To do", tasks: [{ taskId: "task-1", title: "Prepare interview kit", assignee: "Mira", dueDate: "2026-09-25", priority: "High" }] },
                { status: "doing", label: "In progress", tasks: [{ taskId: "task-2", title: "Review transcript", assignee: "Lee", dueDate: "2026-09-24", priority: "Normal" }] }
            ]
        }
    }
}
