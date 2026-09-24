// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_property_sync.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace QindaTK {
namespace {
constexpr int MaxOperations = 4096;
constexpr int MaxClients = 8;
constexpr int MaxStoreBytes = 4 * 1024 * 1024;
constexpr int MaxValueBytes = 64 * 1024;

bool canonicalUuid(const QString &id)
{
    const QUuid uuid(id);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

QString keyOf(const PropertySyncOperation &op)
{
    return op.entityId + QLatin1Char('/') + op.propertyId;
}

bool outranks(const PropertySyncOperation &a, const PropertySyncOperation &b)
{
    if (a.logicalTime != b.logicalTime) return a.logicalTime > b.logicalTime;
    if (a.actorId != b.actorId) return a.actorId > b.actorId;
    return a.operationId > b.operationId;
}

std::optional<QJsonObject> encodeValue(const QVariant &value)
{
    QString kind;
    QJsonValue stored;
    switch (value.metaType().id()) {
    case QMetaType::Bool: kind = QStringLiteral("bool"); stored = value.toBool(); break;
    case QMetaType::Int: kind = QStringLiteral("int"); stored = value.toString(); break;
    case QMetaType::UInt: kind = QStringLiteral("uint"); stored = value.toString(); break;
    case QMetaType::LongLong: kind = QStringLiteral("int64"); stored = value.toString(); break;
    case QMetaType::ULongLong: kind = QStringLiteral("uint64"); stored = value.toString(); break;
    case QMetaType::Double:
        if (!std::isfinite(value.toDouble())) return std::nullopt;
        kind = QStringLiteral("double"); stored = QString::number(value.toDouble(), 'g', 17); break;
    case QMetaType::QString: kind = QStringLiteral("string"); stored = value.toString(); break;
    case QMetaType::QByteArray:
        kind = QStringLiteral("bytes"); stored = QString::fromLatin1(value.toByteArray().toBase64()); break;
    default: return std::nullopt;
    }
    QJsonObject result{{QStringLiteral("kind"), kind}, {QStringLiteral("data"), stored}};
    if (QJsonDocument(result).toJson(QJsonDocument::Compact).size() > MaxValueBytes)
        return std::nullopt;
    return result;
}

std::optional<QVariant> decodeValue(const QJsonObject &object)
{
    if (object.size() != 2 || !object.contains(QStringLiteral("kind"))
        || !object.contains(QStringLiteral("data"))
        || QJsonDocument(object).toJson(QJsonDocument::Compact).size() > MaxValueBytes)
        return std::nullopt;
    const QString kind = object.value(QStringLiteral("kind")).toString();
    const QJsonValue data = object.value(QStringLiteral("data"));
    if (kind == QStringLiteral("bool") && data.isBool()) return QVariant(data.toBool());
    if (!data.isString()) return std::nullopt;
    const QString text = data.toString();
    if (kind == QStringLiteral("string")) return QVariant(text);
    if (kind == QStringLiteral("bytes")) {
        const QByteArray encoded = text.toLatin1();
        const auto decoded = QByteArray::fromBase64Encoding(encoded);
        if (!decoded || decoded.decoded.toBase64() != encoded) return std::nullopt;
        return QVariant(decoded.decoded);
    }
    bool ok = false;
    if (kind == QStringLiteral("int")) {
        const int n = text.toInt(&ok); if (ok && QString::number(n) == text) return QVariant(n);
    } else if (kind == QStringLiteral("uint")) {
        const uint n = text.toUInt(&ok); if (ok && QString::number(n) == text) return QVariant(n);
    } else if (kind == QStringLiteral("int64")) {
        const qlonglong n = text.toLongLong(&ok); if (ok && QString::number(n) == text) return QVariant(n);
    } else if (kind == QStringLiteral("uint64")) {
        const qulonglong n = text.toULongLong(&ok); if (ok && QString::number(n) == text) return QVariant(n);
    } else if (kind == QStringLiteral("double")) {
        const double n = text.toDouble(&ok);
        if (ok && std::isfinite(n) && QString::number(n, 'g', 17) == text) return QVariant(n);
    }
    return std::nullopt;
}

std::optional<QJsonObject> encode(const PropertySyncOperation &op)
{
    const auto value = encodeValue(op.value);
    if (!value) return std::nullopt;
    return QJsonObject{{QStringLiteral("document"), op.documentId},
                       {QStringLiteral("id"), op.operationId},
                       {QStringLiteral("actor"), op.actorId},
                       {QStringLiteral("time"), QString::number(op.logicalTime)},
                       {QStringLiteral("entity"), op.entityId},
                       {QStringLiteral("property"), op.propertyId},
                       {QStringLiteral("value"), *value}};
}

std::optional<PropertySyncOperation> decode(const QJsonObject &object)
{
    if (object.size() != 7 || !object.value(QStringLiteral("value")).isObject())
        return std::nullopt;
    PropertySyncOperation op;
    op.documentId = object.value(QStringLiteral("document")).toString();
    op.operationId = object.value(QStringLiteral("id")).toString();
    op.actorId = object.value(QStringLiteral("actor")).toString();
    op.entityId = object.value(QStringLiteral("entity")).toString();
    op.propertyId = object.value(QStringLiteral("property")).toString();
    const QString time = object.value(QStringLiteral("time")).toString();
    bool ok = false;
    op.logicalTime = time.toULongLong(&ok);
    const auto value = decodeValue(object.value(QStringLiteral("value")).toObject());
    if (!canonicalUuid(op.documentId) || !canonicalUuid(op.operationId)
        || !canonicalUuid(op.actorId) || !canonicalUuid(op.entityId)
        || !ok || !op.logicalTime || QString::number(op.logicalTime) != time
        || op.propertyId.isEmpty() || op.propertyId.size() > 256 || !value)
        return std::nullopt;
    op.value = *value;
    return op;
}

bool sameOperation(const PropertySyncOperation &a, const PropertySyncOperation &b)
{
    return a.documentId == b.documentId && a.operationId == b.operationId
        && a.actorId == b.actorId && a.logicalTime == b.logicalTime
        && a.entityId == b.entityId && a.propertyId == b.propertyId
        && a.value.metaType() == b.value.metaType() && a.value == b.value;
}
}

std::optional<QJsonObject> propertySyncOperationToJson(const PropertySyncOperation &operation)
{
    return encode(operation);
}

std::optional<PropertySyncOperation> propertySyncOperationFromJson(const QJsonObject &object)
{
    return decode(object);
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
        const auto op = decode(row.toObject());
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
        const auto encoded = encode(op);
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
    if (!m_opened || operation.documentId != m_documentId || !encode(operation)
        || !decode(*encode(operation)))
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

bool LocalPropertySyncClient::fail(const QString &message)
{
    m_lastError = message;
    emit changed();
    return false;
}

bool LocalPropertySyncClient::connectToService()
{
    if (m_connected) return true;
    if (!m_document || !m_service || !canonicalUuid(m_actorId)
        || m_document->documentIdString() != m_service->documentId())
        return fail(QStringLiteral("Client configuration is incomplete"));
    if (m_document->revision().value != m_expectedRevision
        || m_document->resetGeneration() != m_resetGeneration)
        return fail(QStringLiteral("Document changed outside synchronization"));
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
    if (!m_document || !m_service || !canonicalUuid(m_actorId)
        || m_document->documentIdString() != m_service->documentId()
        || !canonicalUuid(entityId) || m_clock == std::numeric_limits<quint64>::max())
        return fail(QStringLiteral("Client, entity or clock is invalid"));
    if (m_pending.size() >= MaxOperations)
        return fail(QStringLiteral("Offline operation queue is full"));
    if (m_document->revision().value != m_expectedRevision
        || m_document->resetGeneration() != m_resetGeneration)
        return fail(QStringLiteral("Document changed outside synchronization"));
    if (!encodeValue(value)) return fail(QStringLiteral("Unsupported or oversized value"));
    if (auto error = m_document->validate(EntityId::parse(entityId), propertyId, value))
        return fail(error.message);
    PropertySyncOperation operation{m_document->documentIdString(),
        QUuid::createUuid().toString(QUuid::WithoutBraces), m_actorId,
        ++m_clock, entityId, propertyId, value};
    if (!receive(operation)) return false;
    m_pending.append(operation);
    emit changed();
    return !m_connected || flush();
}

bool LocalPropertySyncClient::receive(const PropertySyncOperation &operation)
{
    if (!m_document || operation.documentId != m_document->documentIdString()
        || !encode(operation) || !decode(*encode(operation)))
        return fail(QStringLiteral("Invalid incoming operation"));
    const auto previous = m_seen.constFind(operation.operationId);
    if (previous != m_seen.cend())
        return sameOperation(previous.value(), operation)
            ? true : fail(QStringLiteral("Duplicate ID has different content"));
    if (m_seen.size() >= MaxOperations * 2)
        return fail(QStringLiteral("Client operation limit reached"));
    if (m_document->revision().value != m_expectedRevision
        || m_document->resetGeneration() != m_resetGeneration)
        return fail(QStringLiteral("Document changed outside synchronization"));
    const QString key = keyOf(operation);
    const auto winner = m_winners.constFind(key);
    if (winner == m_winners.cend() || outranks(operation, winner.value())) {
        if (auto error = m_document->applySynchronizedValue(
                EntityId::parse(operation.entityId), operation.propertyId, operation.value))
            return fail(error.message);
        m_expectedRevision = m_document->revision().value;
        if (winner != m_winners.cend() && winner->actorId == m_actorId
            && operation.actorId != m_actorId
            && winner->value != operation.value)
            emit operationLost(winner->operationId, operation.operationId);
        m_winners.insert(key, operation);
    }
    m_seen.insert(operation.operationId, operation);
    m_clock = std::max(m_clock, operation.logicalTime);
    emit changed();
    return true;
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
