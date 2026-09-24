// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_property_sync.h"
#include "reference_document_codec.h"
#include <QUuid>
#include <algorithm>
#include <limits>

namespace QindaTK {
namespace {
bool outranks(const PropertySyncOperation &a, const PropertySyncOperation &b)
{
    if (a.logicalTime != b.logicalTime) return a.logicalTime > b.logicalTime;
    if (a.actorId != b.actorId) return a.actorId > b.actorId;
    return a.operationId > b.operationId;
}

QString keyOf(const PropertyChange &change)
{
    return change.target.toString() + QLatin1Char('/') + change.propertyId;
}
}

bool LocalPropertySyncClient::restoreVersion(const QByteArray &snapshot)
{
    if (!m_document || !m_service || m_actorId.isEmpty()
        || m_document->documentIdString() != m_service->documentId()
        || m_clock == std::numeric_limits<quint64>::max()
        || m_pending.size() >= 4096 || snapshot.size() > 40 * 1024)
        return fail(QStringLiteral("Restore client or snapshot is invalid"));
    if (m_document->revision().value != m_expectedRevision
        || m_document->resetGeneration() != m_resetGeneration)
        return fail(QStringLiteral("Document changed outside synchronization"));
    const auto decoded = ReferenceDocumentCodec::decode(snapshot);
    if (!decoded || decoded->document->documentIdString() != m_document->documentIdString())
        return fail(QStringLiteral("Restore snapshot has wrong identity or format"));
    PropertySyncOperation operation{m_document->documentIdString(),
        QUuid::createUuid().toString(QUuid::WithoutBraces), m_actorId,
        ++m_clock, m_document->documentIdString(), QStringLiteral("__qtk.restore.v1"),
        snapshot, PropertySyncOperation::Kind::Restore};
    if (!receive(operation)) return false;
    m_pending.append(operation);
    emit changed();
    return !m_connected || flush();
}

bool LocalPropertySyncClient::receiveRestore(const PropertySyncOperation &operation)
{
    const auto decoded = ReferenceDocumentCodec::decode(operation.value.toByteArray());
    if (!decoded || decoded->document->documentIdString() != m_document->documentIdString())
        return fail(QStringLiteral("Invalid restore snapshot"));
    const auto current = m_document->committedValues();
    const auto target = decoded->document->committedValues();
    if (current.size() != target.size())
        return fail(QStringLiteral("Restore snapshot structure differs"));
    QVector<PropertyChange> apply;
    QVector<QString> winners;
    for (qsizetype i = 0; i < current.size(); ++i) {
        if (current.at(i).target != target.at(i).target
            || current.at(i).propertyId != target.at(i).propertyId
            || current.at(i).after.metaType() != target.at(i).after.metaType())
            return fail(QStringLiteral("Restore snapshot schema differs"));
        const QString key = keyOf(target.at(i));
        const auto winner = m_winners.constFind(key);
        if (winner != m_winners.cend() && !outranks(operation, winner.value())) continue;
        winners.append(key);
        if (current.at(i).after != target.at(i).after)
            apply.append(target.at(i));
    }
    // AGENT-GUARD: validate and publish every winning restored value in one
    // document revision, then advance per-property winner metadata together.
    if (auto error = m_document->applySynchronizedValues(apply)) return fail(error.message);
    m_expectedRevision = m_document->revision().value;
    for (const QString &key : winners) {
        const auto previous = m_winners.constFind(key);
        if (previous != m_winners.cend() && previous->actorId == m_actorId
            && operation.actorId != m_actorId)
            emit operationLost(previous->operationId, operation.operationId);
        m_winners.insert(key, operation);
    }
    m_seen.insert(operation.operationId, operation);
    m_clock = std::max(m_clock, operation.logicalTime);
    emit changed();
    return true;
}

} // namespace QindaTK
