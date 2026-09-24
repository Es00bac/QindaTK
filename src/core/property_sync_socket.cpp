// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_sync_socket.h"
#include <QJsonDocument>
#include <QUuid>

namespace QindaTK {
namespace {
constexpr qsizetype MaxFrameBytes = 96 * 1024;
constexpr qsizetype MaxQueuedBytes = 8 * 1024 * 1024;

bool canonicalUuid(const QString &id)
{
    const QUuid uuid(id);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

bool writeFrame(QLocalSocket *socket, const QJsonObject &object)
{
    QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxFrameBytes || socket->bytesToWrite() + bytes.size() > MaxQueuedBytes)
        return false;
    bytes.append('\n');
    return socket->write(bytes) == bytes.size();
}

std::optional<QJsonObject> nextFrame(QByteArray &buffer)
{
    const auto end = buffer.indexOf('\n');
    if (end < 0) return std::nullopt;
    const QByteArray bytes = buffer.left(end);
    buffer.remove(0, end + 1);
    if (bytes.size() > MaxFrameBytes) return QJsonObject{};
    const auto document = QJsonDocument::fromJson(bytes);
    return document.isObject() ? document.object() : QJsonObject{};
}
}

PropertySyncSocketServer::PropertySyncSocketServer(QObject *parent) : QObject(parent)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this,
            &PropertySyncSocketServer::acceptPending);
}

void PropertySyncSocketServer::setService(LocalPropertySyncService *service)
{
    if (isListening()) return;
    QObject::disconnect(m_serviceConnection);
    m_service = service;
    if (!service) return;
    m_serviceConnection = connect(service, &LocalPropertySyncService::changed, this, [this] {
        const auto sockets = m_peers.keys();
        for (auto *socket : sockets)
            if (m_peers.contains(socket) && !m_peers[socket].actorId.isEmpty())
                sendBacklog(socket);
    });
}

void PropertySyncSocketServer::setSocketName(const QString &name)
{
    if (!isListening()) m_socketName = name;
}

bool PropertySyncSocketServer::grant(const QString &actorId,
                                     const QString &credential, bool mayEdit)
{
    if (!canonicalUuid(actorId) || credential.size() < 16 || credential.size() > 256)
        return fail(QStringLiteral("Actor ID or credential is invalid"));
    m_grants.insert(actorId, {credential, mayEdit});
    return true;
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

void PropertySyncSocketServer::close()
{
    m_server.close();
    const auto sockets = m_peers.keys();
    for (auto *socket : sockets) socket->disconnectFromServer();
    m_peers.clear();
}

void PropertySyncSocketServer::acceptPending()
{
    while (auto *socket = m_server.nextPendingConnection()) {
        if (m_peers.size() >= 8) { socket->disconnectFromServer(); socket->deleteLater(); continue; }
        m_peers.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this,
                [this, socket] { readPeer(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_peers.remove(socket);
            socket->deleteLater();
        });
    }
}

void PropertySyncSocketServer::readPeer(QLocalSocket *socket)
{
    auto it = m_peers.find(socket);
    if (it == m_peers.end()) return;
    it->buffer.append(socket->readAll());
    while (m_peers.contains(socket)) {
        auto frame = nextFrame(m_peers[socket].buffer);
        if (!frame) break;
        if (frame->isEmpty()) { sendError(socket, QStringLiteral("Invalid frame")); socket->disconnectFromServer(); break; }
        handleFrame(socket, *frame);
    }
    if (m_peers.contains(socket) && m_peers[socket].buffer.size() > MaxFrameBytes) {
        sendError(socket, QStringLiteral("Frame limit exceeded"));
        socket->disconnectFromServer();
    }
}

void PropertySyncSocketServer::sendError(QLocalSocket *socket, const QString &message,
                                         const QString &operationId)
{
    writeFrame(socket, {{QStringLiteral("type"), QStringLiteral("reject")},
                        {QStringLiteral("id"), operationId},
                        {QStringLiteral("reason"), message}});
}

void PropertySyncSocketServer::handleFrame(QLocalSocket *socket, const QJsonObject &frame)
{
    auto &peer = m_peers[socket];
    const QString type = frame.value(QStringLiteral("type")).toString();
    if (peer.actorId.isEmpty()) {
        const QString actor = frame.value(QStringLiteral("actor")).toString();
        const auto grant = m_grants.constFind(actor);
        if (type != QStringLiteral("hello") || !m_service
            || frame.value(QStringLiteral("document")).toString() != m_service->documentId()
            || grant == m_grants.cend()
            || frame.value(QStringLiteral("credential")).toString() != grant->credential) {
            sendError(socket, QStringLiteral("Actor authorization failed"));
            socket->disconnectFromServer();
            return;
        }
        peer.actorId = actor;
        if (!writeFrame(socket, {{QStringLiteral("type"), QStringLiteral("ready")}})) {
            socket->disconnectFromServer(); return;
        }
        sendBacklog(socket);
        if (!writeFrame(socket, {{QStringLiteral("type"), QStringLiteral("replayEnd")}}))
            socket->disconnectFromServer();
        return;
    }
    if (type != QStringLiteral("operation") || !frame.value(QStringLiteral("data")).isObject()) {
        sendError(socket, QStringLiteral("Unsupported frame"));
        return;
    }
    const auto operation = propertySyncOperationFromJson(frame.value(QStringLiteral("data")).toObject());
    const QString operationId = operation ? operation->operationId : QString{};
    if (!operation || operation->actorId != peer.actorId
        || operation->documentId != m_service->documentId()) {
        sendError(socket, QStringLiteral("Operation actor or document mismatch"), operationId);
        return;
    }
    const auto grant = m_grants.constFind(peer.actorId);
    if (grant == m_grants.cend() || !grant->mayEdit) {
        sendError(socket, QStringLiteral("Actor has read-only permission"), operationId);
        return;
    }
    if (!m_service->submit(*operation)) {
        sendError(socket, m_service->lastError(), operationId);
        return;
    }
    if (!writeFrame(socket, {{QStringLiteral("type"), QStringLiteral("ack")},
                             {QStringLiteral("id"), operationId}}))
        socket->disconnectFromServer();
}

void PropertySyncSocketServer::sendBacklog(QLocalSocket *socket)
{
    if (!m_service || !m_peers.contains(socket)) return;
    auto &peer = m_peers[socket];
    const auto &operations = m_service->operations();
    while (peer.sentCount < operations.size()) {
        const auto encoded = propertySyncOperationToJson(operations.at(peer.sentCount));
        if (!encoded || !writeFrame(socket, {{QStringLiteral("type"), QStringLiteral("operation")},
                                      {QStringLiteral("data"), *encoded}})) {
            socket->disconnectFromServer(); return;
        }
        ++peer.sentCount;
    }
}

PropertySyncSocketClient::PropertySyncSocketClient(QObject *parent) : QObject(parent)
{
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        if (!m_service || !writeFrame(&m_socket,
            {{QStringLiteral("type"), QStringLiteral("hello")},
             {QStringLiteral("actor"), m_actorId},
             {QStringLiteral("document"), m_service->documentId()},
             {QStringLiteral("credential"), m_credential}}))
            fail(QStringLiteral("Cannot send authorization frame"));
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &PropertySyncSocketClient::readFrames);
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        m_ready = false;
        m_sent.clear();
        m_buffer.clear();
        emit disconnected();
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) { fail(m_socket.errorString()); });
}

void PropertySyncSocketClient::setService(LocalPropertySyncService *service)
{
    if (m_socket.state() != QLocalSocket::UnconnectedState) return;
    QObject::disconnect(m_serviceConnection);
    m_service = service;
    if (service)
        m_serviceConnection = connect(service, &LocalPropertySyncService::changed,
                                      this, &PropertySyncSocketClient::sendOutgoing);
}

void PropertySyncSocketClient::setSocketName(const QString &name)
{
    if (m_socket.state() == QLocalSocket::UnconnectedState) m_socketName = name;
}

void PropertySyncSocketClient::setActorId(const QString &actorId)
{
    if (m_socket.state() == QLocalSocket::UnconnectedState) m_actorId = actorId;
}

void PropertySyncSocketClient::setCredential(const QString &credential)
{
    if (m_socket.state() == QLocalSocket::UnconnectedState) m_credential = credential;
}

bool PropertySyncSocketClient::fail(const QString &message)
{
    m_lastError = message;
    emit errorOccurred(message);
    return false;
}

bool PropertySyncSocketClient::connectToServer()
{
    if (m_socket.state() != QLocalSocket::UnconnectedState) return true;
    if (!m_service || !canonicalUuid(m_service->documentId()) || !canonicalUuid(m_actorId)
        || m_service->storagePath().isEmpty() || m_credential.isEmpty() || m_socketName.isEmpty())
        return fail(QStringLiteral("Socket client configuration is incomplete"));
    m_lastError.clear();
    m_sent.clear();
    m_socket.connectToServer(m_socketName);
    return true;
}

void PropertySyncSocketClient::disconnectFromServer()
{
    m_socket.disconnectFromServer();
}

void PropertySyncSocketClient::readFrames()
{
    m_buffer.append(m_socket.readAll());
    while (true) {
        auto frame = nextFrame(m_buffer);
        if (!frame) break;
        if (frame->isEmpty()) { fail(QStringLiteral("Invalid server frame")); m_socket.abort(); return; }
        handleFrame(*frame);
    }
    if (m_buffer.size() > MaxFrameBytes) {
        fail(QStringLiteral("Server frame limit exceeded"));
        m_socket.abort();
    }
}

void PropertySyncSocketClient::handleFrame(const QJsonObject &frame)
{
    const QString type = frame.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("ready")) return;
    if (type == QStringLiteral("replayEnd")) {
        m_ready = true;
        emit connected();
        sendOutgoing();
    } else if (type == QStringLiteral("operation")) {
        const auto operation = propertySyncOperationFromJson(frame.value(QStringLiteral("data")).toObject());
        if (!operation || !m_service || !m_service->submit(*operation))
            fail(m_service ? m_service->lastError() : QStringLiteral("No local service"));
    } else if (type == QStringLiteral("reject")) {
        const QString id = frame.value(QStringLiteral("id")).toString();
        const QString reason = frame.value(QStringLiteral("reason")).toString();
        fail(reason);
        emit operationRejected(id, reason);
    } else if (type != QStringLiteral("ack")) {
        fail(QStringLiteral("Unknown server frame"));
        m_socket.abort();
    }
}

void PropertySyncSocketClient::sendOutgoing()
{
    if (!m_ready || !m_service) return;
    for (const auto &operation : m_service->operations()) {
        if (operation.actorId != m_actorId || m_sent.contains(operation.operationId)) continue;
        const auto encoded = propertySyncOperationToJson(operation);
        if (!encoded || !writeFrame(&m_socket,
            {{QStringLiteral("type"), QStringLiteral("operation")},
             {QStringLiteral("data"), *encoded}})) {
            fail(QStringLiteral("Cannot send operation frame"));
            m_socket.abort();
            return;
        }
        m_sent.insert(operation.operationId);
    }
}

} // namespace QindaTK
