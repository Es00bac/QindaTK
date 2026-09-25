// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "local_property_sync.h"
#include "property_sync_channel.h"
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace QindaTK {

// Sync endpoint for one document (the hub). setService marks the service
// authoritative, so it compacts and pushes each new base to live peers; the
// replicas attached to that service in this process share presence with
// every peer. It listens on an owner-only local socket
// (listen) and/or a local-network TCP port (listenNetwork). The host grants
// each actor a credential and editor/viewer permission; grants may change
// while listening. Every connection authenticates by challenge-response and
// then exchanges sealed frames (PropertySyncChannel). Credentials never enter
// the operation log or the wire.
class PropertySyncSocketServer final : public QObject {
    Q_OBJECT
public:
    explicit PropertySyncSocketServer(QObject *parent = nullptr);
    ~PropertySyncSocketServer() override;
    void setService(LocalPropertySyncService *service);
    void setSocketName(const QString &name);
    // While listening: a permission change is pushed to the actor's live
    // sessions; a credential change closes them so they must re-authenticate.
    bool grant(const QString &actorId, const QString &credential, bool mayEdit);
    // Removes the grant and closes the actor's sessions.
    bool revoke(const QString &actorId);
    bool listen();
    bool listenNetwork(const QHostAddress &address, quint16 port = 0);
    quint16 networkPort() const { return m_tcpServer.serverPort(); }
    // Sessions silent for this long are closed (0 disables). Default 15000 ms.
    void setIdleTimeout(int milliseconds);
    void close();
    QString lastError() const { return m_lastError; }
    bool isListening() const { return m_server.isListening() || m_tcpServer.isListening(); }
    int sessionCount() const;
signals:
    void errorOccurred(const QString &message);
private:
    struct Grant { QString credential; bool mayEdit = false; };
    struct Peer {
        QByteArray buffer;
        QByteArray nonce;
        QString actorId;
        QVariantMap presence;
        qsizetype sentCount = 0;
        qint64 lastSeen = 0;
        PropertySyncChannel channel;
    };
    bool fail(const QString &message);
    void addPeer(QIODevice *device);
    void forgetPeer(QIODevice *device);
    void closePeer(QIODevice *device, bool graceful);
    void readPeer(QIODevice *device);
    void authenticate(QIODevice *device, const QJsonObject &frame);
    void handleFrame(QIODevice *device, const QJsonObject &frame);
    bool send(QIODevice *device, const QJsonObject &body);
    void sendBacklog(QIODevice *device);
    void broadcastPresence(QIODevice *origin, const QJsonObject &frame);
    void sendError(QIODevice *device, const QString &message, const QString &operationId = {},
                   bool permanent = false);
    bool sendBase(QIODevice *device);
    void endSessions(const QString &actorId, const QString &reason);
    void expireIdle();
    QLocalServer m_server;
    QTcpServer m_tcpServer;
    QTimer m_idleTimer;
    QElapsedTimer m_clock;
    int m_idleTimeout = 15000;
    QPointer<LocalPropertySyncService> m_service;
    QString m_socketName;
    QString m_lastError;
    QHash<QString, Grant> m_grants;
    QHash<QIODevice *, Peer> m_peers;
    QVector<QMetaObject::Connection> m_serviceConnections;
};

// Local-service bridge. The client process owns a LocalPropertySyncService
// with storagePath set: its durable outbox is the outgoing queue. On
// reconnect, this actor's unacknowledged operations are resent; server dedup
// uses operation UUID and exact typed content. A permanent reject discards
// the operation locally; a hub base (after compaction) is adopted. Presence
// never enters either log.
// setHost selects the TCP transport; otherwise the local socket name is used.
class PropertySyncSocketClient final : public QObject {
    Q_OBJECT
public:
    explicit PropertySyncSocketClient(QObject *parent = nullptr);
    ~PropertySyncSocketClient() override;
    void setService(LocalPropertySyncService *service);
    void setLocalClient(LocalPropertySyncClient *client);
    void setSocketName(const QString &name);
    void setHost(const QString &host, quint16 port);
    void setActorId(const QString &actorId);
    void setCredential(const QString &credential);
    // Ping period while connected (0 disables) and silence limit before the
    // client drops the connection (0 disables). Defaults 5000 and 15000 ms.
    void setHeartbeatInterval(int milliseconds);
    void setIdleTimeout(int milliseconds);
    void setPresence(const QVariantMap &presence);
    bool connectToServer();
    void disconnectFromServer();
    bool isConnected() const { return m_ready; }
    bool mayEdit() const { return m_mayEdit; }
    QString lastError() const { return m_lastError; }
signals:
    void connected();
    void disconnected();
    void operationRejected(const QString &operationId, const QString &reason);
    // A permanently refused operation was removed from the local store and
    // the local replicas were rebuilt without it (D-179).
    void operationDiscarded(const QString &operationId, const QString &reason);
    void permissionChanged(bool mayEdit);
    void revoked(const QString &reason);
    void presenceChanged(const QString &actorId, const QVariantMap &presence);
    void presenceLeft(const QString &actorId);
    void errorOccurred(const QString &message);
private:
    bool fail(const QString &message);
    bool idle() const;
    QIODevice *device();
    void abortConnection();
    void onConnected();
    void onDisconnected();
    void readFrames();
    void handlePlain(const QJsonObject &frame);
    void handleFrame(const QJsonObject &frame);
    bool send(const QJsonObject &body);
    void setMayEdit(bool mayEdit);
    void heartbeat();
    void sendOutgoing();
    void resetRebase();
    void receiveBasePart(const QJsonObject &frame);
    bool finishRebase();
    void sendCurrentPresence();
    void leaveRemotePresence();
    QLocalSocket m_socket;
    QTcpSocket m_tcpSocket;
    QTimer m_heartbeat;
    QElapsedTimer m_lastReceived;
    int m_idleTimeout = 15000;
    PropertySyncChannel m_channel;
    QPointer<LocalPropertySyncService> m_service;
    QPointer<LocalPropertySyncClient> m_localClient;
    QString m_socketName;
    QString m_host;
    quint16 m_port = 0;
    QString m_actorId;
    QString m_credential;
    QString m_lastError;
    QByteArray m_buffer;
    QSet<QString> m_sent;
    QHash<QString, QVariantMap> m_remotePresence;
    QVariantMap m_localPresence;
    QByteArray m_baseBytes;
    QString m_baseId;
    int m_baseParts = 0;
    int m_baseTotal = 0;
    bool m_rebasing = false;
    QVector<PropertySyncOperation> m_rebaseTail;
    bool m_ready = false;
    bool m_mayEdit = false;
    QMetaObject::Connection m_serviceConnection;
    QMetaObject::Connection m_localPresenceConnection;
    QMetaObject::Connection m_localDestroyedConnection;
};

} // namespace QindaTK
