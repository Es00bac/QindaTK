// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_version_history.h"
#include "property_sync_detail_p.h"
#include "reference_document_codec.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

namespace QindaTK {
namespace {
// Local versions keep the newest MaxCheckpoints within MaxStoreBytes; a
// shared version is one log operation, so it stays within one packet.
constexpr int MaxCheckpoints = 64;
constexpr qsizetype MaxLocalSnapshotBytes = 1024 * 1024;
constexpr qsizetype MaxStoreBytes = 16 * 1024 * 1024;

bool canonicalUuid(const QString &id)
{
    const QUuid uuid(id);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

QVariantMap valueRow(const PropertyChange &change)
{
    return {{QStringLiteral("entity"), change.target.toString()},
            {QStringLiteral("property"), change.propertyId},
            {QStringLiteral("value"), change.after}};
}
}

void PropertyVersionHistory::setClient(LocalPropertySyncClient *client)
{
    if (m_opened || m_client == client) return;
    m_client = client;
    emit changed();
}

void PropertyVersionHistory::setStoragePath(const QString &path)
{
    if (m_opened || m_storagePath == path) return;
    m_storagePath = path;
    emit changed();
}

void PropertyVersionHistory::setShared(bool shared)
{
    if (m_opened || m_shared == shared) return;
    m_shared = shared;
    emit changed();
}

void PropertyVersionHistory::loadShared()
{
    if (!m_client) return;
    QVector<PropertySyncOperation> operations = m_client->sharedCheckpoints();
    // AGENT-GUARD: sort by the operation order, not arrival, so every replica
    // lists shared versions identically.
    std::sort(operations.begin(), operations.end(),
              [](const PropertySyncOperation &a, const PropertySyncOperation &b) {
                  return sync_detail::outranks(b, a);
              });
    QVector<Entry> loaded;
    for (const auto &operation : operations) {
        const auto meta = sync_detail::decodeCheckpointMeta(operation.meta);
        const auto decoded = ReferenceDocumentCodec::decode(operation.value.toByteArray());
        if (!meta || !decoded) continue;
        loaded.append({operation.operationId, meta->name, operation.actorId,
                       decoded->document->revisionString(), meta->createdAt, 0,
                       operation.value.toByteArray(), QString::number(operation.logicalTime)});
    }
    m_entries = std::move(loaded);
    emit changed();
}

bool PropertyVersionHistory::fail(const QString &message)
{
    m_lastError = message;
    emit changed();
    return false;
}

const PropertyVersionHistory::Entry *PropertyVersionHistory::find(const QString &id) const
{
    for (const auto &entry : m_entries)
        if (entry.id == id) return &entry;
    return nullptr;
}

bool PropertyVersionHistory::open()
{
    if (m_opened) return true;
    if (!m_client || !m_client->document() || !m_client->service()
        || (m_storagePath.isEmpty() && !m_shared))
        return fail(QStringLiteral("Configure a document client and checkpoint path"));
    if (m_shared) {
        m_sharedConnection = connect(m_client, &LocalPropertySyncClient::checkpointShared,
                                     this, &PropertyVersionHistory::loadShared);
        m_opened = true;
        m_lastError.clear();
        loadShared();
        return true;
    }
    QFile file(m_storagePath);
    if (!file.exists()) { m_opened = true; return true; }
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxStoreBytes)
        return fail(QStringLiteral("Cannot read checkpoint store"));
    const auto json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject()) return fail(QStringLiteral("Invalid checkpoint store"));
    const auto root = json.object();
    const auto rows = root.value(QStringLiteral("checkpoints")).toArray();
    if (root.size() != 3 || root.value(QStringLiteral("version")).toInt() != 1
        || root.value(QStringLiteral("document")).toString()
               != m_client->document()->documentIdString()
        || !root.value(QStringLiteral("checkpoints")).isArray()
        || rows.size() > MaxCheckpoints)
        return fail(QStringLiteral("Checkpoint store identity or version mismatch"));
    QVector<Entry> loaded;
    QSet<QString> ids;
    for (const auto &row : rows) {
        if (!row.isObject()) return fail(QStringLiteral("Invalid checkpoint row"));
        const auto object = row.toObject();
        Entry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        entry.name = object.value(QStringLiteral("name")).toString();
        entry.actorId = object.value(QStringLiteral("actor")).toString();
        entry.revision = object.value(QStringLiteral("revision")).toString();
        entry.createdAt = object.value(QStringLiteral("createdAt")).toString();
        entry.operationCount = object.value(QStringLiteral("operationCount")).toInt(-1);
        const QByteArray encoded = object.value(QStringLiteral("snapshot")).toString().toLatin1();
        const auto bytes = QByteArray::fromBase64Encoding(encoded);
        if (object.size() != 7 || !canonicalUuid(entry.id) || ids.contains(entry.id)
            || !canonicalUuid(entry.actorId) || entry.name.isEmpty()
            || entry.name.size() > 128 || entry.operationCount < 0
            || QDateTime::fromString(entry.createdAt, Qt::ISODateWithMs).isValid() == false
            || !bytes || bytes.decoded.toBase64() != encoded
            || bytes.decoded.size() > MaxLocalSnapshotBytes)
            return fail(QStringLiteral("Invalid checkpoint metadata"));
        entry.snapshot = bytes.decoded;
        const auto decoded = ReferenceDocumentCodec::decode(entry.snapshot);
        if (!decoded || decoded->document->documentIdString()
                            != m_client->document()->documentIdString()
            || decoded->document->revisionString() != entry.revision)
            return fail(QStringLiteral("Invalid checkpoint snapshot"));
        ids.insert(entry.id);
        loaded.append(std::move(entry));
    }
    m_entries = std::move(loaded);
    m_opened = true;
    m_lastError.clear();
    emit changed();
    return true;
}

bool PropertyVersionHistory::persist(const QVector<Entry> &entries)
{
    QJsonArray rows;
    for (const auto &entry : entries)
        rows.append(QJsonObject{
            {QStringLiteral("id"), entry.id}, {QStringLiteral("name"), entry.name},
            {QStringLiteral("actor"), entry.actorId},
            {QStringLiteral("revision"), entry.revision},
            {QStringLiteral("createdAt"), entry.createdAt},
            {QStringLiteral("operationCount"), entry.operationCount},
            {QStringLiteral("snapshot"), QString::fromLatin1(entry.snapshot.toBase64())}});
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("document"), m_client->document()->documentIdString()},
        {QStringLiteral("checkpoints"), rows}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxStoreBytes) return fail(QStringLiteral("Checkpoint store is full"));
    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
        || !file.commit()) return fail(QStringLiteral("Cannot commit checkpoint store"));
    return true;
}

QString PropertyVersionHistory::checkpoint(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!m_opened || !m_client || !m_client->document() || !m_client->service()
        || !canonicalUuid(m_client->actorId()) || trimmed.isEmpty()
        || trimmed.size() > 128) {
        fail(QStringLiteral("Checkpoint configuration or name is invalid"));
        return {};
    }
    const auto snapshot = ReferenceDocumentCodec::capture(*m_client->document());
    const qsizetype limit = m_shared ? sync_detail::MaxSnapshotBytes : MaxLocalSnapshotBytes;
    if (!snapshot || snapshot->payload.size() > limit) {
        fail(QStringLiteral("Checkpoint snapshot is unsupported or too large"));
        return {};
    }
    if (m_shared) {
        const QString id = m_client->shareCheckpoint(trimmed, snapshot->payload);
        if (id.isEmpty()) fail(m_client->lastError());
        else m_lastError.clear();
        return id;
    }
    Entry entry{QUuid::createUuid().toString(QUuid::WithoutBraces), trimmed,
                m_client->actorId(), m_client->document()->revisionString(),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                m_client->service()->operationCount(), snapshot->payload, {}};
    // AGENT-NOTE: the history keeps the newest versions instead of refusing
    // new ones, so restore (which saves a safety version first) keeps working
    // for the life of the document. remove() prunes explicitly.
    auto updated = m_entries;
    updated.append(entry);
    while (updated.size() > MaxCheckpoints) updated.removeFirst();
    while (!persist(updated)) {
        if (updated.size() == 1) return {};
        updated.removeFirst();
    }
    m_entries = std::move(updated);
    m_lastError.clear();
    emit changed();
    return entry.id;
}

bool PropertyVersionHistory::remove(const QString &id)
{
    if (!m_opened || m_shared)
        return fail(QStringLiteral("Only a local version history removes versions"));
    auto updated = m_entries;
    const auto removed = std::remove_if(updated.begin(), updated.end(),
                                        [&](const Entry &entry) { return entry.id == id; });
    if (removed == updated.end()) return fail(QStringLiteral("Unknown version"));
    updated.erase(removed, updated.end());
    if (!persist(updated)) return false;
    m_entries = std::move(updated);
    m_lastError.clear();
    emit changed();
    return true;
}

QVariantList PropertyVersionHistory::versions() const
{
    QVariantList result;
    for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it)
        result.append(QVariantMap{{QStringLiteral("id"), it->id},
                                  {QStringLiteral("name"), it->name},
                                  {QStringLiteral("actor"), it->actorId},
                                  {QStringLiteral("revision"), it->revision},
                                  {QStringLiteral("createdAt"), it->createdAt},
                                  {QStringLiteral("operationCount"), it->operationCount},
                                  {QStringLiteral("shared"), !it->logicalTime.isEmpty()},
                                  {QStringLiteral("logicalTime"), it->logicalTime}});
    return result;
}

QVariantMap PropertyVersionHistory::inspect(const QString &id) const
{
    const Entry *entry = find(id);
    if (!entry) return {};
    const auto decoded = ReferenceDocumentCodec::decode(entry->snapshot);
    if (!decoded) return {};
    QVariantList values;
    for (const auto &change : decoded->document->committedValues())
        values.append(valueRow(change));
    return {{QStringLiteral("id"), entry->id}, {QStringLiteral("name"), entry->name},
            {QStringLiteral("actor"), entry->actorId},
            {QStringLiteral("revision"), entry->revision},
            {QStringLiteral("createdAt"), entry->createdAt},
            {QStringLiteral("operationCount"), entry->operationCount},
            {QStringLiteral("values"), values}};
}

QVariantList PropertyVersionHistory::diffSnapshots(const QByteArray &from,
                                                    const QByteArray &to) const
{
    const auto old = ReferenceDocumentCodec::decode(from);
    const auto recent = ReferenceDocumentCodec::decode(to);
    if (!old || !recent) return {};
    // Rows are keyed by entity/property, so versions whose objects differ
    // still compare: an object only on one side reports "added"/"removed".
    QMap<QString, PropertyChange> left;
    QMap<QString, PropertyChange> right;
    for (const auto &change : old->document->committedValues())
        left.insert(change.target.toString() + QLatin1Char('/') + change.propertyId, change);
    for (const auto &change : recent->document->committedValues())
        right.insert(change.target.toString() + QLatin1Char('/') + change.propertyId, change);
    QStringList keys = left.keys() + right.keys();
    keys.removeDuplicates();
    keys.sort();
    QVariantList result;
    for (const QString &key : keys) {
        const bool before = left.contains(key);
        const bool after = right.contains(key);
        const PropertyChange from = left.value(key);
        const PropertyChange to = right.value(key);
        const PropertyChange &row = before ? from : to;
        if (before && after && from.after == to.after) continue;
        result.append(QVariantMap{
            {QStringLiteral("entity"), row.target.toString()},
            {QStringLiteral("property"), row.propertyId},
            {QStringLiteral("from"), before ? from.after : QVariant{}},
            {QStringLiteral("to"), after ? to.after : QVariant{}},
            {QStringLiteral("change"), !before ? QStringLiteral("added")
                                       : !after ? QStringLiteral("removed")
                                                : QStringLiteral("changed")}});
    }
    return result;
}

QVariantList PropertyVersionHistory::diff(const QString &fromId, const QString &toId) const
{
    const Entry *from = find(fromId);
    const Entry *to = find(toId);
    return from && to ? diffSnapshots(from->snapshot, to->snapshot) : QVariantList{};
}

QVariantList PropertyVersionHistory::diffCurrent(const QString &fromId) const
{
    const Entry *from = find(fromId);
    if (!from || !m_client || !m_client->document()) return {};
    const auto current = ReferenceDocumentCodec::capture(*m_client->document());
    return current ? diffSnapshots(from->snapshot, current->payload) : QVariantList{};
}

bool PropertyVersionHistory::restore(const QString &id)
{
    const Entry *found = find(id);
    if (!found || !m_client) return fail(QStringLiteral("Unknown version or client"));
    const Entry target = *found;
    if (checkpoint(QStringLiteral("Before restore: ") + target.name.left(112)).isEmpty())
        return false;
    if (!m_client->restoreVersion(target.snapshot))
        return fail(m_client->lastError());
    m_lastError.clear();
    emit changed();
    return true;
}

} // namespace QindaTK
