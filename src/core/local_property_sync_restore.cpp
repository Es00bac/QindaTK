// SPDX-License-Identifier: LGPL-3.0-or-later
// Version restore and shared checkpoints for LocalPropertySyncClient.
#include "local_property_sync.h"
#include "property_sync_detail_p.h"
#include "reference_document_codec.h"
#include <QDateTime>
#include <algorithm>

namespace QindaTK {
using namespace sync_detail;

bool LocalPropertySyncClient::restoreVersion(const QByteArray &snapshot)
{
    if (!readyToEdit()) return false;
    if (snapshot.size() > MaxSnapshotBytes)
        return fail(QStringLiteral("Restore client or snapshot is invalid"));
    const auto decoded = ReferenceDocumentCodec::decode(snapshot);
    if (!decoded || decoded->document->documentIdString() != m_document->documentIdString())
        return fail(QStringLiteral("Restore snapshot has wrong identity or format"));
    const QString documentId = m_document->documentIdString();
    // AGENT-NOTE: one Restore operation carries every non-text value (and
    // revives entities the version contains). Entities created after the
    // version and text properties follow as ordinary Delete/Text operations
    // from the same actor, so each keeps its own merge rule (D-166, D-167).
    QSet<QString> versionEntities;
    QHash<QString, QString> versionText;
    for (const auto &change : decoded->document->committedValues()) {
        const QString entity = change.target.toString();
        versionEntities.insert(entity);
        if (!isTextProperty(change.propertyId)) continue;
        if (change.after.toString().size() > MaxTextInsert)
            return fail(QStringLiteral("Version text is too long to restore in one step"));
        versionText.insert(keyOf(entity, change.propertyId), change.after.toString());
    }
    if (!submitLocal(makeOperation(Kind::Restore, documentId, restoreProperty(), snapshot)))
        return false;
    QStringList extra;
    for (const auto &change : m_document->committedValues()) {
        const QString entity = change.target.toString();
        if (!versionEntities.contains(entity) && !extra.contains(entity)) extra.append(entity);
    }
    for (const QString &entity : extra)
        if (!deleteEntityImpl(entity, false)) return false;
    QStringList keys = versionText.keys();
    keys.sort();
    for (const QString &key : keys) {
        const auto split = key.indexOf(QLatin1Char('/'));
        const QString entity = key.left(split);
        const QString property = key.mid(split + 1);
        if (!m_document->contains(EntityId::parse(entity))) continue;
        TextSequence *text = sequence(entity, property);
        if (!text) return false;
        if (visibleText(*text) == versionText.value(key)) continue;
        QStringList visible;
        for (const auto &element : text->elements)
            if (!element.deleted) visible.append(element.id);
        if (!textEdit(entity, property, visible, versionText.value(key), {}, false))
            return false;
    }
    return true;
}

bool LocalPropertySyncClient::receiveRestore(const PropertySyncOperation &operation)
{
    const auto decoded = ReferenceDocumentCodec::decode(operation.value.toByteArray());
    if (!decoded || decoded->document->documentIdString() != m_document->documentIdString())
        return fail(QStringLiteral("Invalid restore snapshot"));
    QVector<PropertyChange> apply;
    QVector<QPair<QString, PropertySyncOperation>> won;
    QStringList touched;
    for (const auto &change : decoded->document->committedValues()) {
        if (isTextProperty(change.propertyId)) continue;   // restored by text operations
        const auto descriptor = m_document->descriptor(change.propertyId);
        if (!descriptor || descriptor->type != change.after.metaType())
            return fail(QStringLiteral("Restore snapshot schema differs"));
        const QString entity = change.target.toString();
        const QString key = keyOf(entity, change.propertyId);
        const auto winner = m_winners.constFind(key);
        if (winner != m_winners.cend() && !outranks(operation, winner.value())) continue;
        // The stored winner keeps the restore's identity and this key's value.
        PropertySyncOperation stored = operation;
        stored.value = change.after;
        won.append({key, stored});
        if (!touched.contains(entity)) touched.append(entity);
        if (m_document->contains(change.target)) apply.append(change);
    }
    // AGENT-GUARD: validate and publish every winning restored value in one
    // document revision, then advance per-property winner metadata together.
    if (auto error = m_document->applySynchronizedValues(apply)) return fail(error.message);
    m_expectedRevision = m_document->revision().value;
    for (const auto &[key, stored] : won) {
        const auto previous = m_winners.constFind(key);
        const bool lost = previous != m_winners.cend() && previous->actorId == m_actorId
            && operation.actorId != m_actorId;
        const QString lostId = lost ? previous->operationId : QString();
        m_winners.insert(key, stored);
        if (lost) emit operationLost(lostId, operation.operationId);
    }
    // A version that contains a deleted entity revives it with those values.
    touched.sort();
    for (const QString &entity : touched)
        if (!reconcileEntity(entity)) return false;
    return true;
}

QString LocalPropertySyncClient::shareCheckpoint(const QString &name, const QByteArray &snapshot)
{
    const QString trimmed = name.trimmed();
    if (!readyToEdit()) return {};
    if (trimmed.isEmpty() || trimmed.size() > 128 || snapshot.size() > MaxSnapshotBytes) {
        fail(QStringLiteral("Checkpoint name or snapshot is invalid"));
        return {};
    }
    const auto decoded = ReferenceDocumentCodec::decode(snapshot);
    if (!decoded || decoded->document->documentIdString() != m_document->documentIdString()) {
        fail(QStringLiteral("Checkpoint snapshot has wrong identity or format"));
        return {};
    }
    const QString meta = encodeCheckpointMeta(
        {trimmed, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)});
    const auto operation = makeOperation(Kind::Checkpoint, m_document->documentIdString(),
                                         checkpointProperty(), snapshot, meta);
    submitLocal(operation);
    return m_seen.contains(operation.operationId) ? operation.operationId : QString();
}

} // namespace QindaTK
