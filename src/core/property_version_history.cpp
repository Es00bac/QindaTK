// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_version_history.h"
#include "reference_document_codec.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace QindaTK {
namespace {
constexpr int MaxCheckpoints = 64;
constexpr qsizetype MaxSnapshotBytes = 40 * 1024;
constexpr qsizetype MaxStoreBytes = 4 * 1024 * 1024;

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
        || m_storagePath.isEmpty())
        return fail(QStringLiteral("Configure a document client and checkpoint path"));
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
            || bytes.decoded.size() > MaxSnapshotBytes)
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
        || trimmed.size() > 128 || m_entries.size() >= MaxCheckpoints) {
        fail(QStringLiteral("Checkpoint configuration, name or limit is invalid"));
        return {};
    }
    const auto snapshot = ReferenceDocumentCodec::capture(*m_client->document());
    if (!snapshot || snapshot->payload.size() > MaxSnapshotBytes) {
        fail(QStringLiteral("Checkpoint snapshot is unsupported or too large"));
        return {};
    }
    Entry entry{QUuid::createUuid().toString(QUuid::WithoutBraces), trimmed,
                m_client->actorId(), m_client->document()->revisionString(),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                m_client->service()->operationCount(), snapshot->payload};
    auto updated = m_entries;
    updated.append(entry);
    if (!persist(updated)) return {};
    m_entries = std::move(updated);
    m_lastError.clear();
    emit changed();
    return entry.id;
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
                                  {QStringLiteral("operationCount"), it->operationCount}});
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
    const auto left = old->document->committedValues();
    const auto right = recent->document->committedValues();
    if (left.size() != right.size()) return {};
    QVariantList result;
    for (qsizetype i = 0; i < left.size(); ++i) {
        if (left.at(i).target != right.at(i).target
            || left.at(i).propertyId != right.at(i).propertyId) return {};
        if (left.at(i).after == right.at(i).after) continue;
        result.append(QVariantMap{{QStringLiteral("entity"), left.at(i).target.toString()},
                                  {QStringLiteral("property"), left.at(i).propertyId},
                                  {QStringLiteral("from"), left.at(i).after},
                                  {QStringLiteral("to"), right.at(i).after}});
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
