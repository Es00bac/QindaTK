// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "Wp058Productivity"
    visible: true
    when: windowShown
    width: 800
    height: 600

    Component { id: mailComponent; Tk.MailView { messages: [{ messageId: "m-1", sender: "A", subject: "Hello", bodyPlain: "test body" }]; selectedMessageId: "m-1"; offline: true } }
    Component { id: composerComponent; Tk.RichMailComposer { draft: ({ id: "draft-stable", to: "a@example.test", subject: "Review", body: "Body", attachments: [{id: "attachment-1", name: "notes.pdf", mimeType: "application/pdf", size: 120} ] }); online: true } }
    Component { id: calendarComponent; Tk.CalendarView { events: [{ eventId: "evt-9", title: "Review", startLocal: "2026-09-24 10:30", zone: "UTC" }] } }
    Component { id: recurrenceComponent; Tk.RecurrenceEditor { eventId: "evt-9"; rule: ({ frequency: "weekly", interval: 1, weekdays: [4], exceptions: [] }) } }
    Component { id: boardComponent; Tk.TaskBoard { columns: [{ status: "todo", label: "To do", tasks: [{ taskId: "task-50", title: "Review" }] }, { status: "done", label: "Done", tasks: [] }] } }
    Component { id: signalSpyComponent; SignalSpy { } }

    function test_host_requests_and_stale_acknowledgments() {
        const mail = createTemporaryObject(mailComponent, root)
        verify(mail)
        compare(mail.selectedMessage.bodyPlain, "test body")
        verify(!mail.requestAction("m-1", "archive"), "offline actions must be rejected locally")
        verify(!mail.requestRefresh(), "offline refresh must be rejected locally")
        mail.offline = false
        const refreshId = mail.requestRefresh()
        verify(mail.acknowledge(refreshId, "accepted", "cached"))
        verify(!mail.acknowledge("old-id", "accepted", "late"))

        const composer = createTemporaryObject(composerComponent, root)
        verify(composer.validDraft)
        const draftSpy = createTemporaryObject(signalSpyComponent, root, { target: composer, signalName: "draftChangeRequested" })
        composer.editDraft("body", "Edited body")
        compare(draftSpy.signalArguments[0][0], "body")
        compare(composer.snapshot().body, "Edited body")
        verify(composer.snapshot().richDocumentJson.indexOf("qindatk.richtext") >= 0,
               "plain draft bodies are promoted into the native rich-text document format")
        const editor = findChild(composer, "composerBody")
        verify(editor)
        editor.area.cursorPosition = editor.document.text().length
        editor.forceActiveFocus()
        verify(editor.invokeCommand("text.bold"), "formatting command should reach the draft document")
        for (const character of " Bold") keyClick(character)
        compare(composer.snapshot().body, "Edited body Bold")
        const storedDocument = JSON.parse(composer.snapshot().richDocumentJson)
        verify(storedDocument.formats.some(function(format) { return format.weight === 700 }),
               "the saved document must retain the formatting applied in the editor")
        const reopened = createTemporaryObject(composerComponent, root, { draft: Object.assign({id: "draft-stable"}, composer.snapshot()) })
        verify(reopened)
        compare(reopened.richDocument.text(), "Edited body Bold")
        compare(reopened.snapshot().attachments[0].id, "attachment-1")
        compare(reopened.draft.id, "draft-stable")
        const sendSpy = createTemporaryObject(signalSpyComponent, root, { target: composer, signalName: "sendRequested" })
        verify(composer.requestSend())
        compare(sendSpy.count, 1)
        const sendId = sendSpy.signalArguments[0][0]
        verify(composer.acknowledge(sendId, "rejected", "delivery disabled"))
        compare(composer.status, "rejected")
        compare(composer.snapshot().body, "Edited body Bold")

        const calendar = createTemporaryObject(calendarComponent, root)
        calendar.offline = true
        verify(!calendar.navigate("next"))
        calendar.offline = false
        const moveSpy = createTemporaryObject(signalSpyComponent, root, { target: calendar, signalName: "eventChangeRequested" })
        verify(calendar.proposeChange("evt-9", { kind: "shift-local", minutes: 60, zone: "UTC" }))
        compare(moveSpy.signalArguments[0][1], "evt-9")
        compare(moveSpy.signalArguments[0][2].zone, "UTC")

        const recurrence = createTemporaryObject(recurrenceComponent, root)
        const ruleSpy = createTemporaryObject(signalSpyComponent, root, { target: recurrence, signalName: "ruleChangeRequested" })
        recurrence.toggleWeekday(0)
        compare(ruleSpy.signalArguments[0][2].weekdays.length, 2)
        compare(ruleSpy.count, 1)
        verify(recurrence.acknowledge(ruleSpy.signalArguments[0][0], "accepted", "host accepted"))
        compare(recurrence.requestMessage, "host accepted")
        compare(recurrence.preview.length, 0, "the control must not invent occurrences")
        recurrence.preview = [{ occurrenceId: "occ-1", localStart: "2026-09-24 10:30", zone: "UTC" }]
        const exceptionSpy = createTemporaryObject(signalSpyComponent, root, { target: recurrence, signalName: "occurrenceExceptionRequested" })
        verify(recurrence.requestException("occ-1", true))
        compare(exceptionSpy.signalArguments[0][2], "occ-1")

        const board = createTemporaryObject(boardComponent, root)
        const moveTaskSpy = createTemporaryObject(signalSpyComponent, root, { target: board, signalName: "moveRequested" })
        verify(board.move("task-50", "todo", "done"))
        compare(moveTaskSpy.signalArguments[0][1], "task-50")
        compare(moveTaskSpy.signalArguments[0][2], "todo")
        board.offline = true
        verify(!board.move("task-50", "todo", "done"))
    }
}
