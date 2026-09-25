// SPDX-License-Identifier: LGPL-3.0-or-later
// PropertySyncSocketServer: local-socket and local-network sync endpoint.
#include "property_sync_socket.h"
#include "property_sync_detail_p.h"
#include <QJsonDocument>
#include <algorithm>

namespace QindaTK {
using sync_detail::canonicalUuid;

namespace {
constexpr int MaxPeers = 8;
constexpr qsizetype BasePartBytes = 48 * 1024;

QJsonObject leaveFrame(const QString &actorId)
{
    return {{QStringLiteral("type"), QStringLiteral("presenceLeave")},
            {QStringLiteral("actor"), actorId}};
}

QJsonObject presenceFrame(const QString &actorId, const QVariantMap &presence)
{
    if (presence.isEmpty()) return leaveFrame(actorId);
    return {{QStringLiteral("type"), QStringLiteral("presence")},
            {QStringLiteral("actor"), actorId},
            {QStringLiteral("data"), QJsonObject::fromVariantMap(presence)}};
}
}

PropertySyncSocketServer::PropertySyncSocketServer(QObject *parent) : QObject(parent)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_clock.start();
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection()) addPeer(socket);
    });
    connect(&m_tcpServer, &QTcpServer::newConnection, this, [this] {
        while (auto *socket = m_tcpServer.nextPendingConnection()) addPeer(socket);
    });
    connect(&m_idleTimer, &QTimer::timeout, this, &PropertySyncSocketServer::expireIdle);
    setIdleTimeout(m_idleTimeout);
}

PropertySyncSocketServer::~PropertySyncSocketServer()
{
    // Sockets still closing gracefully are children of the listeners and die
    // after m_peers; their disconnect callbacks must not reach this object.
    for (auto *socket : m_server.findChildren<QLocalSocket *>())
        QObject::disconnect(socket, nullptr, this, nullptr);
    for (auto *socket : m_tcpServer.findChildren<QTcpSocket *>())
        QObject::disconnect(socket, nullptr, this, nullptr);
    const auto devices = m_peers.keys();
    for (auto *device : devices) {
        QObject::disconnect(device, nullptr, this, nullptr);
        closePeer(device, false);
    }
    m_peers.clear();
    m_server.close();
    m_tcpServer.close();
}

void PropertySyncSocketServer::setService(LocalPropertySyncService *service)
{
    if (isListening()) return;
    for (const auto &connection : std::as_const(m_serviceConnections))
        QObject::disconnect(connection);
    m_serviceConnections.clear();
    m_service = service;
    if (!service) return;
    // This service holds the document's shared log: it compacts (D-295).
    service->setAuthoritative(true);
    m_serviceConnections.append(connect(service, &LocalPropertySyncService::changed, this, [this] {
        const auto devices = m_peers.keys();
        for (auto *device : devices)
            if (m_peers.contains(device) && !m_peers[device].actorId.isEmpty())
                sendBacklog(device);
    }));
    // A compaction replaces the log: every live peer gets the new base, the
    // retained tail and a rebased marker, then continues from the tail.
    m_serviceConnections.append(connect(service, &LocalPropertySyncService::compacted, this, [this] {
        const auto devices = m_peers.keys();
        for (auto *device : devices) {
            if (!m_peers.contains(device) || m_peers[device].actorId.isEmpty()) continue;
            m_peers[device].sentCount = 0;
            if (!sendBase(device)) continue;
            sendBacklog(device);
            if (m_peers.contains(device))
                send(device, {{QStringLiteral("type"), QStringLiteral("rebased")}});
        }
    }));
    // Presence of replicas attached in this process reaches every peer.
    m_serviceConnections.append(connect(service, &LocalPropertySyncService::presencePublished,
        this, [this](const QString &actorId, const QVariantMap &presence) {
            broadcastPresence(nullptr, presenceFrame(actorId, presence));
        }));
}

void PropertySyncSocketServer::setSocketName(const QString &name)
{
    if (!m_server.isListening()) m_socketName = name;
}

void PropertySyncSocketServer::setIdleTimeout(int milliseconds)
{
    m_idleTimeout = std::max(0, milliseconds);
    m_idleTimer.stop();
    if (m_idleTimeout > 0) m_idleTimer.start(std::max(20, m_idleTimeout / 4));
}

int PropertySyncSocketServer::sessionCount() const
{
    int count = 0;
    for (const auto &peer : m_peers)
        if (!peer.actorId.isEmpty()) ++count;
    return count;
}

bool PropertySyncSocketServer::grant(const QString &actorId,
                                     const QString &credential, bool mayEdit)
{
    if (!canonicalUuid(actorId) || credential.size() < 16 || credential.size() > 256)
        return fail(QStringLiteral("Actor ID or credential is invalid"));
    const auto previous = m_grants.constFind(actorId);
    const bool rotated = previous != m_grants.cend() && previous->credential != credential;
    const bool permissionChanged = previous == m_grants.cend() || previous->mayEdit != mayEdit;
    m_grants.insert(actorId, {credential, mayEdit});
    if (rotated) {
        endSessions(actorId, QStringLiteral("Credential changed"));
    } else if (permissionChanged) {
        const auto devices = m_peers.keys();
        for (auto *device : devices)
            if (m_peers.value(device).actorId == actorId)
                send(device, {{QStringLiteral("type"), QStringLiteral("permission")},
                              {QStringLiteral("mayEdit"), mayEdit}});
    }
    return true;
}

bool PropertySyncSocketServer::revoke(const QString &actorId)
{
    if (!m_grants.remove(actorId)) return fail(QStringLiteral("Actor has no grant"));
    endSessions(actorId, QStringLiteral("Access revoked"));
    return true;
}

void PropertySyncSocketServer::endSessions(const QString &actorId, const QString &reason)
{
    const auto devices = m_peers.keys();
    for (auto *device : devices) {
        if (!m_peers.contains(device) || m_peers.value(device).actorId != actorId) continue;
        send(device, {{QStringLiteral("type"), QStringLiteral("revoked")},
                      {QStringLiteral("reason"), reason}});
        forgetPeer(device);
        closePeer(device, true);
    }
}

bool PropertySyncSocketServer::fail(const QString &message)
{
    m_lastError = message;
    emit errorOccurred(message);
    return false;
}

bool PropertySyncSocketServer::listen()
{
    if (m_server.isListening()) return true;
    if (!m_service || !canonicalUuid(m_service->documentId()) || m_socketName.isEmpty())
        return fail(QStringLiteral("Configure an open service and socket name"));
    if (!m_server.listen(m_socketName)) return fail(m_server.errorString());
    m_lastError.clear();
    return true;
}

bool PropertySyncSocketServer::listenNetwork(const QHostAddress &address, quint16 port)
{
    if (m_tcpServer.isListening()) return true;
    if (!m_service || !canonicalUuid(m_service->documentId()) || address.isNull())
        return fail(QStringLiteral("Configure an open service and network address"));
    if (!m_tcpServer.listen(address, port)) return fail(m_tcpServer.errorString());
    m_lastError.clear();
    return true;
}

void PropertySyncSocketServer::close()
{
    m_server.close();
    m_tcpServer.close();
    const auto devices = m_peers.keys();
    for (auto *device : devices) {
        forgetPeer(device);
        closePeer(device, true);
    }
}

void PropertySyncSocketServer::addPeer(QIODevice *device)
{
    if (m_peers.size() >= MaxPeers) { closePeer(device, false); return; }
    Peer peer;
    peer.nonce = PropertySyncChannel::createNonce();
    peer.lastSeen = m_clock.elapsed();
    m_peers.insert(device, peer);
    connect(device, &QIODevice::readyRead, this, [this, device] { readPeer(device); });
    const auto gone = [this, device] {
        forgetPeer(device);
        device->deleteLater();
    };
    if (auto *local = qobject_cast<QLocalSocket *>(device))
        connect(local, &QLocalSocket::disconnected, this, gone);
    else if (auto *tcp = qobject_cast<QAbstractSocket *>(device))
        connect(tcp, &QAbstractSocket::disconnected, this, gone);
    if (!writeSyncFrame(device, {{QStringLiteral("type"), QStringLiteral("challenge")},
                                 {QStringLiteral("version"), 2},
                                 {QStringLiteral("nonce"), QString::fromLatin1(peer.nonce)}})) {
        QObject::disconnect(device, nullptr, this, nullptr);
        forgetPeer(device);
        closePeer(device, false);
    }
}

void PropertySyncSocketServer::forgetPeer(QIODevice *device)
{
    const auto it = m_peers.constFind(device);
    if (it == m_peers.cend()) return;
    const QString actor = it->actorId;
    const bool present = !it->presence.isEmpty();
    m_peers.remove(device);
    if (actor.isEmpty() || !present) return;
    broadcastPresence(device, leaveFrame(actor));
    if (m_service) m_service->relayPresence(actor, {});
}

void PropertySyncSocketServer::closePeer(QIODevice *device, bool graceful)
{
    if (auto *local = qobject_cast<QLocalSocket *>(device)) {
        if (graceful) local->disconnectFromServer(); else local->abort();
    } else if (auto *tcp = qobject_cast<QAbstractSocket *>(device)) {
        if (graceful) tcp->disconnectFromHost(); else tcp->abort();
    }
    if (!graceful) device->deleteLater();
}

void PropertySyncSocketServer::expireIdle()
{
    if (m_idleTimeout <= 0) return;
    const qint64 now = m_clock.elapsed();
    const auto devices = m_peers.keys();
    for (auto *device : devices) {
        if (!m_peers.contains(device) || now - m_peers.value(device).lastSeen <= m_idleTimeout)
            continue;
        // AGENT-GUARD: forget before aborting so the leave broadcast happens
        // once, and the disconnected callback finds no peer to handle again.
        QObject::disconnect(device, nullptr, this, nullptr);
        forgetPeer(device);
        closePeer(device, false);
    }
}

void PropertySyncSocketServer::readPeer(QIODevice *device)
{
    auto it = m_peers.find(device);
    if (it == m_peers.end()) return;
    it->buffer.append(device->readAll());
    it->lastSeen = m_clock.elapsed();
    while (m_peers.contains(device)) {
        auto frame = nextSyncFrame(m_peers[device].buffer);
        if (!frame) break;
        Peer &peer = m_peers[device];
        if (!frame->isEmpty() && !peer.channel.isOpen()) {
            authenticate(device, *frame);
            continue;
        }
        const auto body = frame->isEmpty() ? std::nullopt : peer.channel.open(*frame);
        if (!body) {
            // A bad MAC or sequence: the stream can no longer be trusted.
            forgetPeer(device);
            closePeer(device, true);
            break;
        }
        handleFrame(device, *body);
    }
    if (m_peers.contains(device)
        && m_peers[device].buffer.size() > PropertySyncChannel::MaxFrameBytes) {
        forgetPeer(device);
        closePeer(device, true);
    }
}

void PropertySyncSocketServer::sendError(QIODevice *device, const QString &message,
                                         const QString &operationId, bool permanent)
{
    // AGENT-CONTRACT (docs/collaboration.md "Permissions"; the bridge in
    // property_sync_socket_client.cpp): a permanent reject means this
    // operation will never be accepted, and the replica discards it (D-296).
    const QJsonObject body{{QStringLiteral("type"), QStringLiteral("reject")},
                           {QStringLiteral("id"), operationId},
                           {QStringLiteral("reason"), message},
                           {QStringLiteral("permanent"), permanent}};
    if (m_peers.contains(device) && m_peers[device].channel.isOpen()) send(device, body);
    else writeSyncFrame(device, body);
}

void PropertySyncSocketServer::authenticate(QIODevice *device, const QJsonObject &frame)
{
    Peer &peer = m_peers[device];
    const QString actor = frame.value(QStringLiteral("actor")).toString();
    const QByteArray clientNonce = frame.value(QStringLiteral("nonce")).toString().toLatin1();
    const auto grant = m_grants.constFind(actor);
    const bool valid = frame.value(QStringLiteral("type")).toString() == QStringLiteral("hello")
        && m_service && grant != m_grants.cend()
        && frame.value(QStringLiteral("document")).toString() == m_service->documentId()
        && PropertySyncChannel::validNonce(clientNonce)
        && PropertySyncChannel::sameDigest(
               PropertySyncChannel::helloProof(grant->credential, m_service->documentId(),
                                               actor, peer.nonce, clientNonce),
               frame.value(QStringLiteral("proof")).toString().toLatin1());
    if (!valid) {
        sendError(device, QStringLiteral("Actor authorization failed"));
        forgetPeer(device);
        closePeer(device, true);
        return;
    }
    const auto devices = m_peers.keys();
    for (auto *old : devices) {
        if (old == device || !m_peers.contains(old) || m_peers.value(old).actorId != actor)
            continue;
        forgetPeer(old);   // the newer session replaces a stale one
        closePeer(old, true);
    }
    Peer &current = m_peers[device];
    current.actorId = actor;
    current.channel.begin(PropertySyncChannel::Role::Server, grant->credential,
                          current.nonce, clientNonce);
    if (!send(device, {{QStringLiteral("type"), QStringLiteral("ready")},
                       {QStringLiteral("mayEdit"), grant->mayEdit}}))
        return;
    if (!sendBase(device)) return;
    sendBacklog(device);
    if (!m_peers.contains(device)
        || !send(device, {{QStringLiteral("type"), QStringLiteral("replayEnd")}}))
        return;
    QHash<QString, QVariantMap> present = m_service->attachedPresence();
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it)
        if (it.key() != device && !it.value().actorId.isEmpty() && !it.value().presence.isEmpty())
            present.insert(it.value().actorId, it.value().presence);
    for (auto it = present.cbegin(); it != present.cend(); ++it)
        if (it.key() != actor && !send(device, presenceFrame(it.key(), it.value()))) return;
}

bool PropertySyncSocketServer::sendBase(QIODevice *device)
{
    if (!m_service || m_service->baseId().isEmpty()) return m_peers.contains(device);
    const QByteArray &base = m_service->base();
    const qsizetype parts = (base.size() + BasePartBytes - 1) / BasePartBytes;
    for (qsizetype part = 0; part < parts; ++part)
        if (!send(device, {{QStringLiteral("type"), QStringLiteral("base")},
                           {QStringLiteral("id"), m_service->baseId()},
                           {QStringLiteral("part"), int(part)},
                           {QStringLiteral("parts"), int(parts)},
                           {QStringLiteral("data"), QString::fromLatin1(
                                base.mid(part * BasePartBytes, BasePartBytes).toBase64())}}))
            return false;
    return true;
}

bool PropertySyncSocketServer::send(QIODevice *device, const QJsonObject &body)
{
    auto it = m_peers.find(device);
    if (it == m_peers.end()) return false;
    if (writeSyncFrame(device, it->channel.seal(body))) return true;
    forgetPeer(device);
    closePeer(device, true);
    return false;
}

void PropertySyncSocketServer::handleFrame(QIODevice *device, const QJsonObject &frame)
{
    Peer &peer = m_peers[device];
    const QString type = frame.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("ping")) {
        send(device, {{QStringLiteral("type"), QStringLiteral("pong")}});
        return;
    }
    if (type == QStringLiteral("presence") || type == QStringLiteral("presenceLeave")) {
        QVariantMap presence;
        if (type == QStringLiteral("presence")) {
            if (!frame.value(QStringLiteral("data")).isObject()
                || QJsonDocument(frame.value(QStringLiteral("data")).toObject())
                       .toJson(QJsonDocument::Compact).size() > 4096) {
                sendError(device, QStringLiteral("Invalid presence frame"));
                return;
            }
            presence = frame.value(QStringLiteral("data")).toObject().toVariantMap();
        }
        if (presence == peer.presence) return;
        peer.presence = presence;
        const QString actor = peer.actorId;
        broadcastPresence(device, presenceFrame(actor, presence));
        // Replicas attached to the hub's own service see peers too.
        m_service->relayPresence(actor, presence);
        return;
    }
    if (type != QStringLiteral("operation") || !frame.value(QStringLiteral("data")).isObject()) {
        sendError(device, QStringLiteral("Unsupported frame"));
        return;
    }
    const auto operation =
        propertySyncOperationFromJson(frame.value(QStringLiteral("data")).toObject());
    const QString operationId = operation ? operation->operationId : QString{};
    if (!operation || operation->actorId != peer.actorId
        || operation->documentId != m_service->documentId()) {
        sendError(device, QStringLiteral("Operation actor or document mismatch"), operationId,
                  true);
        return;
    }
    // AGENT-GUARD: permission is read per operation, never cached per
    // session, so a live grant change applies to the very next edit.
    const auto grant = m_grants.constFind(peer.actorId);
    if (grant == m_grants.cend() || !grant->mayEdit) {
        sendError(device, QStringLiteral("Actor has read-only permission"), operationId, true);
        return;
    }
    if (!m_service->submit(*operation)) {
        sendError(device, m_service->lastError(), operationId,
                  m_service->lastErrorPermanent());
        return;
    }
    if (m_peers.contains(device))
        send(device, {{QStringLiteral("type"), QStringLiteral("ack")},
                      {QStringLiteral("id"), operationId}});
}

void PropertySyncSocketServer::broadcastPresence(QIODevice *origin, const QJsonObject &frame)
{
    const auto devices = m_peers.keys();
    for (auto *device : devices) {
        if (device == origin || !m_peers.contains(device)
            || m_peers.value(device).actorId.isEmpty()) continue;
        send(device, frame);
    }
}

void PropertySyncSocketServer::sendBacklog(QIODevice *device)
{
    if (!m_service || !m_peers.contains(device)) return;
    const auto &operations = m_service->operations();
    while (m_peers.contains(device) && m_peers[device].sentCount < operations.size()) {
        const auto encoded = propertySyncOperationToJson(operations.at(m_peers[device].sentCount));
        if (!encoded || !send(device, {{QStringLiteral("type"), QStringLiteral("operation")},
                                       {QStringLiteral("data"), *encoded}}))
            return;
        ++m_peers[device].sentCount;
    }
}

} // namespace QindaTK
