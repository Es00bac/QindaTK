// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// All five productivity views over one local store. ProductivityHost turns
// every view request into a store edit or a provider operation and returns
// the result through the view's acknowledge(). No account is configured
// here, so mail refresh and send reach the real providers and come back
// rejected by them; tst_productivity_seam runs this same file against local
// SMTP, IMAP and CalDAV test servers.
Item {
    id: root
    width: 1600
    height: 1060
    property alias storePath: host.storePath
    property alias attachmentPath: host.attachmentPath
    property alias smtpSettings: host.smtpSettings
    property alias imapSettings: host.imapSettings
    property alias caldavSettings: host.caldavSettings
    readonly property alias host: host

    ProductivityHost {
        id: host
        mail: mail
        composer: composer
        calendar: calendar
        board: board
        recurrence: recurrence
    }
    Component.onCompleted: host.reload()

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Local productivity") }
                Tk.Caption { text: qsTr("Messages · drafts · events · tasks are stored on this device; providers carry them to mail and calendar servers"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button { text: qsTr("Reload from disk"); tooltip: qsTr("Reload persisted local records"); onClicked: host.reload() }
            Tk.Button { objectName: "undoEventChange"; text: qsTr("Undo calendar change"); tooltip: qsTr("Undo the last local calendar change"); enabled: host.store.canUndoEventChange; onClicked: { const result = host.store.undoEventChange(); if (result.ok && host.store.save()) host.message = qsTr("Calendar change undone and saved") } }
            Tk.Button { objectName: "undoTaskMove"; text: qsTr("Undo task move"); tooltip: qsTr("Undo the last local task move"); enabled: host.store.canUndoTaskMove; onClicked: { const result = host.store.undoTaskMove(); if (result.ok && host.store.save()) host.message = qsTr("Task move undone and saved") } }
            Tk.Badge { text: host.caldav.collectionUrl ? qsTr("Account connected") : qsTr("Local only"); variant: "info" }
        }
        Tk.Caption { objectName: "localStoreStatus"; text: host.message + " · " + host.store.status + " · revision " + host.store.revision; color: host.store.lastError.length ? Tk.Theme.color.warning : Tk.Theme.color.textMuted; wrapMode: Text.Wrap; width: parent.width }
        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1fr) minmax(0, 1fr) minmax(0, 1fr)"
            rows: "minmax(0, 1fr) minmax(0, 1fr)"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.sm
            Tk.Panel {
                title: qsTr("Messages"); iconName: "archive"
                Tk.Scroll {
                    anchors.fill: parent
                    Tk.MailView {
                        id: mail
                        width: parent.width
                        messages: host.store.messages.filter(function(row) { return !row.archived })
                        selectedMessageId: "message-welcome-2"
                        syncState: host.imap.busy ? "syncing" : host.imap.host ? "connected" : "local"
                        onRefreshRequested: function(requestId) { host.refreshMail(requestId) }
                        onActionRequested: function(requestId, messageId, action) { host.mailAction(requestId, messageId, action) }
                        onAttachmentRequested: function(requestId, messageId, attachmentId) { host.imapFetch(requestId, messageId, attachmentId) }
                        onAttachmentOpenRequested: function(messageId, attachmentId) { host.openAttachment(messageId, attachmentId) }
                        onReplyRequested: function(messageId) { host.reply(messageId) }
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Draft composer"); iconName: "file-plus"
                Tk.Scroll {
                    anchors.fill: parent
                    Tk.RichMailComposer {
                        id: composer
                        width: parent.width
                        online: true
                        onDraftChangeRequested: function(field, value) { host.persistDraft(false) }
                        onSaveDraftRequested: function(requestId, draft) { host.persistDraft(true) }
                        onSendRequested: function(requestId, draft) { host.send(requestId, draft) }
                        onAttachmentAddRequested: function(requestId) { host.addAttachment(requestId) }
                        onAttachmentRemoveRequested: function(requestId, attachmentId) { host.removeAttachment(requestId, attachmentId) }
                    }
                }
            }
            Tk.Panel {
                Tk.Grid.rowSpan: 2
                title: qsTr("Calendar"); iconName: "calendar"
                Tk.Scroll {
                    anchors.fill: parent
                    Tk.Flex {
                        width: parent.width
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.sm
                        Tk.CalendarView {
                            id: calendar
                            width: parent.width
                            events: host.projector.occurrences
                            rangeLabel: host.rangeLabel()
                            zone: "America/Denver"
                            errorText: host.projector.error
                            onNavigateRequested: function(requestId, direction) { calendar.acknowledge(requestId, host.moveRange(direction) ? "accepted" : "rejected", host.projector.error) }
                            onEventChangeRequested: function(requestId, eventId, proposed) { host.moveEvent(requestId, eventId, proposed) }
                            onEventSelected: function(eventId) { host.selectRecurrence(eventId) }
                        }
                        Tk.Divider { width: parent.width }
                        Tk.RecurrenceEditor {
                            id: recurrence
                            width: parent.width
                            eventId: host.recurrenceEventId
                            zone: "America/Denver"
                            startLocal: host.recurrenceEventId ? String((host.record("events", host.recurrenceEventId) || {}).startLocal || "") : ""
                            onRuleChangeRequested: function(requestId, eventId, proposed) { host.changeRule(requestId, eventId, proposed) }
                            onPreviewRequested: function(requestId, eventId, rule) { host.previewRecurrence(requestId) }
                            onOccurrenceExceptionRequested: function(requestId, eventId, occurrenceId, excluded) { host.excludeOccurrence(requestId, eventId, occurrenceId, excluded) }
                        }
                    }
                }
            }
            Tk.Panel {
                Tk.Grid.columnSpan: 2
                title: qsTr("Task board"); iconName: "check"
                Tk.Scroll {
                    anchors.fill: parent
                    Tk.TaskBoard {
                        id: board
                        width: parent.width
                        columns: host.boardColumns
                        conflicts: host.caldav.conflicts
                        onMoveRequested: function(requestId, taskId, fromStatus, toStatus, targetIndex) { host.moveTask(requestId, taskId, fromStatus, toStatus, targetIndex) }
                        onConflictResolutionRequested: function(requestId, taskId, choice) { host.resolveTask(requestId, taskId, choice) }
                    }
                }
            }
        }
    }
}
