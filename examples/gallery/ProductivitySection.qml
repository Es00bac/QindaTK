// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Productivity"
    note: "MailView · RichMailComposer · CalendarView · RecurrenceEditor · TaskBoard"

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
