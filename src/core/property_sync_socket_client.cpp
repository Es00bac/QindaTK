// SPDX-License-Identifier: LGPL-3.0-or-later
// PropertySyncSocketClient: bridges a local replica to a sync endpoint over
// the owner-only local socket or the local-network TCP transport.
#include "property_sync_socket.h"
#include "property_sync_detail_p.h"
#include <QJsonDocument>
#include <algorithm>

namespace QindaTK {
using sync_detail::canonicalUuid;

PropertySyncSocketClient::PropertySyncSocketClient(QObject *parent) : QObject(parent)
{
    connect(&m_socket, &QLocalSocket::connected, this, &PropertySyncSocketClient::onConnected);
    connect(&m_tcpSocket, &QTcpSocket::connected, this, &PropertySyncSocketClient::onConnected);
    connect(&m_socket, &QLocalSocket::readyRead, this, &PropertySyncSocketClient::readFrames);
    connect(&m_tcpSocket, &QTcpSocket::readyRead, this, &PropertySyncSocketClient::readFrames);
    connect(&m_socket, &QLocalSocket::disconnected, this,
            &PropertySyncSocketClient::onDisconnected);
    connect(&m_tcpSocket, &QTcpSocket::disconnected, this,
            &PropertySyncSocketClient::onDisconnected);
    connect(&m_socket, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) { fail(m_socket.errorString()); });
    connect(&m_tcpSocket, &QAbstractSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) { fail(m_tcpSocket.errorString()); });
    m_heartbeat.setInterval(5000);
    connect(&m_heartbeat, &QTimer::timeout, this, &PropertySyncSocketClient::heartbeat);
}

PropertySyncSocketClient::~PropertySyncSocketClient()
{
    // Member hashes are destroyed before the socket members. Suppress their
    // disconnect callbacks while those hashes still exist.
    QObject::disconnect(&m_socket, nullptr, this, nullptr);
    QObject::disconnect(&m_tcpSocket, nullptr, this, nullptr);
    m_heartbeat.stop();
    m_socket.abort();
    m_tcpSocket.abort();
}

bool PropertySyncSocketClient::idle() const
{
    return m_socket.state() == QLocalSocket::UnconnectedState
        && m_tcpSocket.state() == QAbstractSocket::UnconnectedState;
}

QIODevice *PropertySyncSocketClient::device()
{
    if (m_host.isEmpty()) return &m_socket;
    return &m_tcpSocket;
}

void PropertySyncSocketClient::setService(LocalPropertySyncService *service)
{
    if (!idle()) return;
    QObject::disconnect(m_serviceConnection);
    m_service = service;
    if (service)
        m_serviceConnection = connect(service, &LocalPropertySyncService::changed,
                                      this, &PropertySyncSocketClient::sendOutgoing);
}

void PropertySyncSocketClient::setLocalClient(LocalPropertySyncClient *client)
{
    QObject::disconnect(m_localPresenceConnection);
    QObject::disconnect(m_localDestroyedConnection);
    if (m_localClient)
        for (auto it = m_remotePresence.cbegin(); it != m_remotePresence.cend(); ++it)
            m_localClient->receivePresenceLeft(it.key());
    m_localClient = client;
    setPresence(client ? client->m_presence : QVariantMap{});
    if (!client) return;
    if (m_ready) client->setReadOnly(!m_mayEdit);
    for (auto it = m_remotePresence.cbegin(); it != m_remotePresence.cend(); ++it)
        client->receivePresence(it.key(), it.value());
    m_localPresenceConnection = connect(client, &LocalPropertySyncClient::ownPresenceChanged,
                                        this, &PropertySyncSocketClient::setPresence);
    m_localDestroyedConnection = connect(client, &QObject::destroyed, this,
                                         [this] { m_localClient = nullptr; setPresence({}); });
}

void PropertySyncSocketClient::setSocketName(const QString &name)
{
    if (idle()) m_socketName = name;
}

void PropertySyncSocketClient::setHost(const QString &host, quint16 port)
{
    if (!idle()) return;
    m_host = host;
    m_port = port;
}

void PropertySyncSocketClient::setActorId(const QString &actorId)
{
    if (idle()) m_actorId = actorId;
}

void PropertySyncSocketClient::setCredential(const QString &credential)
{
    if (idle()) m_credential = credential;
}

void PropertySyncSocketClient::setHeartbeatInterval(int milliseconds)
{
    m_heartbeat.stop();
    m_heartbeat.setInterval(std::max(0, milliseconds));
    if (milliseconds > 0 && !idle()) m_heartbeat.start();
}

void PropertySyncSocketClient::setIdleTimeout(int milliseconds)
{
    m_idleTimeout = std::max(0, milliseconds);
}

void PropertySyncSocketClient::setPresence(const QVariantMap &presence)
{
    const QJsonDocument document = QJsonDocument::fromVariant(presence);
    if (!document.isObject() || document.toJson(QJsonDocument::Compact).size() > 4096) {
        fail(QStringLiteral("Invalid or oversized presence"));
        return;
    }
    if (presence == m_localPresence) return;
    m_localPresence = presence;
    if (!m_ready) return;
    if (presence.isEmpty()) {
        if (!send({{QStringLiteral("type"), QStringLiteral("presenceLeave")}}))
            fail(QStringLiteral("Cannot send presence leave"));
    } else {
        sendCurrentPresence();
    }
}

bool PropertySyncSocketClient::fail(const QString &message)
{
    m_lastError = message;
    emit errorOccurred(message);
    return false;
}

bool PropertySyncSocketClient::connectToServer()
{
    if (!idle()) return true;
    if (!m_service || !canonicalUuid(m_service->documentId()) || !canonicalUuid(m_actorId)
        || m_service->storagePath().isEmpty() || m_credential.isEmpty()
        || (m_host.isEmpty() ? m_socketName.isEmpty() : m_port == 0))
        return fail(QStringLiteral("Socket client configuration is incomplete"));
    if (m_localClient && (m_localClient->service() != m_service
                          || m_localClient->actorId() != m_actorId))
        return fail(QStringLiteral("Local presence client identity mismatch"));
    if (m_service->isAuthoritative())
        return fail(QStringLiteral("A hub service cannot also be a replica of another hub"));
    m_lastError.clear();
    m_sent.clear();
    m_buffer.clear();
    resetRebase();
    m_channel.reset();
    if (m_host.isEmpty()) m_socket.connectToServer(m_socketName);
    else m_tcpSocket.connectToHost(m_host, m_port);
    return true;
}

void PropertySyncSocketClient::disconnectFromServer()
{
    m_socket.disconnectFromServer();
    m_tcpSocket.disconnectFromHost();
}

void PropertySyncSocketClient::abortConnection()
{
    m_heartbeat.stop();
    m_socket.abort();
    m_tcpSocket.abort();
    m_channel.reset();
}

void PropertySyncSocketClient::onConnected()
{
    // Wait for the server challenge; the credential itself is never sent.
    m_lastReceived.start();
    if (m_heartbeat.interval() > 0) m_heartbeat.start();
}

void PropertySyncSocketClient::onDisconnected()
{
    m_heartbeat.stop();
    m_ready = false;
    m_sent.clear();
    m_buffer.clear();
    resetRebase();
    m_channel.reset();
    leaveRemotePresence();
    emit disconnected();
}

void PropertySyncSocketClient::heartbeat()
{
    if (m_idleTimeout > 0 && m_lastReceived.isValid()
        && m_lastReceived.elapsed() > m_idleTimeout) {
        fail(QStringLiteral("Sync server stopped responding"));
        abortConnection();
        return;
    }
    if (m_ready) send({{QStringLiteral("type"), QStringLiteral("ping")}});
}

bool PropertySyncSocketClient::send(const QJsonObject &body)
{
    return writeSyncFrame(device(), m_channel.seal(body));
}

void PropertySyncSocketClient::setMayEdit(bool mayEdit)
{
    const bool changed = m_mayEdit != mayEdit;
    m_mayEdit = mayEdit;
    // AGENT-GUARD: a viewer's replica refuses new local edits, so it cannot
    // diverge from the server with operations the server will reject.
    if (m_localClient) m_localClient->setReadOnly(!mayEdit);
    if (changed) emit permissionChanged(mayEdit);
}

void PropertySyncSocketClient::readFrames()
{
    m_buffer.append(device()->readAll());
    m_lastReceived.restart();
    while (!idle()) {
        auto frame = nextSyncFrame(m_buffer);
        if (!frame) break;
        if (frame->isEmpty()) {
            fail(QStringLiteral("Invalid server frame"));
            abortConnection();
            return;
        }
        if (!frame->contains(QStringLiteral("mac"))) {
            handlePlain(*frame);
            continue;
        }
        const auto body = m_channel.open(*frame);
        if (!body) {
            fail(QStringLiteral("Server frame failed authentication"));
            abortConnection();
            return;
        }
        handleFrame(*body);
    }
    if (m_buffer.size() > PropertySyncChannel::MaxFrameBytes) {
        fail(QStringLiteral("Server frame limit exceeded"));
        abortConnection();
    }
}

void PropertySyncSocketClient::handlePlain(const QJsonObject &frame)
{
    const QString type = frame.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("challenge") && !m_channel.isOpen()) {
        const QByteArray serverNonce = frame.value(QStringLiteral("nonce")).toString().toLatin1();
        if (frame.value(QStringLiteral("version")).toInt() != 2
            || !PropertySyncChannel::validNonce(serverNonce) || !m_service) {
            fail(QStringLiteral("Unsupported sync server"));
            abortConnection();
            return;
        }
        const QByteArray clientNonce = PropertySyncChannel::createNonce();
        const QString documentId = m_service->documentId();
        const QByteArray proof = PropertySyncChannel::helloProof(
            m_credential, documentId, m_actorId, serverNonce, clientNonce);
        if (!writeSyncFrame(device(), {{QStringLiteral("type"), QStringLiteral("hello")},
                                       {QStringLiteral("actor"), m_actorId},
                                       {QStringLiteral("document"), documentId},
                                       {QStringLiteral("nonce"), QString::fromLatin1(clientNonce)},
                                       {QStringLiteral("proof"), QString::fromLatin1(proof)}})) {
            fail(QStringLiteral("Cannot send authorization frame"));
            abortConnection();
            return;
        }
        m_channel.begin(PropertySyncChannel::Role::Client, m_credential, serverNonce,
                        clientNonce);
        return;
    }
    if (type == QStringLiteral("reject") && !m_ready) {
        // The server refused the hello and closes the connection.
        const QString reason = frame.value(QStringLiteral("reason")).toString();
        fail(reason);
        emit operationRejected(frame.value(QStringLiteral("id")).toString(), reason);
        return;
    }
    fail(QStringLiteral("Unexpected unauthenticated frame"));
    abortConnection();
}

void PropertySyncSocketClient::handleFrame(const QJsonObject &frame)
{
    const QString type = frame.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("ready") || type == QStringLiteral("permission")) {
        setMayEdit(frame.value(QStringLiteral("mayEdit")).toBool());
    } else if (type == QStringLiteral("replayEnd") || type == QStringLiteral("rebased")) {
        if (m_rebasing && !finishRebase()) return;
        if (type == QStringLiteral("rebased")) return;
        m_ready = true;
        emit connected();
        sendOutgoing();
        sendCurrentPresence();
    } else if (type == QStringLiteral("base")) {
        receiveBasePart(frame);
    } else if (type == QStringLiteral("operation")) {
        const auto operation =
            propertySyncOperationFromJson(frame.value(QStringLiteral("data")).toObject());
        if (operation && m_rebasing) {
            // Held until the marker: the base and its tail replace the log at once.
            if (m_rebaseTail.size() >= sync_detail::MaxOperations) {
                fail(QStringLiteral("Hub tail is too long"));
                abortConnection();
                return;
            }
            m_rebaseTail.append(*operation);
        } else if (!operation || !m_service || !m_service->submitUpstream(*operation)) {
            fail(m_service ? m_service->lastError() : QStringLiteral("No local service"));
        }
    } else if (type == QStringLiteral("ack")) {
        if (m_service) m_service->settle(frame.value(QStringLiteral("id")).toString());
    } else if (type == QStringLiteral("reject")) {
        const QString id = frame.value(QStringLiteral("id")).toString();
        const QString reason = frame.value(QStringLiteral("reason")).toString();
        fail(reason);
        // AGENT-GUARD (D-205): an edit the hub will never accept is taken out
        // of this replica, or it would stay visible here and nowhere else and
        // be resent on every reconnect. A transient refusal stays queued.
        if (frame.value(QStringLiteral("permanent")).toBool() && m_service
            && m_service->discard(id))
            emit operationDiscarded(id, reason);
        emit operationRejected(id, reason);
    } else if (type == QStringLiteral("revoked")) {
        const QString reason = frame.value(QStringLiteral("reason")).toString();
        fail(reason);
        setMayEdit(false);
        emit revoked(reason);
    } else if (type == QStringLiteral("presence")) {
        const QString actor = frame.value(QStringLiteral("actor")).toString();
        const QJsonValue data = frame.value(QStringLiteral("data"));
        if (!canonicalUuid(actor) || actor == m_actorId || !data.isObject()
            || QJsonDocument(data.toObject()).toJson(QJsonDocument::Compact).size() > 4096) {
            fail(QStringLiteral("Invalid remote presence"));
            abortConnection();
            return;
        }
        const QVariantMap presence = data.toObject().toVariantMap();
        if (presence.isEmpty()) return;
        m_remotePresence.insert(actor, presence);
        emit presenceChanged(actor, presence);
        if (m_localClient) m_localClient->receivePresence(actor, presence);
    } else if (type == QStringLiteral("presenceLeave")) {
        const QString actor = frame.value(QStringLiteral("actor")).toString();
        if (m_remotePresence.remove(actor)) {
            emit presenceLeft(actor);
            if (m_localClient) m_localClient->receivePresenceLeft(actor);
        }
    } else if (type != QStringLiteral("pong")) {
        fail(QStringLiteral("Unknown server frame"));
        abortConnection();
    }
}

void PropertySyncSocketClient::sendCurrentPresence()
{
    if (!m_ready || m_localPresence.isEmpty()) return;
    if (!send({{QStringLiteral("type"), QStringLiteral("presence")},
               {QStringLiteral("data"), QJsonObject::fromVariantMap(m_localPresence)}}))
        fail(QStringLiteral("Cannot send presence"));
}

void PropertySyncSocketClient::leaveRemotePresence()
{
    const auto actors = m_remotePresence.keys();
    m_remotePresence.clear();
    for (const QString &actor : actors) {
        emit presenceLeft(actor);
        if (m_localClient) m_localClient->receivePresenceLeft(actor);
    }
}

void PropertySyncSocketClient::resetRebase()
{
    m_rebasing = false;
    m_baseParts = 0;
    m_baseBytes.clear();
    m_baseId.clear();
    m_rebaseTail.clear();
}

void PropertySyncSocketClient::receiveBasePart(const QJsonObject &frame)
{
    const QString id = frame.value(QStringLiteral("id")).toString();
    const int part = frame.value(QStringLiteral("part")).toInt(-1);
    const int parts = frame.value(QStringLiteral("parts")).toInt(-1);
    const QByteArray encoded = frame.value(QStringLiteral("data")).toString().toLatin1();
    const auto decoded = QByteArray::fromBase64Encoding(encoded);
    const bool first = part == 0;
    const int maxParts = int(sync_detail::MaxStateBytes / (48 * 1024)) + 1;
    if (!sync_detail::canonicalUuid(id) || parts < 1 || parts > maxParts || !decoded
        || (first ? false : (id != m_baseId || part != m_baseParts || parts != m_baseTotal))
        || m_baseBytes.size() + decoded.decoded.size() > sync_detail::MaxStateBytes) {
        fail(QStringLiteral("Invalid hub base frame"));
        abortConnection();
        return;
    }
    if (first) {
        resetRebase();
        m_baseId = id;
        m_baseTotal = parts;
    }
    m_baseBytes.append(decoded.decoded);
    m_baseParts = part + 1;
    m_rebasing = m_baseParts == m_baseTotal;
}

bool PropertySyncSocketClient::finishRebase()
{
    const bool adopted = m_service
        && m_service->adoptBase(m_baseBytes, m_baseId, m_rebaseTail);
    const QString error = m_service ? m_service->lastError() : QStringLiteral("No local service");
    resetRebase();
    if (adopted) return true;
    fail(error);
    abortConnection();
    return false;
}

void PropertySyncSocketClient::sendOutgoing()
{
    if (!m_ready || !m_service) return;
    // Only unacknowledged local operations: the outbox is the durable queue.
    for (const auto &operation : m_service->outbox()) {
        if (operation.actorId != m_actorId || m_sent.contains(operation.operationId)) continue;
        const auto encoded = propertySyncOperationToJson(operation);
        if (!encoded || !send({{QStringLiteral("type"), QStringLiteral("operation")},
                               {QStringLiteral("data"), *encoded}})) {
            fail(QStringLiteral("Cannot send operation frame"));
            abortConnection();
            return;
        }
        m_sent.insert(operation.operationId);
    }
}

} // namespace QindaTK
