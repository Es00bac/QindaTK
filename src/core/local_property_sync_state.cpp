// SPDX-License-Identifier: LGPL-3.0-or-later
// Compacted state and rebuild for LocalPropertySyncClient (D-178).
//
// AGENT-NOTE: the state is everything a replica derives from the operations
// it has seen: per-property winners, entity supporters and removed sets, and
// every text sequence with its tombstones and element IDs. A hub stores it
// as its base and drops the folded operations. Every rule stays idempotent,
// so an operation delivered again after compaction changes nothing. Shared
// checkpoints are not folded; they stay operations (the retained tail).
#include "local_property_sync.h"
#include "property_sync_detail_p.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace QindaTK {
using namespace sync_detail;

namespace {
constexpr int MaxStateActors = 4096;
constexpr int MaxStateSequences = 1024;
constexpr int MaxStateWaiting = 1024;
constexpr int MaxStateRemoved = 16 * MaxObserved;

QString stateFormat() { return QStringLiteral("qindatk.sync-state"); }

std::optional<quint64> decimal(const QJsonValue &value)
{
    bool ok = false;
    const quint64 number = value.toString().toULongLong(&ok);
    if (!value.isString() || !ok || QString::number(number) != value.toString())
        return std::nullopt;
    return number;
}

std::optional<int> integer(const QJsonValue &value, int minimum, int maximum)
{
    const double number = value.toDouble(-1.0e12);
    if (!value.isDouble() || number != std::floor(number) || number < minimum || number > maximum)
        return std::nullopt;
    return int(number);
}

QPair<QString, QString> splitKey(const QString &key)
{
    const auto split = key.indexOf(QLatin1Char('/'));
    return {key.left(split), key.mid(split + 1)};
}
} // namespace

QByteArray LocalPropertySyncClient::exportState(const QString &baseId) const
{
    QJsonArray actors;
    QHash<QString, int> actorIndex;
    const auto actor = [&](const QString &id) {
        if (id.isEmpty()) return -1;
        const auto found = actorIndex.constFind(id);
        if (found != actorIndex.cend()) return found.value();
        actorIndex.insert(id, int(actors.size()));
        actors.append(id);
        return int(actors.size() - 1);
    };
    QJsonArray winners;
    QStringList keys = m_winners.keys();
    keys.sort();
    for (const QString &key : std::as_const(keys)) {
        const auto &op = m_winners[key];
        QJsonValue value;
        if (op.kind == Kind::Set || op.kind == Kind::Restore) {
            const auto encoded = encodeValue(op.value);
            if (!encoded) return {};
            value = *encoded;
        }
        const auto [entity, property] = splitKey(key);
        winners.append(QJsonArray{entity, property, op.operationId, actor(op.actorId),
                                  QString::number(op.logicalTime), int(op.kind), value});
    }
    QJsonArray support;
    QStringList entities = m_support.keys();
    entities.sort();
    for (const QString &entity : std::as_const(entities)) {
        const auto &stamps = m_support[entity];
        QStringList ids = stamps.keys();
        ids.sort();
        for (const QString &id : std::as_const(ids))
            support.append(QJsonArray{entity, id, actor(stamps[id].actorId),
                                      QString::number(stamps[id].time)});
    }
    QJsonArray removed;
    entities = m_removed.keys();
    entities.sort();
    for (const QString &entity : std::as_const(entities)) {
        QStringList ids(m_removed[entity].cbegin(), m_removed[entity].cend());
        ids.sort();
        removed.append(QJsonArray{entity, QJsonArray::fromStringList(ids)});
    }
    QJsonArray text;
    keys = m_text.keys();
    keys.sort();
    for (const QString &key : std::as_const(keys)) {
        const TextSequence &sequence = m_text[key];
        // A run is consecutive elements of one insert with consecutive indices.
        QJsonArray runs;
        for (qsizetype i = 0; i < sequence.elements.size();) {
            const TextElement &first = sequence.elements.at(i);
            qsizetype end = i + 1;
            while (end < sequence.elements.size()
                   && sequence.elements.at(end).op == first.op
                   && sequence.elements.at(end).index == first.index + int(end - i))
                ++end;
            QString characters;
            QJsonArray deleted;
            for (qsizetype k = i; k < end; ++k) {
                characters.append(sequence.elements.at(k).character);
                if (!sequence.elements.at(k).deleted) continue;
                const int offset = int(k - i);
                const QJsonArray last = deleted.isEmpty() ? QJsonArray() : deleted.last().toArray();
                if (!last.isEmpty() && last.at(0).toInt() + last.at(1).toInt() == offset)
                    deleted.replace(deleted.size() - 1, QJsonArray{last.at(0), last.at(1).toInt() + 1});
                else
                    deleted.append(QJsonArray{offset, 1});
            }
            runs.append(QJsonArray{first.op, first.index, QString::number(first.time),
                                   actor(first.actorId), characters, deleted});
            i = end;
        }
        QStringList pending(sequence.pendingDeletes.cbegin(), sequence.pendingDeletes.cend());
        pending.sort();
        QJsonArray waiting;
        for (const auto &op : sequence.waiting)
            if (const auto encoded = propertySyncOperationToJson(op)) waiting.append(*encoded);
        const auto [entity, property] = splitKey(key);
        text.append(QJsonArray{entity, property, runs, QJsonArray::fromStringList(pending),
                               waiting});
    }
    return QJsonDocument(QJsonObject{
        {QStringLiteral("format"), stateFormat()}, {QStringLiteral("version"), 1},
        {QStringLiteral("document"), m_document ? m_document->documentIdString() : QString()},
        {QStringLiteral("id"), baseId}, {QStringLiteral("clock"), QString::number(m_clock)},
        {QStringLiteral("actors"), actors}, {QStringLiteral("winners"), winners},
        {QStringLiteral("support"), support}, {QStringLiteral("removed"), removed},
        {QStringLiteral("text"), text}}).toJson(QJsonDocument::Compact);
}

bool LocalPropertySyncClient::importState(const QByteArray &state)
{
    const auto invalid = [this](const char *what) {
        return fail(QStringLiteral("Invalid compacted state: ") + QLatin1String(what));
    };
    if (!m_document || state.size() > MaxStateBytes) return invalid("size");
    const QJsonObject root = QJsonDocument::fromJson(state).object();
    const QString documentId = m_document->documentIdString();
    const auto clock = decimal(root.value(QStringLiteral("clock")));
    if (root.size() != 10 || root.value(QStringLiteral("format")).toString() != stateFormat()
        || root.value(QStringLiteral("version")).toInt() != 1
        || root.value(QStringLiteral("document")).toString() != documentId || !clock)
        return invalid("identity");
    QStringList actors;
    for (const auto &value : root.value(QStringLiteral("actors")).toArray()) {
        if (!canonicalUuid(value.toString()) || actors.contains(value.toString())
            || actors.size() >= MaxStateActors)
            return invalid("actor");
        actors.append(value.toString());
    }
    const auto actorAt = [&](const QJsonValue &value, bool optional) -> std::optional<QString> {
        const auto index = integer(value, optional ? -1 : 0, int(actors.size()) - 1);
        if (!index) return std::nullopt;
        return *index < 0 ? QString() : actors.at(*index);
    };
    QHash<QString, PropertySyncOperation> winners;
    for (const auto &value : root.value(QStringLiteral("winners")).toArray()) {
        const QJsonArray row = value.toArray();
        if (row.size() != 7) return invalid("winner");
        const QString entity = row.at(0).toString();
        const QString property = row.at(1).toString();
        const auto descriptor = m_document->descriptor(property);
        const auto author = actorAt(row.at(3), false);
        const auto time = decimal(row.at(4));
        const auto kind = integer(row.at(5), 0, int(Kind::Checkpoint));
        if (!canonicalUuid(entity) || !descriptor || !canonicalUuid(row.at(2).toString())
            || !author || !time || !kind || winners.contains(keyOf(entity, property)))
            return invalid("winner");
        PropertySyncOperation op{documentId, row.at(2).toString(), *author, *time, entity,
                                 property, {}, Kind(*kind), {}};
        const bool text = op.kind == Kind::TextInsert || op.kind == Kind::TextDelete;
        if (text != isTextProperty(property)
            || (!text && op.kind != Kind::Set && op.kind != Kind::Restore))
            return invalid("winner kind");
        if (!text) {
            const auto decoded = decodeValue(row.at(6).toObject());
            if (!decoded || decoded->metaType() != descriptor->type) return invalid("value");
            op.value = *decoded;
        }
        winners.insert(keyOf(entity, property), op);
    }
    QHash<QString, QHash<QString, Stamp>> support;
    for (const auto &value : root.value(QStringLiteral("support")).toArray()) {
        const QJsonArray row = value.toArray();
        const auto author = actorAt(row.at(2), false);
        const auto time = decimal(row.at(3));
        auto &stamps = support[row.at(0).toString()];
        if (row.size() != 4 || !canonicalUuid(row.at(0).toString())
            || !canonicalUuid(row.at(1).toString()) || !author || !time
            || stamps.size() >= MaxObserved)
            return invalid("support");
        stamps.insert(row.at(1).toString(), {*time, *author});
    }
    QHash<QString, QSet<QString>> removed;
    for (const auto &value : root.value(QStringLiteral("removed")).toArray()) {
        const QJsonArray row = value.toArray();
        const QJsonArray ids = row.at(1).toArray();
        if (row.size() != 2 || !canonicalUuid(row.at(0).toString()) || ids.size() > MaxStateRemoved)
            return invalid("removed");
        auto &set = removed[row.at(0).toString()];
        for (const auto &id : ids) {
            if (!canonicalUuid(id.toString())) return invalid("removed");
            set.insert(id.toString());
        }
    }
    QHash<QString, TextSequence> sequences;
    const QJsonArray textRows = root.value(QStringLiteral("text")).toArray();
    if (textRows.size() > MaxStateSequences) return invalid("text count");
    for (const auto &value : textRows) {
        const QJsonArray row = value.toArray();
        const QString entity = row.at(0).toString();
        const QString property = row.at(1).toString();
        if (row.size() != 5 || !canonicalUuid(entity) || !isTextProperty(property)
            || sequences.contains(keyOf(entity, property)))
            return invalid("text");
        TextSequence sequence;
        QSet<QString> ids;
        for (const auto &runValue : row.at(2).toArray()) {
            const QJsonArray run = runValue.toArray();
            const QString op = run.at(0).toString();
            const bool base = op == baseElementOp();
            const auto start = integer(run.at(1), 0, MaxTextElements - 1);
            const auto time = decimal(run.at(2));
            const auto author = actorAt(run.at(3), base);
            const QString characters = run.at(4).toString();
            if (run.size() != 6 || (!base && !canonicalUuid(op)) || !start || !time || !author
                || base != author->isEmpty() || characters.isEmpty()
                || *start + characters.size() > MaxTextElements
                || sequence.elements.size() + characters.size() > MaxTextElements)
                return invalid("text run");
            const auto first = sequence.elements.size();
            for (int k = 0; k < characters.size(); ++k) {
                const QString id = elementId(op, *start + k);
                if (ids.contains(id)) return invalid("text element");
                ids.insert(id);
                sequence.elements.append({id, characters.at(k), *time, *author, op, *start + k,
                                          false});
            }
            for (const auto &rangeValue : run.at(5).toArray()) {
                const QJsonArray range = rangeValue.toArray();
                const auto offset = integer(range.at(0), 0, int(characters.size()) - 1);
                const auto count = offset ? integer(range.at(1), 1, int(characters.size()) - *offset)
                                          : std::nullopt;
                if (range.size() != 2 || !offset || !count) return invalid("text tombstone");
                for (int k = *offset; k < *offset + *count; ++k)
                    sequence.elements[first + k].deleted = true;
            }
        }
        for (const auto &id : row.at(3).toArray()) {
            if (!parseElementId(id.toString()) || sequence.pendingDeletes.size() >= MaxTextElements)
                return invalid("pending delete");
            sequence.pendingDeletes.insert(id.toString());
        }
        const QJsonArray waiting = row.at(4).toArray();
        if (waiting.size() > MaxStateWaiting) return invalid("waiting insert");
        for (const auto &opValue : waiting) {
            const auto op = propertySyncOperationFromJson(opValue.toObject());
            if (!op || op->kind != Kind::TextInsert || op->documentId != documentId
                || op->entityId != entity || op->propertyId != property)
                return invalid("waiting insert");
            sequence.waiting.append(*op);
        }
        sequences.insert(keyOf(entity, property), sequence);
    }
    m_winners = std::move(winners);
    m_support = std::move(support);
    m_removed = std::move(removed);
    m_text = std::move(sequences);
    m_clock = std::max(m_clock, *clock);
    return true;
}

void LocalPropertySyncClient::clearState()
{
    m_winners.clear();
    m_support.clear();
    m_removed.clear();
    m_text.clear();
    m_checkpoints.clear();
    m_seen.clear();
}

bool LocalPropertySyncClient::rebuild()
{
    if (!m_document || !m_service) return fail(QStringLiteral("Client configuration is incomplete"));
    if (!inSync()) return fail(QStringLiteral("Document changed outside synchronization"));
    // AGENT-GUARD: the clock is never reset, so this actor's next operation
    // still outranks everything it issued before the rebuild.
    clearState();
    m_detached = true;
    QString error;
    if (!m_service->base().isEmpty() && !importState(m_service->base())) error = m_lastError;
    const auto operations = m_service->operations();
    for (const auto &operation : operations)
        if (error.isEmpty() && !receive(operation)) error = m_lastError;
    const auto pending = m_pending;
    for (const auto &operation : pending)
        if (error.isEmpty() && !receive(operation)) error = m_lastError;
    m_detached = false;
    m_syncedBaseId = m_service->baseId();
    const bool materialized = materialize();
    emit rebuilt();
    emit changed();
    if (!error.isEmpty()) return fail(error);
    return materialized;
}

bool LocalPropertySyncClient::materialize()
{
    QSet<QString> candidates = m_baselineEntities;
    for (auto it = m_support.cbegin(); it != m_support.cend(); ++it) candidates.insert(it.key());
    for (auto it = m_removed.cbegin(); it != m_removed.cend(); ++it) candidates.insert(it.key());
    for (auto it = m_winners.cbegin(); it != m_winners.cend(); ++it)
        candidates.insert(splitKey(it.key()).first);
    for (auto it = m_text.cbegin(); it != m_text.cend(); ++it)
        candidates.insert(splitKey(it.key()).first);
    QStringList ordered(candidates.cbegin(), candidates.cend());
    ordered.sort();
    QVector<QPair<EntityId, QVariantMap>> entities;
    for (const QString &entity : std::as_const(ordered)) {
        if (!canonicalUuid(entity) || !entityAlive(entity)) continue;
        const QVariantMap values = entityValues(entity);
        if (values.isEmpty() && !m_document->propertyIds().isEmpty()) return false;
        entities.append({EntityId::parse(entity), values});
    }
    // One reset: views see the rebuilt replica at once, never a half state.
    if (!m_document->resetModel(entities))
        return fail(QStringLiteral("Cannot rebuild the synchronized document"));
    m_expectedRevision = m_document->revision().value;
    m_resetGeneration = m_document->resetGeneration();
    return true;
}

void LocalPropertySyncClient::compacted(const QString &baseId)
{
    // This replica held base + log, so it now stands on the new base. Only
    // pending operations must stay recognisable; the rest are idempotent.
    m_syncedBaseId = baseId;
    QHash<QString, PropertySyncOperation> kept;
    for (const auto &operation : std::as_const(m_pending))
        if (m_seen.contains(operation.operationId)) kept.insert(operation.operationId, operation);
    m_seen = std::move(kept);
}

bool LocalPropertySyncClient::addCheckpoint(const PropertySyncOperation &operation)
{
    for (const auto &existing : std::as_const(m_checkpoints))
        if (existing.operationId == operation.operationId) return true;
    // Oldest first by operation order; the window keeps the newest, so every
    // replica with the same operations lists the same versions.
    const auto position = std::find_if(m_checkpoints.begin(), m_checkpoints.end(),
                                       [&](const PropertySyncOperation &existing) {
                                           return outranks(existing, operation);
                                       });
    m_checkpoints.insert(position, operation);
    if (m_checkpoints.size() > MaxSharedCheckpoints) m_checkpoints.removeFirst();
    return true;
}

} // namespace QindaTK
