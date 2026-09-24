// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.AppWindow {
    id: window
    title: qsTr("Local attachment store")
    width: 820
    height: 520
    property string attachmentId: "draft-local-1/attachment-note"
    property var draft: ({ id: "draft-local-1", subject: "Field notes", attachments: [] })
    property string resultText: qsTr("Checking local attachment bytes…")

    function refreshAttachment() {
        const existing = attachmentStore.read(attachmentId)
        if (!existing.ok) {
            const imported = attachmentStore.importFile(attachmentId, Qt.resolvedUrl("sample-note.txt"))
            if (!imported.ok) { resultText = imported.error; return }
        }
        const payload = attachmentStore.read(attachmentId)
        if (!payload.ok) { resultText = payload.error; return }
        draft = Object.assign({}, draft, { attachments: [{ id: payload.id, name: payload.name,
            mimeType: payload.mimeType, size: payload.size, state: "local" }] })
        resultText = qsTr("Persisted %1 bytes · verified by rereading local payload").arg(payload.size)
    }

    Tk.LocalAttachmentStore {
        id: attachmentStore
        rootPath: defaultPath + "/drafts"
    }
    Component.onCompleted: refreshAttachment()

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Overline { title: qsTr("Local attachment bytes"); wide: true }
        Tk.Heading { level: 2; text: window.draft.subject }
        Tk.Notice {
            variant: "info"
            title: qsTr("Draft snapshot reference")
            text: qsTr("The draft owns stable attachment metadata; LocalAttachmentStore owns and atomically writes the payload bytes.")
        }
        Tk.Panel {
            title: qsTr("Attachments · %1").arg(window.draft.attachments.length)
            Tk.Flex {
                anchors.fill: parent
                padding: Tk.Theme.space.md
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.Label { text: window.draft.attachments.length ? window.draft.attachments[0].name : qsTr("No attachment") }
                Tk.Caption { text: window.draft.attachments.length ? qsTr("%1 · %2 bytes · ID %3").arg(window.draft.attachments[0].mimeType).arg(window.draft.attachments[0].size).arg(window.draft.attachments[0].id) : ""; color: Tk.Theme.color.textMuted; wrapMode: Text.Wrap }
                Tk.Caption { objectName: "attachmentStoreStatus"; text: window.resultText + " · " + attachmentStore.status; color: attachmentStore.lastError.length ? Tk.Theme.color.warning : Tk.Theme.color.textMuted; wrapMode: Text.Wrap }
                Tk.Button { text: qsTr("Read persisted bytes again"); tooltip: qsTr("Read the payload associated with this stable attachment ID"); onClicked: window.refreshAttachment() }
                Tk.Spacer { Tk.Flex.grow: 1 }
            }
        }
        Tk.Notice { variant: "warning"; title: qsTr("Local only"); text: qsTr("This fixture writes one sample file under the application data directory. It does not send a draft or upload the attachment.") }
    }
}
