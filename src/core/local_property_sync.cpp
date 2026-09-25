// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_property_sync.h"
#include "property_sync_detail_p.h"
#include <QFile>
#include <QUuid>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace QindaTK {
using namespace sync_detail;
namespace {
constexpr int MaxClients = 8;
constexpr int MaxStoreBytes = 4 * 1024 * 1024;
constexpr int MaxHistory = 256;
}

void LocalPropertySyncService::setDocumentId(const QString &id)
{
    if (m_opened || m_documentId == id) return;
    m_documentId = id;
    emit changed();
}

void LocalPropertySyncService::setStoragePath(const QString &path)
{
    if (m_opened || m_storagePath == path) return;
    m_storagePath = path;
    emit changed();
}

bool LocalPropertySyncService::fail(const QString &message)
{
    m_lastError = message;
    emit changed();
    return false;
}

bool LocalPropertySyncService::open()
{
    if (m_opened) return true;
    if (!canonicalUuid(m_documentId) || !m_clients.isEmpty())
        return fail(QStringLiteral("Configure a document ID before opening the service"));
    if (m_storagePath.isEmpty()) { m_opened = true; return true; }
    QFile file(m_storagePath);
    if (!file.exists()) { m_operations.clear(); m_opened = true; emit changed(); return true; }
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxStoreBytes)
        return fail(QStringLiteral("Cannot read bounded operation store"));
    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return fail(QStringLiteral("Invalid operation store"));
    const auto root = json.object();
    const auto rows = root.value(QStringLiteral("operations")).toArray();
    if (root.size() != 3 || !root.value(QStringLiteral("operations")).isArray()
        || root.value(QStringLiteral("version")).toInt() != 1
        || root.value(QStringLiteral("document")).toString() != m_documentId
        || rows.size() > MaxOperations)
        return fail(QStringLiteral("Operation store identity or version mismatch"));
    QVector<PropertySyncOperation> loaded;
    QHash<QString, PropertySyncOperation> seen;
    for (const auto &row : rows) {
        if (!row.isObject()) return fail(QStringLiteral("Invalid operation row"));
        const auto op = propertySyncOperationFromJson(row.toObject());
        if (!op || op->documentId != m_documentId || seen.contains(op->operationId))
            return fail(QStringLiteral("Invalid or duplicate stored operation"));
        seen.insert(op->operationId, *op);
        loaded.append(*op);
    }
    m_operations = std::move(loaded);
    m_lastError.clear();
    m_opened = true;
    emit changed();
    return true;
}

bool LocalPropertySyncService::persist(const QVector<PropertySyncOperation> &operations)
{
    if (m_storagePath.isEmpty()) return true;
    QJsonArray rows;
    for (const auto &op : operations) {
        const auto encoded = propertySyncOperationToJson(op);
        if (!encoded) return fail(QStringLiteral("Unsupported operation value"));
        rows.append(*encoded);
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1}, {QStringLiteral("document"), m_documentId},
        {QStringLiteral("operations"), rows}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxStoreBytes) return fail(QStringLiteral("Operation store is full"));
    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(QStringLiteral("Cannot commit operation store"));
    return true;
}

bool LocalPropertySyncService::attach(LocalPropertySyncClient *client)
{
    if (!m_opened || !client || !canonicalUuid(m_documentId) || !client->document()
        || client->document()->documentIdString() != m_documentId
        || m_clients.size() >= MaxClients)
        return fail(QStringLiteral("Client or document identity is invalid"));
    for (const auto &existing : m_clients)
        if (existing && existing->actorId() == client->actorId())
            return fail(QStringLiteral("Actor is already connected"));
    m_clients.append(client);
    for (const auto &existing : m_clients)
        if (existing && existing != client && !existing->m_presence.isEmpty())
            client->receivePresence(existing->actorId(), existing->m_presence);
    return true;
}

void LocalPropertySyncService::detach(LocalPropertySyncClient *client)
{
    m_clients.removeAll(client);
    for (const auto &other : m_clients)
        if (other) other->receivePresenceLeft(client->actorId());
}

bool LocalPropertySyncService::submit(const PropertySyncOperation &operation)
{
    if (!m_opened || operation.documentId != m_documentId || !validOperation(operation))
        return fail(QStringLiteral("Invalid operation"));
    for (const auto &existing : m_operations) {
        if (existing.operationId != operation.operationId) continue;
        if (!sameOperation(existing, operation))
            return fail(QStringLiteral("Operation ID reused with different content"));
        return true;
    }
    if (m_operations.size() >= MaxOperations) return fail(QStringLiteral("Operation limit reached"));
    auto updated = m_operations;
    updated.append(operation);
    if (!persist(updated)) return false;
    m_operations = std::move(updated);
    emit changed();
    QVector<QPointer<LocalPropertySyncClient>> failed;
    const auto recipients = m_clients;
    for (const auto &client : recipients)
        if (client && !client->receive(operation)) failed.append(client);
    for (const auto &client : failed)
        if (client) client->disconnectFromService();
    return true;
}

void LocalPropertySyncService::publishPresence(LocalPropertySyncClient *client,
                                               const QVariantMap &presence)
{
    if (!m_clients.contains(client)) return;
    for (const auto &other : m_clients)
        if (other && other != client) {
            if (presence.isEmpty()) other->receivePresenceLeft(client->actorId());
            else other->receivePresence(client->actorId(), presence);
        }
}

LocalPropertySyncClient::~LocalPropertySyncClient()
{
    disconnectFromService();
}

void LocalPropertySyncClient::setDocument(PropertyDocument *document)
{
    if (m_connected || !m_pending.isEmpty() || !m_seen.isEmpty()) return;
    m_document = document;
    m_expectedRevision = document ? document->revision().value : 0;
    m_resetGeneration = document ? document->resetGeneration() : 0;
    m_baselineEntities.clear();
    m_baselineValues.clear();
    m_support.clear();
    m_removed.clear();
    m_text.clear();
    if (document) {
        for (const auto &change : document->committedValues()) {
            const QString entity = change.target.toString();
            m_baselineEntities.insert(entity);
            m_baselineValues.insert(keyOf(entity, change.propertyId), change.after);
        }
    }
    emit changed();
}

void LocalPropertySyncClient::setService(LocalPropertySyncService *service)
{
    if (m_connected) disconnectFromService();
    m_service = service;
    emit changed();
}

void LocalPropertySyncClient::setActorId(const QString &id)
{
    if (m_connected || !m_pending.isEmpty() || !m_seen.isEmpty()) return;
    m_actorId = id;
    emit changed();
}

void LocalPropertySyncClient::setTextProperties(const QStringList &properties)
{
    if (m_connected || !m_pending.isEmpty() || !m_seen.isEmpty()
        || m_textProperties == properties) return;
    m_textProperties = properties;
    m_text.clear();
    emit changed();
}

void LocalPropertySyncClient::setReadOnly(bool readOnly)
{
    if (m_readOnly == readOnly) return;
    m_readOnly = readOnly;
    emit changed();
}

bool LocalPropertySyncClient::fail(const QString &message)
{
    m_lastError = message;
    emit changed();
    return false;
}

bool LocalPropertySyncClient::inSync() const
{
    return m_document && m_document->revision().value == m_expectedRevision
        && m_document->resetGeneration() == m_resetGeneration;
}

bool LocalPropertySyncClient::readyToEdit()
{
    if (!m_document || !m_service || !canonicalUuid(m_actorId)
        || m_document->documentIdString() != m_service->documentId()
        || m_clock >= std::numeric_limits<quint64>::max() - 2)
        return fail(QStringLiteral("Client, entity or clock is invalid"));
    if (m_readOnly) return fail(QStringLiteral("This replica is read only"));
    if (m_pending.size() >= MaxOperations)
        return fail(QStringLiteral("Offline operation queue is full"));
    if (!inSync()) return fail(QStringLiteral("Document changed outside synchronization"));
    return true;
}

PropertySyncOperation LocalPropertySyncClient::makeOperation(
    Kind kind, const QString &entityId, const QString &propertyId, const QVariant &value,
    const QString &meta)
{
    return {m_document->documentIdString(), QUuid::createUuid().toString(QUuid::WithoutBraces),
            m_actorId, ++m_clock, entityId, propertyId, value, kind, meta};
}

bool LocalPropertySyncClient::submitLocal(const PropertySyncOperation &operation,
                                          const std::optional<HistoryEntry> &entry)
{
    if (!receive(operation)) return false;
    if (entry) record(*entry);
    m_pending.append(operation);
    emit changed();
    return !m_connected || flush();
}

void LocalPropertySyncClient::record(HistoryEntry entry)
{
    auto &stack = m_recording == Record::Undo ? m_redo : m_undo;
    if (m_recording == Record::Fresh) m_redo.clear();
    stack.append(std::move(entry));
    if (stack.size() > MaxHistory) stack.removeFirst();
}

bool LocalPropertySyncClient::connectToService()
{
    if (m_connected) return true;
    if (!m_document || !m_service || !canonicalUuid(m_actorId)
        || m_document->documentIdString() != m_service->documentId())
        return fail(QStringLiteral("Client configuration is incomplete"));
    if (!inSync()) return fail(QStringLiteral("Document changed outside synchronization"));
    if (!m_service->attach(this)) return fail(m_service->lastError());
    m_connected = true;
    emit changed();
    // AGENT-GUARD: replay before flushing queued offline operations. Their
    // identities and clocks remain fixed, so reconnect order cannot change a
    // key's final winner.
    for (const auto &operation : m_service->operations()) {
        if (!receive(operation)) { disconnectFromService(); return false; }
    }
    if (!flush()) return false;
    return true;
}

void LocalPropertySyncClient::disconnectFromService()
{
    if (!m_connected) return;
    m_connected = false;
    if (m_service) m_service->detach(this);
    const bool hadPresence = !m_presence.isEmpty();
    m_presence.clear();
    if (hadPresence) emit ownPresenceChanged({});
    emit changed();
}

bool LocalPropertySyncClient::setValue(const QString &entityId,
                                       const QString &propertyId, const QVariant &value)
{
    return setValueImpl(entityId, propertyId, value, true);
}

bool LocalPropertySyncClient::setValueImpl(const QString &entityId, const QString &propertyId,
                                           const QVariant &value, bool recordHistory)
{
    if (!readyToEdit()) return false;
    if (!canonicalUuid(entityId)) return fail(QStringLiteral("Entity ID is invalid"));
    if (isTextProperty(propertyId))
        return fail(QStringLiteral("Text properties change through replaceText"));
    if (!encodableValue(value)) return fail(QStringLiteral("Unsupported or oversized value"));
    const EntityId entity = EntityId::parse(entityId);
    if (auto error = m_document->validate(entity, propertyId, value))
        return fail(error.message);
    const QVariant previous = m_document->committedValue(entity, propertyId);
    const auto operation = makeOperation(Kind::Set, entityId, propertyId, value);
    std::optional<HistoryEntry> entry;
    if (recordHistory)
        entry = step(Kind::Set, operation.operationId, entityId, propertyId, previous);
    return submitLocal(operation, entry);
}

bool LocalPropertySyncClient::receive(const PropertySyncOperation &operation)
{
    if (!m_document || operation.documentId != m_document->documentIdString()
        || !validOperation(operation))
        return fail(QStringLiteral("Invalid incoming operation"));
    const auto previous = m_seen.constFind(operation.operationId);
    if (previous != m_seen.cend())
        return sameOperation(previous.value(), operation)
            ? true : fail(QStringLiteral("Duplicate ID has different content"));
    if (m_seen.size() >= MaxOperations * 2)
        return fail(QStringLiteral("Client operation limit reached"));
    if (!inSync()) return fail(QStringLiteral("Document changed outside synchronization"));
    bool accepted = false;
    switch (operation.kind) {
    case Kind::Set: accepted = receiveSet(operation); break;
    case Kind::Restore: accepted = receiveRestore(operation); break;
    case Kind::Create:
    case Kind::Delete: accepted = receiveEntity(operation); break;
    case Kind::TextInsert:
    case Kind::TextDelete: accepted = receiveText(operation); break;
    case Kind::Checkpoint:
        accepted = m_checkpoints.size() < 256
            || fail(QStringLiteral("Shared checkpoint limit reached"));
        if (accepted) m_checkpoints.append(operation);
        break;
    }
    if (!accepted) return false;
    m_seen.insert(operation.operationId, operation);
    m_clock = std::max(m_clock, operation.logicalTime);
    if (operation.kind == Kind::Checkpoint) emit checkpointShared(operation.operationId);
    emit changed();
    return true;
}

bool LocalPropertySyncClient::receiveSet(const PropertySyncOperation &operation)
{
    if (isTextProperty(operation.propertyId))
        return fail(QStringLiteral("A text property received a value operation"));
    const auto descriptor = m_document->descriptor(operation.propertyId);
    if (!descriptor || descriptor->type != operation.value.metaType())
        return fail(QStringLiteral("Operation value does not match the property schema"));
    const QString key = keyOf(operation.entityId, operation.propertyId);
    const auto found = m_winners.constFind(key);
    const std::optional<PropertySyncOperation> winner = found != m_winners.cend()
        ? std::optional<PropertySyncOperation>(found.value()) : std::nullopt;
    if (winner && !outranks(operation, *winner)) return true;
    const EntityId entity = EntityId::parse(operation.entityId);
    if (m_document->contains(entity)) {
        if (auto error = m_document->applySynchronizedValue(
                entity, operation.propertyId, operation.value))
            return fail(error.message);
        m_expectedRevision = m_document->revision().value;
    }
    m_winners.insert(key, operation);
    if (winner && winner->actorId == m_actorId && operation.actorId != m_actorId
        && winner->value != operation.value)
        emit operationLost(winner->operationId, operation.operationId);
    // A winning edit to a deleted entity revives it (observed-remove, D-166).
    return reconcileEntity(operation.entityId);
}

bool LocalPropertySyncClient::flush()
{
    while (m_connected && !m_pending.isEmpty()) {
        if (!m_service || !m_service->submit(m_pending.front()))
            return fail(m_service ? m_service->lastError() : QStringLiteral("Service disappeared"));
        m_pending.removeFirst();
        emit changed();
    }
    return true;
}

void LocalPropertySyncClient::setPresence(const QVariantMap &presence)
{
    if (!m_connected || !m_service) return;
    const QJsonDocument json = QJsonDocument::fromVariant(presence);
    if (!json.isObject() || json.toJson(QJsonDocument::Compact).size() > 4096)
        return;
    m_presence = presence;
    m_service->publishPresence(this, presence);
    emit ownPresenceChanged(presence);
}

void LocalPropertySyncClient::receivePresence(const QString &actorId,
                                              const QVariantMap &presence)
{
    emit presenceChanged(actorId, presence);
}

void LocalPropertySyncClient::receivePresenceLeft(const QString &actorId)
{
    emit presenceLeft(actorId);
}

} // namespace QindaTK
