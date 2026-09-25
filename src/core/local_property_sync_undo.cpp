// SPDX-License-Identifier: LGPL-3.0-or-later
// Collaborative undo for LocalPropertySyncClient. AGENT-NOTE: UndoService's
// linear inverse chain would overwrite remote edits, so a replica undoes only
// its own steps, each as a new operation, and refuses a step that a newer
// edit has superseded (docs/collaboration.md "Undo", D-169).
#include "local_property_sync.h"
#include "property_sync_detail_p.h"

namespace QindaTK {
using namespace sync_detail;

bool LocalPropertySyncClient::undo()
{
    if (m_undo.isEmpty()) return fail(QStringLiteral("Nothing to undo"));
    if (!readyToEdit()) return false;
    const HistoryEntry entry = m_undo.takeLast();
    const bool done = invert(entry, Record::Undo);
    if (!done) emit undoConflict(entry.operationId, m_lastError);
    emit changed();
    return done;
}

bool LocalPropertySyncClient::redo()
{
    if (m_redo.isEmpty()) return fail(QStringLiteral("Nothing to redo"));
    if (!readyToEdit()) return false;
    const HistoryEntry entry = m_redo.takeLast();
    const bool done = invert(entry, Record::Redo);
    if (!done) emit undoConflict(entry.operationId, m_lastError);
    emit changed();
    return done;
}

bool LocalPropertySyncClient::invert(const HistoryEntry &entry, Record into)
{
    struct Scope {
        Record &target;
        ~Scope() { target = Record::Fresh; }
    } scope{m_recording};
    m_recording = into;
    const bool present = m_document->contains(EntityId::parse(entry.entityId));
    switch (entry.kind) {
    case Kind::Set: {
        const auto winner = m_winners.constFind(keyOf(entry.entityId, entry.propertyId));
        if (!present || winner == m_winners.cend()
            || winner->operationId != entry.operationId)
            return fail(QStringLiteral("A newer edit replaced this value"));
        return setValueImpl(entry.entityId, entry.propertyId, entry.previous, true);
    }
    case Kind::Create:
        if (!present) return fail(QStringLiteral("The object was already deleted"));
        return deleteEntityImpl(entry.entityId, true);
    case Kind::Delete:
        if (present) return fail(QStringLiteral("The object was already restored"));
        return reviveEntity(entry.entityId, true);
    case Kind::TextInsert: {
        if (!present) return fail(QStringLiteral("The object was deleted"));
        TextSequence *text = sequence(entry.entityId, entry.propertyId);
        if (!text) return false;
        QStringList visible;
        int firstRemoved = -1;
        for (int i = 0; i < text->elements.size(); ++i) {
            const auto &element = text->elements.at(i);
            if (!element.deleted && entry.elements.contains(element.id))
                visible.append(element.id);
            if (firstRemoved < 0 && !entry.removed.isEmpty()
                && element.id == entry.removed.first())
                firstRemoved = i;
        }
        // Removed characters return right before the first removed element.
        const QString anchor = firstRemoved > 0 ? text->elements.at(firstRemoved - 1).id
                                                : QString();
        if (visible.isEmpty() && entry.text.isEmpty())
            return fail(QStringLiteral("Other edits already removed this text"));
        return textEdit(entry.entityId, entry.propertyId, visible, entry.text, anchor, true);
    }
    case Kind::Restore:
    case Kind::TextDelete:
    case Kind::Checkpoint:
        break;
    }
    return fail(QStringLiteral("This step cannot be undone"));
}

} // namespace QindaTK
