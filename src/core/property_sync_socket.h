// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "local_property_sync.h"
#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSet>

namespace QindaTK {

// Owner-only local socket endpoint. The host grants each actor a credential
// and editor/viewer permission before listen(); credentials never enter the
// operation log. A server routes the existing typed operation packets.
class PropertySyncSocketServer final : public QObject {
    Q_OBJECT
public:
    explicit PropertySyncSocketServer(QObject *parent = nullptr);
    void setService(LocalPropertySyncService *service);
    void setSocketName(const QString &name);
    bool grant(const QString &actorId, const QString &credential, bool mayEdit);
    bool listen();
    void close();
    QString lastError() const { return m_lastError; }
    bool isListening() const { return m_server.isListening(); }
signals:
    void errorOccurred(const QString &message);
private:
    struct Grant { QString credential; bool mayEdit = false; };
    struct Peer { QByteArray buffer; QString actorId; qsizetype sentCount = 0; };
    bool fail(const QString &message);
    void acceptPending();
    void readPeer(QLocalSocket *socket);
    void handleFrame(QLocalSocket *socket, const QJsonObject &frame);
    void sendBacklog(QLocalSocket *socket);
    void sendError(QLocalSocket *socket, const QString &message, const QString &operationId = {});
    QLocalServer m_server;
    QPointer<LocalPropertySyncService> m_service;
    QString m_socketName;
    QString m_lastError;
    QHash<QString, Grant> m_grants;
    QHash<QLocalSocket *, Peer> m_peers;
    QMetaObject::Connection m_serviceConnection;
};

// Local-service bridge. The client process owns a LocalPropertySyncService
// with storagePath set: its durable operation log is also its outgoing queue.
// On reconnect, only this actor's operations are resent; server dedup uses
// operation UUID and exact typed content. Presence never enters either log.
class PropertySyncSocketClient final : public QObject {
    Q_OBJECT
public:
    explicit PropertySyncSocketClient(QObject *parent = nullptr);
    void setService(LocalPropertySyncService *service);
    void setSocketName(const QString &name);
    void setActorId(const QString &actorId);
    void setCredential(const QString &credential);
    bool connectToServer();
    void disconnectFromServer();
    bool isConnected() const { return m_ready; }
    QString lastError() const { return m_lastError; }
signals:
    void connected();
    void disconnected();
    void operationRejected(const QString &operationId, const QString &reason);
    void errorOccurred(const QString &message);
private:
    bool fail(const QString &message);
    void readFrames();
    void handleFrame(const QJsonObject &frame);
    void sendOutgoing();
    QLocalSocket m_socket;
    QPointer<LocalPropertySyncService> m_service;
    QString m_socketName;
    QString m_actorId;
    QString m_credential;
    QString m_lastError;
    QByteArray m_buffer;
    QSet<QString> m_sent;
    bool m_ready = false;
    QMetaObject::Connection m_serviceConnection;
};

} // namespace QindaTK
