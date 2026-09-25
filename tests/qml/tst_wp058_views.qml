// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// View-level WP-058 behaviour: conversation threading, lazy attachments and
// flag/read/reply actions (OFF-28); recipient tokens and attachment jobs
// (OFF-29); scoped calendar moves (OFF-30); recurrence edits persisted in
// the local store and re-expanded (OFF-31); keyboard/pointer reorder and
// conflict reporting on the task board (OFF-32).
TestCase {
    id: root
    name: "Wp058Views"
    visible: true
    when: windowShown
    width: 1100
    height: 800

    readonly property var threadMessages: [
        { messageId: "m-1", threadId: "t-1", sender: "Ada", subject: "Plan", date: "2026-09-24T08:00:00Z", unread: true,
          attachments: [{ id: "m-1/2", name: "plan.pdf", mimeType: "application/pdf", size: 900, state: "remote" }] },
        { messageId: "m-2", threadId: "t-1", sender: "Bob", subject: "Re: Plan", date: "2026-09-24T09:00:00Z", unread: false,
          attachments: [{ id: "m-2/2", name: "notes.txt", mimeType: "text/plain", size: 20, state: "remote" }] },
        { messageId: "m-3", threadId: "t-2", sender: "Cy", subject: "Lunch", date: "2026-09-23T12:00:00Z", flagged: true, attachments: [] }]

    Component { id: mailComponent; Tk.MailView { width: 520; messages: root.threadMessages } }
    Component { id: composerComponent; Tk.RichMailComposer { width: 560; online: true; draft: ({ to: "", cc: "", subject: "Review", body: "Body", attachments: [] }) } }
    Component { id: calendarComponent; Tk.CalendarView { width: 640 } }
    Component { id: recurrenceComponent; Tk.RecurrenceEditor { width: 640 } }
    Component {
        id: boardComponent
        Tk.TaskBoard {
            width: 900
            columns: [{ status: "todo", label: "To do", tasks: [{ taskId: "a", title: "Alpha" }, { taskId: "b", title: "Beta" }, { taskId: "c", title: "Gamma" }] },
                      { status: "doing", label: "Doing", tasks: [{ taskId: "d", title: "Delta", syncState: "conflict" }] }]
        }
    }
    Component { id: storeComponent; Tk.LocalProductivityStore { } }
    Component { id: serviceComponent; Tk.RecurrenceService { } }
    Component { id: projectorComponent; Tk.LocalCalendarProjectionModel { } }
    Component { id: spyComponent; SignalSpy { } }

    // Flex/Grid place children during polish; let geometry settle before clicking.
    function settle(item) { waitForRendering(item); wait(0) }
    function spy(target, signalName) { return createTemporaryObject(spyComponent, root, { target: target, signalName: signalName }) }

    function test_mail_threads_lazy_attachments_and_actions() {
        const mail = createTemporaryObject(mailComponent, root)
        compare(mail.threads.length, 2)
        compare(mail.threads[0].threadId, "t-1", "newest conversation first")
        compare(mail.rows.length, 2, "a collapsed conversation and a single message")
        const thread = findChild(mail, "mailThread_t-1")
        verify(thread)
        compare(thread.text, "Plan · 2 messages")
        mouseClick(thread)
        compare(mail.rows.length, 4)
        compare(mail.rows[1].message.messageId, "m-1", "messages inside a conversation read oldest first")
        tryVerify(function() { return findChild(mail, "mailMessage_m-2") !== null })
        mouseClick(findChild(mail, "mailMessage_m-2"))
        compare(mail.selectedMessageId, "m-2")

        // Lazy attachment: only the requested attachment changes state, and a
        // late answer after the reader moved on does not touch the new reader.
        const attachmentSpy = spy(mail, "attachmentRequested")
        const requestId = mail.requestAttachment("m-2", "m-2/2")
        compare(attachmentSpy.count, 1)
        compare(attachmentSpy.signalArguments[0][1], "m-2")
        compare(attachmentSpy.signalArguments[0][2], "m-2/2")
        compare(mail.attachmentState("m-2", mail.messages[1].attachments[0]), "loading")
        verify(!mail.requestAttachment("m-2", "m-2/2"), "a loading attachment is not requested twice")
        verify(mail.selectMessage("m-1"))
        verify(mail.acknowledgeAttachment(requestId, "accepted", ""))
        verify(!mail.acknowledgeAttachment(requestId, "accepted", ""), "one acknowledgment per job")
        compare(mail.attachmentState("m-2", mail.messages[1].attachments[0]), "local")
        compare(mail.attachmentState("m-1", mail.messages[0].attachments[0]), "remote")
        const row = findChild(mail, "mailAttachment_m-1/2")
        verify(row)
        compare(findChild(row, "mailAttachmentState").text, "900 bytes · on server")
        const failed = mail.requestAttachment("m-1", "m-1/2")
        verify(mail.acknowledgeAttachment(failed, "offline", "Server unreachable"))
        compare(mail.attachmentState("m-1", mail.messages[0].attachments[0]), "failed")
        mail.messages = root.threadMessages.map(function(message) {
            return message.messageId !== "m-1" ? message : Object.assign({}, message, { attachments: [Object.assign({}, message.attachments[0], { state: "local" })] })
        })
        const openSpy = spy(mail, "attachmentOpenRequested")
        compare(mail.requestAttachment("m-1", "m-1/2"), "open", "saved bytes open instead of downloading again")
        compare(openSpy.count, 1)
        mail.offline = true
        verify(!mail.requestAttachment("m-2", "m-2/2"), "downloads need a connection")
        mail.offline = false

        // Read, flag and reply are explicit requests for the selected message.
        const actionSpy = spy(mail, "actionRequested")
        settle(mail)
        mouseClick(findChild(mail, "mailToggleRead"))
        compare(actionSpy.signalArguments[0][2], "read")
        verify(mail.acknowledge(actionSpy.signalArguments[0][0], "accepted", ""))
        mouseClick(findChild(mail, "mailToggleFlag"))
        compare(actionSpy.signalArguments[1][2], "flag")
        verify(mail.acknowledge(actionSpy.signalArguments[1][0], "conflict", "Mailbox rebuilt"))
        verify(findChild(mail, "mailError").visible)
        const replySpy = spy(mail, "replyRequested")
        mouseClick(findChild(mail, "mailReply"))
        compare(replySpy.signalArguments[0][0], "m-1")
    }

    function test_composer_recipients_validate_without_losing_focus() {
        const composer = createTemporaryObject(composerComponent, root)
        const changeSpy = spy(composer, "draftChangeRequested")
        const input = composer.toField.inputItem
        input.forceActiveFocus()
        verify(input.activeFocus)
        for (const character of "Ada <ada@example.test>,") keyClick(character)
        compare(composer.toField.tokens.length, 1)
        compare(composer.toField.tokens[0].valid, true)
        compare(composer.toField.tokens[0].value, "ada@example.test")
        verify(input.activeFocus, "committing a recipient keeps focus in the recipient field")
        verify(composer.validDraft)
        compare(changeSpy.signalArguments[changeSpy.count - 1][0], "to")
        compare(composer.snapshot().to, "Ada <ada@example.test>")

        for (const character of "not an address,") keyClick(character)
        compare(composer.toField.tokens.length, 2)
        compare(composer.toField.tokens[1].valid, false)
        verify(input.activeFocus)
        verify(composer.recipientError.indexOf("not an address") >= 0)
        verify(!composer.validDraft)
        verify(!findChild(composer, "composerSend").enabled)
        verify(!composer.requestSend(), "an invalid recipient blocks the send request")
        keyClick(Qt.Key_Backspace)
        keyClick(Qt.Key_Backspace)
        compare(composer.toField.tokens.length, 1)
        verify(composer.validDraft)
        verify(input.activeFocus)

        // An address typed without a separator is committed before sending.
        composer.ccField.draft = "cy@example.test"
        const sendSpy = spy(composer, "sendRequested")
        verify(composer.requestSend())
        compare(sendSpy.signalArguments[0][1].cc, "cy@example.test")

        // Bcc: hidden by default, shown by a draft that carries one, sent
        // in the snapshot, and an invalid Bcc blocks the send like To/Cc.
        compare(findChild(composer, "composerBccRow").visible, false)
        composer.draft = { id: "d-bcc", to: "Ada <ada@example.test>", cc: "", bcc: "boss@example.test", subject: "S", body: "b" }
        verify(findChild(composer, "composerBccRow").visible)
        compare(composer.snapshot().bcc, "boss@example.test")
        composer.bccField.draft = "nope"
        composer.commitRecipientDrafts()
        verify(!composer.validDraft)
        // Presentation for a composer window: no heading or action row.
        composer.headerVisible = false
        composer.actionsVisible = false
        compare(findChild(composer, "composerTitle").visible, false)
        compare(findChild(composer, "composerSend").visible, false)
        compare(findChild(composer, "composerSend").text, "Send")
        // Reloading the same draft keeps the token objects in place.
        const tokenId = composer.toField.tokens[0].id
        composer.draft = Object.assign({}, composer.snapshot())
        compare(composer.toField.tokens[0].id, tokenId)
    }

    function test_composer_attachment_jobs() {
        const composer = createTemporaryObject(composerComponent, root)
        const addSpy = spy(composer, "attachmentAddRequested")
        const removeSpy = spy(composer, "attachmentRemoveRequested")
        const first = composer.requestAttachmentAdd()
        compare(addSpy.count, 1)
        compare(composer.attachmentJobs.length, 1)
        verify(composer.acknowledgeAttachment(first, "rejected", "File is larger than 8 MiB"))
        compare(composer.snapshot().attachments.length, 0, "a failed job leaves the draft unchanged")
        compare(composer.attachmentJobs[0].state, "rejected")
        const second = composer.requestAttachmentAdd()
        verify(composer.acknowledgeAttachment(second, "accepted", "", { id: "att-1", name: "agenda.txt", mimeType: "text/plain", size: 42, state: "local" }))
        verify(!composer.acknowledgeAttachment(second, "accepted", "", { id: "att-2" }), "a job is acknowledged once")
        compare(composer.snapshot().attachments.length, 1)
        tryVerify(function() { return findChild(composer, "composerAttachment_att-1") !== null })
        settle(composer)
        mouseClick(findChild(findChild(composer, "composerAttachment_att-1"), "composerAttachmentRemove"))
        compare(removeSpy.signalArguments[0][1], "att-1")
        verify(composer.acknowledgeAttachment(removeSpy.signalArguments[0][0], "accepted", ""))
        compare(composer.snapshot().attachments.length, 0)
        composer.dismissAttachmentJob(first)
        compare(composer.attachmentJobs.length, 0)
    }

    function test_calendar_moves_carry_explicit_scope() {
        const calendar = createTemporaryObject(calendarComponent, root, { events: [
            { eventId: "weekly", occurrenceId: "weekly/2026-03-12T09:00:00", title: "Review", startLocal: "2026-03-12T09:00:00", zone: "America/Denver", recurrenceRule: { frequency: "weekly" } },
            { eventId: "offsite", occurrenceId: "offsite/2026-03-07", title: "Offsite", startLocal: "2026-03-07", allDay: true, zone: "America/Denver" }] })
        verify(calendar.hasRecurring)
        const scope = findChild(calendar, "calendarMoveScope")
        verify(scope.visible)
        const moveSpy = spy(calendar, "eventChangeRequested")
        verify(calendar.proposeMove(calendar.events[0], 1))
        compare(moveSpy.signalArguments[0][2].scope, "occurrence")
        compare(moveSpy.signalArguments[0][2].occurrenceId, "weekly/2026-03-12T09:00:00")
        compare(moveSpy.signalArguments[0][2].minutes, 60)
        calendar.moveScope = "following"
        verify(calendar.proposeMove(calendar.events[0], -1))
        compare(moveSpy.signalArguments[1][2].scope, "following")
        compare(moveSpy.signalArguments[1][2].minutes, -60)
        verify(calendar.proposeMove(calendar.events[1], 1))
        compare(moveSpy.signalArguments[2][2].scope, "series", "a single event always moves as a whole")
        compare(moveSpy.signalArguments[2][2].minutes, 1440, "all-day events move by days")
        verify(calendar.acknowledge(moveSpy.signalArguments[2][0], "rejected", "Event shift enters an invalid local time"))
        compare(calendar.requestMessage, "Event shift enters an invalid local time")
        calendar.select(calendar.events[0])
        compare(calendar.selectedOccurrenceId, "weekly/2026-03-12T09:00:00")
    }

    // Req 3: RecurrenceEditor requests persist in the store's event record and
    // the projection re-expands them.
    function test_recurrence_editor_persists_through_store() {
        const store = createTemporaryObject(storeComponent, root)
        const service = createTemporaryObject(serviceComponent, root)
        const projector = createTemporaryObject(projectorComponent, root, { store: store, recurrenceService: service })
        verify(store.upsert("events", { id: "ev", eventId: "ev", title: "Review", startLocal: "2026-01-31T09:00:00", endLocal: "2026-01-31T10:00:00",
                                        zone: "America/Denver", recurrenceRule: { frequency: "weekly" }, exceptions: [] }).ok)
        verify(projector.project("2026-01-01T00:00:00Z", "2026-06-01T00:00:00Z"))
        const editor = createTemporaryObject(recurrenceComponent, root, { eventId: "ev", zone: "America/Denver", startLocal: "2026-01-31T09:00:00",
                                                                          rule: { frequency: "weekly", interval: 1, weekdays: [], endMode: "never", exceptions: [] } })
        editor.ruleChangeRequested.connect(function(requestId, eventId, proposed) {
            const result = store.setEventRecurrence(eventId, proposed)
            editor.rule = Object.assign({}, store.events[0].recurrenceRule, { exceptions: store.events[0].exceptions })
            editor.acknowledge(requestId, result.ok ? "accepted" : "rejected", result.error)
        })
        editor.occurrenceExceptionRequested.connect(function(requestId, eventId, occurrenceId, excluded) {
            const result = store.setOccurrenceExcluded(eventId, occurrenceId, excluded)
            editor.rule = Object.assign({}, store.events[0].recurrenceRule, { exceptions: store.events[0].exceptions })
            editor.acknowledge(requestId, result.ok ? "accepted" : "rejected", result.error)
        })
        editor.propose("frequency", "monthly")
        compare(editor.requestState, "accepted")
        compare(store.events[0].recurrenceRule.frequency, "monthly")
        compare(findChild(editor, "recurrenceSkipNote").text, "Months without day 31 are skipped.")
        compare(projector.occurrences.map(function(row) { return row.startLocal.slice(0, 10) }),
                ["2026-01-31", "2026-03-31", "2026-05-31"])
        verify(editor.requestException("ev/2026-03-31T09:00:00", true))
        compare(store.events[0].exceptions, ["2026-03-31T09:00:00"])
        compare(projector.occurrences.length, 2)
        tryVerify(function() { return findChild(editor, "recurrenceRestore") !== null })
        mouseClick(findChild(editor, "recurrenceRestore"))
        compare(editor.requestState, "accepted")
        compare(projector.occurrences.length, 3)
        editor.propose("frequency", "hourly")
        compare(editor.requestState, "rejected")
        compare(store.events[0].recurrenceRule.frequency, "monthly")
        editor.startLocal = "2028-02-29T09:00:00"
        editor.rule = { frequency: "yearly" }
        compare(findChild(editor, "recurrenceSkipNote").text, "Repeats only in leap years.")
    }

    function test_task_board_keyboard_pointer_and_conflicts() {
        const board = createTemporaryObject(boardComponent, root)
        const moveSpy = spy(board, "moveRequested")
        const alpha = findChild(board, "taskCard_a")
        alpha.forceActiveFocus()
        keyClick(Qt.Key_Down, Qt.ControlModifier)
        compare(moveSpy.count, 1)
        compare(moveSpy.signalArguments[0][2], "todo")
        compare(moveSpy.signalArguments[0][3], "todo")
        compare(moveSpy.signalArguments[0][4], 1, "Ctrl+Down reorders within the column")
        keyClick(Qt.Key_Right, Qt.ControlModifier)
        compare(moveSpy.signalArguments[1][3], "doing")
        compare(moveSpy.signalArguments[1][4], -1)
        keyClick(Qt.Key_Up, Qt.ControlModifier)
        compare(moveSpy.count, 2, "the first card cannot move further up")
        mouseClick(findChild(findChild(board, "taskCard_c"), "taskMoveUp"))
        compare(moveSpy.signalArguments[2][1], "c")
        compare(moveSpy.signalArguments[2][4], 1)

        // Pointer: drag Gamma onto the top half of Delta in the Doing column.
        const gamma = findChild(board, "taskCard_c")
        const delta = findChild(board, "taskCard_d")
        const target = gamma.mapFromItem(delta, delta.width / 2, delta.height / 4)
        mousePress(gamma, 10, 10)
        for (let step = 1; step <= 10; ++step)
            mouseMove(gamma, 10 + (target.x - 10) * step / 10, 10 + (target.y - 10) * step / 10)
        compare(board.dropStatus, "doing")
        compare(board.dropIndex, 0)
        mouseRelease(gamma, target.x, target.y)
        compare(moveSpy.count, 4)
        compare(moveSpy.signalArguments[3][1], "c")
        compare(moveSpy.signalArguments[3][2], "todo")
        compare(moveSpy.signalArguments[3][3], "doing")
        compare(moveSpy.signalArguments[3][4], 0)
        compare(board.dragTaskId, "")

        // Conflicts: the card is marked, the provider's entry is listed with
        // both choices, and a "conflict" acknowledgment is reported.
        verify(findChild(delta, "taskSyncState").visible)
        board.conflicts = [{ collection: "tasks", id: "d", remoteFields: { title: "Delta (server)", status: "done" } },
                           { collection: "events", id: "e" }]
        compare(board.taskConflicts.length, 1)
        verify(findChild(board, "taskConflicts").visible)
        const resolveSpy = spy(board, "conflictResolutionRequested")
        mouseClick(findChild(findChild(board, "taskConflict_d"), "taskUseRemote"))
        compare(resolveSpy.signalArguments[0][1], "d")
        compare(resolveSpy.signalArguments[0][2], "remote")
        verify(board.acknowledge(resolveSpy.signalArguments[0][0], "conflict", "Changed again on the server"))
        verify(findChild(board, "taskRequestNotice").visible)
        compare(findChild(board, "taskRequestNotice").title, "Changed elsewhere")
    }
}
