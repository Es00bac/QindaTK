// SPDX-License-Identifier: LGPL-3.0-or-later
// LocalPropertySyncService: operation routing, the durable store, log
// compaction and the replica outbox. See docs/collaboration.md "Compaction"
// (D-204) and "Permissions" (D-205).
#include "local_property_sync.h"
#include "property_sync_detail_p.h"
#include "reference_document_codec.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace QindaTK {
using namespace sync_detail;
namespace {
constexpr int MaxClients = 8;
constexpr qsizetype MaxStoreBytes = 8 * 1024 * 1024;
constexpr int MinCompactAt = 16;

QString stateFormat() { return QStringLiteral("qindatk.sync-state"); }
}

LocalPropertySyncService::LocalPropertySyncService(QObject *parent) : QObject(parent) {}

LocalPropertySyncService::~LocalPropertySyncService()
{
    // Acknowledgements are persisted lazily; keep them if the process ends.
    if (m_persistQueued) persist(m_operations, m_outbox);
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

void LocalPropertySyncService::setAuthoritative(bool authoritative)
{
    if (m_authoritative == authoritative) return;
    m_authoritative = authoritative;
    if (authoritative && !m_outbox.isEmpty()) {
        m_outbox.clear();
        schedulePersist();
    }
    emit changed();
}

void LocalPropertySyncService::setCompactAt(int operations)
{
    const int clamped = std::clamp(operations, MinCompactAt, MaxOperations);
    if (m_compactAt == clamped) return;
    m_compactAt = clamped;
    emit changed();
}

bool LocalPropertySyncService::fail(const QString &message, bool permanent)
{
    m_lastError = message;
    m_lastErrorPermanent = permanent;
    emit changed();
    return false;
}

QString LocalPropertySyncService::basePath(const QString &baseId) const
{
    return m_storagePath + QStringLiteral(".base.") + baseId;
}

QVector<PropertySyncOperation> LocalPropertySyncService::outbox() const
{
    QVector<PropertySyncOperation> result;
    for (const auto &operation : m_operations)
        if (m_outbox.contains(operation.operationId)) result.append(operation);
    return result;
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
    const int version = root.value(QStringLiteral("version")).toInt();
    // Version 1 (before compaction) has no base or outbox.
    if (root.size() != (version == 2 ? 5 : 3) || (version != 1 && version != 2)
        || !root.value(QStringLiteral("operations")).isArray()
        || root.value(QStringLiteral("document")).toString() != m_documentId
        || rows.size() > MaxOperations)
        return fail(QStringLiteral("Operation store identity or version mismatch"));
    QVector<PropertySyncOperation> loaded;
    QSet<QString> seen;
    for (const auto &row : rows) {
        if (!row.isObject()) return fail(QStringLiteral("Invalid operation row"));
        const auto op = propertySyncOperationFromJson(row.toObject());
        if (!op || op->documentId != m_documentId || seen.contains(op->operationId))
            return fail(QStringLiteral("Invalid or duplicate stored operation"));
        seen.insert(op->operationId);
        loaded.append(*op);
    }
    QStringList outbox;
    if (version == 2) {
        const QJsonValue rowsOut = root.value(QStringLiteral("outbox"));
        const QString baseId = root.value(QStringLiteral("base")).toString();
        if (!rowsOut.isArray() || (!baseId.isEmpty() && !canonicalUuid(baseId)))
            return fail(QStringLiteral("Invalid operation store outbox or base"));
        for (const auto &id : rowsOut.toArray()) {
            if (!seen.contains(id.toString()) || outbox.contains(id.toString()))
                return fail(QStringLiteral("Outbox names an unknown operation"));
            outbox.append(id.toString());
        }
        if (!baseId.isEmpty() && !loadBase(baseId)) return false;
    } else if (!m_authoritative) {
        for (const auto &op : std::as_const(loaded)) outbox.append(op.operationId);
    }
    m_operations = std::move(loaded);
    m_outbox = m_authoritative ? QStringList() : outbox;
    m_lastError.clear();
    m_opened = true;
    emit changed();
    return true;
}

bool LocalPropertySyncService::loadBase(const QString &baseId)
{
    QFile file(basePath(baseId));
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxStateBytes)
        return fail(QStringLiteral("Cannot read the compacted state"));
    const QByteArray bytes = file.readAll();
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    if (root.value(QStringLiteral("format")).toString() != stateFormat()
        || root.value(QStringLiteral("document")).toString() != m_documentId
        || root.value(QStringLiteral("id")).toString() != baseId)
        return fail(QStringLiteral("Compacted state identity mismatch"));
    m_base = bytes;
    m_baseId = baseId;
    return true;
}

bool LocalPropertySyncService::writeBase(const QByteArray &base, const QString &baseId)
{
    if (m_storagePath.isEmpty()) return true;
    QSaveFile file(basePath(baseId));
    if (!file.open(QIODevice::WriteOnly) || file.write(base) != base.size() || !file.commit())
        return fail(QStringLiteral("Cannot commit the compacted state"));
    return true;
}

bool LocalPropertySyncService::persist(const QVector<PropertySyncOperation> &operations,
                                       const QStringList &outbox)
{
    return persistAs(operations, outbox, m_baseId);
}

bool LocalPropertySyncService::persistAs(const QVector<PropertySyncOperation> &operations,
                                         const QStringList &outbox, const QString &baseId)
{
    if (m_storagePath.isEmpty()) return true;
    QJsonArray rows;
    for (const auto &op : operations) {
        const auto encoded = propertySyncOperationToJson(op);
        if (!encoded) return fail(QStringLiteral("Unsupported operation value"), true);
        rows.append(*encoded);
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 2}, {QStringLiteral("document"), m_documentId},
        {QStringLiteral("base"), baseId}, {QStringLiteral("operations"), rows},
        {QStringLiteral("outbox"), QJsonArray::fromStringList(outbox)}})
        .toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxStoreBytes) return fail(QStringLiteral("Operation store is full"));
    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(QStringLiteral("Cannot commit operation store"));
    return true;
}

void LocalPropertySyncService::schedulePersist()
{
    if (m_persistQueued || m_storagePath.isEmpty() || !m_opened) return;
    m_persistQueued = true;
    QTimer::singleShot(0, this, [this] {
        if (!m_persistQueued) return;
        m_persistQueued = false;
        persist(m_operations, m_outbox);
    });
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
    for (auto it = m_relayedPresence.cbegin(); it != m_relayedPresence.cend(); ++it)
        if (it.key() != client->actorId()) client->receivePresence(it.key(), it.value());
    return true;
}

void LocalPropertySyncService::detach(LocalPropertySyncClient *client)
{
    m_clients.removeAll(client);
    for (const auto &other : m_clients)
        if (other) other->receivePresenceLeft(client->actorId());
    if (!client->m_presence.isEmpty()) emit presencePublished(client->actorId(), {});
}

bool LocalPropertySyncService::submit(const PropertySyncOperation &operation)
{
    return append(operation, true);
}

bool LocalPropertySyncService::submitUpstream(const PropertySyncOperation &operation)
{
    return append(operation, false);
}

bool LocalPropertySyncService::append(const PropertySyncOperation &operation, bool local)
{
    if (!m_opened || operation.documentId != m_documentId || !validOperation(operation))
        return fail(QStringLiteral("Invalid operation"), true);
    for (const auto &existing : std::as_const(m_operations)) {
        if (existing.operationId != operation.operationId) continue;
        if (!sameOperation(existing, operation))
            return fail(QStringLiteral("Operation ID reused with different content"), true);
        if (!local) settle(operation.operationId);
        return true;
    }
    if (!canAccept(1)) return false;
    auto updated = m_operations;
    updated.append(operation);
    QStringList outbox = m_outbox;
    if (local && !m_authoritative) outbox.append(operation.operationId);
    if (!persist(updated, outbox)) return false;
    m_operations = std::move(updated);
    m_outbox = std::move(outbox);
    emit changed();
    QVector<QPointer<LocalPropertySyncClient>> failed;
    const auto recipients = m_clients;
    for (const auto &client : recipients)
        if (client && !client->receive(operation)) failed.append(client);
    for (const auto &client : failed)
        if (client) client->disconnectFromService();
    // Best effort: a replica still flushing a batch cannot fold yet, so a
    // refused compaction is retried from the event loop.
    if (m_authoritative && m_operations.size() >= m_compactAt && !compact())
        QTimer::singleShot(0, this, [this] {
            if (m_operations.size() >= m_compactAt) compact();
        });
    return true;
}

void LocalPropertySyncService::settle(const QString &operationId)
{
    if (!m_outbox.removeOne(operationId)) return;
    schedulePersist();
    emit changed();
}

bool LocalPropertySyncService::canAccept(int count)
{
    if (m_operations.size() + count <= MaxOperations) return true;
    if (m_authoritative && compact() && m_operations.size() + count <= MaxOperations)
        return true;
    return fail(m_authoritative
                    ? QStringLiteral("Operation limit reached")
                    : QStringLiteral("Operation limit reached; reconnect to the hub to compact"));
}

void LocalPropertySyncService::setCompactionBaseline(const QByteArray &snapshot,
                                                     const QStringList &textProperties)
{
    m_compactionBaseline = snapshot;
    m_compactionText = textProperties;
}

bool LocalPropertySyncService::compact()
{
    if (!m_opened || !m_authoritative || m_compacting)
        return fail(QStringLiteral("Only an open authoritative service compacts"));
    QSet<QString> logged;
    for (const auto &op : std::as_const(m_operations)) logged.insert(op.operationId);
    // AGENT-GUARD: the folding replica must hold exactly base + log. A client
    // with a pending operation the log lacks, or one built on an older base,
    // would fold state no other replica can reach.
    LocalPropertySyncClient *source = nullptr;
    for (const auto &client : std::as_const(m_clients)) {
        if (!client || !client->m_connected || !client->inSync()
            || client->m_syncedBaseId != m_baseId)
            continue;
        bool folded = true;
        for (const auto &pending : std::as_const(client->m_pending))
            folded = folded && logged.contains(pending.operationId);
        if (folded) { source = client; break; }
    }
    // No suitable replica here: rebuild base + log in a scratch replica.
    std::unique_ptr<PropertyDocument> scratchDocument;
    LocalPropertySyncClient scratch;
    if (!source && !m_compactionBaseline.isEmpty()) {
        auto decoded = ReferenceDocumentCodec::decode(m_compactionBaseline);
        if (decoded && decoded->document->documentIdString() == m_documentId) {
            scratchDocument = std::move(decoded->document);
            scratch.setDocument(scratchDocument.get());
            scratch.setTextProperties(m_compactionText);
            scratch.m_service = this;
            if (scratch.rebuild()) source = &scratch;
        }
    }
    if (!source)
        return fail(QStringLiteral("Compaction needs an attached replica that is in sync"));
    const QString baseId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QByteArray state = source->exportState(baseId);
    if (state.isEmpty() || state.size() > MaxStateBytes)
        return fail(QStringLiteral("Compacted state is too large"));
    const QVector<PropertySyncOperation> tail = source->m_checkpoints;
    m_compacting = true;
    const bool stored = writeBase(state, baseId) && persistAs(tail, {}, baseId);
    m_compacting = false;
    if (!stored) return false;
    if (!m_baseId.isEmpty() && !m_storagePath.isEmpty()) QFile::remove(basePath(m_baseId));
    m_base = state;
    m_baseId = baseId;
    m_operations = tail;
    m_outbox.clear();
    for (const auto &client : std::as_const(m_clients))
        if (client) client->compacted(baseId);
    emit compacted();
    emit changed();
    return true;
}

bool LocalPropertySyncService::adoptBase(const QByteArray &base, const QString &baseId,
                                         const QVector<PropertySyncOperation> &tail)
{
    if (!m_opened || m_authoritative)
        return fail(QStringLiteral("Only an open replica service adopts a hub base"), true);
    if (baseId == m_baseId) {
        for (const auto &operation : tail)
            if (!submitUpstream(operation)) return false;
        return true;
    }
    const QJsonObject root = QJsonDocument::fromJson(base).object();
    if (!canonicalUuid(baseId) || base.size() > MaxStateBytes
        || root.value(QStringLiteral("format")).toString() != stateFormat()
        || root.value(QStringLiteral("document")).toString() != m_documentId
        || root.value(QStringLiteral("id")).toString() != baseId)
        return fail(QStringLiteral("Invalid hub base"), true);
    // Unsettled local operations survive; everything else the hub holds.
    QVector<PropertySyncOperation> operations = outbox();
    QStringList unsettled = m_outbox;
    for (const auto &operation : tail) {
        if (operation.documentId != m_documentId || !validOperation(operation))
            return fail(QStringLiteral("Invalid hub operation"), true);
        const auto found = std::find_if(operations.cbegin(), operations.cend(),
            [&](const PropertySyncOperation &op) { return op.operationId == operation.operationId; });
        if (found == operations.cend()) operations.append(operation);
        else if (!sameOperation(*found, operation))
            return fail(QStringLiteral("Operation ID reused with different content"), true);
        unsettled.removeOne(operation.operationId);
    }
    if (operations.size() > MaxOperations)
        return fail(QStringLiteral("Operation limit reached"));
    if (!writeBase(base, baseId) || !persistAs(operations, unsettled, baseId)) return false;
    if (!m_baseId.isEmpty() && !m_storagePath.isEmpty()) QFile::remove(basePath(m_baseId));
    m_base = base;
    m_baseId = baseId;
    m_operations = std::move(operations);
    m_outbox = std::move(unsettled);
    emit compacted();
    emit changed();
    rebuildClients();
    return true;
}

bool LocalPropertySyncService::discard(const QString &operationId)
{
    if (!m_outbox.contains(operationId))
        return fail(QStringLiteral("Only an unsettled local operation can be discarded"), true);
    auto operations = m_operations;
    operations.erase(std::remove_if(operations.begin(), operations.end(),
                                    [&](const PropertySyncOperation &op) {
                                        return op.operationId == operationId;
                                    }),
                     operations.end());
    QStringList outbox = m_outbox;
    outbox.removeOne(operationId);
    if (!persist(operations, outbox)) return false;
    m_operations = std::move(operations);
    m_outbox = std::move(outbox);
    emit changed();
    rebuildClients();
    return true;
}

void LocalPropertySyncService::rebuildClients()
{
    const auto clients = m_clients;
    for (const auto &client : clients)
        if (client && !client->rebuild()) client->disconnectFromService();
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
    emit presencePublished(client->actorId(), presence);
}

void LocalPropertySyncService::relayPresence(const QString &actorId, const QVariantMap &presence)
{
    if (!canonicalUuid(actorId)) return;
    if (presence.isEmpty() && !m_relayedPresence.remove(actorId)) return;
    if (!presence.isEmpty()) m_relayedPresence.insert(actorId, presence);
    for (const auto &client : std::as_const(m_clients)) {
        if (!client || client->actorId() == actorId) continue;
        if (presence.isEmpty()) client->receivePresenceLeft(actorId);
        else client->receivePresence(actorId, presence);
    }
}

QHash<QString, QVariantMap> LocalPropertySyncService::attachedPresence() const
{
    QHash<QString, QVariantMap> result;
    for (const auto &client : m_clients)
        if (client && !client->m_presence.isEmpty())
            result.insert(client->actorId(), client->m_presence);
    return result;
}

} // namespace QindaTK
