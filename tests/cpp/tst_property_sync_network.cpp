// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-082 local-network transport: authenticated sealed frames, TCP
// convergence after offline edits, live permission changes, credential
// rotation and revocation, and idle-session expiry.
#include "property_sync_socket.h"
#include "reference_document_codec.h"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
constexpr auto ActorA = "00000000-0000-0000-0000-00000000000a";
constexpr auto ActorB = "00000000-0000-0000-0000-00000000000b";
constexpr auto CredentialA = "network-credential-aaaa";
constexpr auto CredentialB = "network-credential-bbbb";

QString gain() { return QStringLiteral("gain"); }
QString name() { return QStringLiteral("name"); }

PropertyDescriptor describe(const QString &id, QMetaType type, const QVariant &defaultValue)
{
    PropertyDescriptor descriptor;
    descriptor.id = id;
    descriptor.type = type;
    descriptor.defaultValue = defaultValue;
    return descriptor;
}

// One process-independent peer: its own replica, durable local log and
// socket bridge, exactly as a second machine would hold them.
struct Peer {
    std::unique_ptr<PropertyDocument> document;
    LocalPropertySyncService local;
    LocalPropertySyncClient client;
    PropertySyncSocketClient socket;
};

struct Network {
    QTemporaryDir directory;
    EntityId entity = EntityId::create();
    QByteArray baseline;
    QString documentId;
    LocalPropertySyncService service;
    PropertySyncSocketServer server;

    bool start()
    {
        PropertyDocument base;
        if (!directory.isValid()
            || !base.defineProperty(describe(gain(), QMetaType::fromType<double>(), 0.0))
            || !base.defineProperty(describe(name(), QMetaType::fromType<QString>(),
                                             QStringLiteral("base")))
            || !base.addEntity(entity))
            return false;
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        if (!snapshot) return false;
        baseline = snapshot->payload;
        documentId = base.documentIdString();
        service.setDocumentId(documentId);
        service.setStoragePath(directory.filePath(QStringLiteral("server.json")));
        server.setService(&service);
        return service.open()
            && server.grant(QString::fromLatin1(ActorA), QString::fromLatin1(CredentialA), true)
            && server.grant(QString::fromLatin1(ActorB), QString::fromLatin1(CredentialB), true)
            && server.listenNetwork(QHostAddress::LocalHost);
    }

    std::unique_ptr<Peer> peer(const char *actor, const char *credential)
    {
        auto result = std::make_unique<Peer>();
        auto decoded = ReferenceDocumentCodec::decode(baseline);
        if (!decoded) return nullptr;
        result->document = std::move(decoded->document);
        result->local.setDocumentId(documentId);
        result->local.setStoragePath(
            directory.filePath(QString::fromLatin1(actor) + QStringLiteral(".json")));
        if (!result->local.open()) return nullptr;
        result->client.setDocument(result->document.get());
        result->client.setService(&result->local);
        result->client.setActorId(QString::fromLatin1(actor));
        if (!result->client.connectToService()) return nullptr;
        result->socket.setService(&result->local);
        result->socket.setActorId(QString::fromLatin1(actor));
        result->socket.setCredential(QString::fromLatin1(credential));
        result->socket.setHost(QStringLiteral("127.0.0.1"), server.networkPort());
        result->socket.setLocalClient(&result->client);
        return result;
    }

    double valueOf(const Peer &peer) const
    {
        return peer.document->value(entity, gain()).toDouble();
    }
};
}

class TstPropertySyncNetwork final : public QObject {
    Q_OBJECT
private slots:
    void channelRejectsTamperReplayGapAndWrongKey()
    {
        const QByteArray serverNonce = PropertySyncChannel::createNonce();
        const QByteArray clientNonce = PropertySyncChannel::createNonce();
        QVERIFY(PropertySyncChannel::validNonce(serverNonce));
        QVERIFY(serverNonce != clientNonce);
        PropertySyncChannel client, server, stranger;
        client.begin(PropertySyncChannel::Role::Client, QStringLiteral("shared-secret-0001"),
                     serverNonce, clientNonce);
        server.begin(PropertySyncChannel::Role::Server, QStringLiteral("shared-secret-0001"),
                     serverNonce, clientNonce);
        stranger.begin(PropertySyncChannel::Role::Server, QStringLiteral("other-secret-00002"),
                       serverNonce, clientNonce);
        const QJsonObject body{{QStringLiteral("type"), QStringLiteral("ping")}};
        const QJsonObject first = client.seal(body);
        const QJsonObject second = client.seal(body);
        QVERIFY(!stranger.open(first));                 // wrong credential
        QVERIFY(!server.open(second));                  // gap: frame 1 was dropped
        QJsonObject tampered = first;
        tampered.insert(QStringLiteral("body"), QStringLiteral("{\"type\":\"pong\"}"));
        QVERIFY(!server.open(tampered));                // altered body
        QCOMPARE(server.open(first).value_or(QJsonObject{}), body);
        QVERIFY(!server.open(first));                   // replay
        QCOMPARE(client.open(server.seal(body)).value_or(QJsonObject{}), body);
        QCOMPARE(server.open(second).value_or(QJsonObject{}), body);
        QVERIFY(!client.open(client.seal(body)));       // reflected direction

        const QByteArray proof = PropertySyncChannel::helloProof(
            QStringLiteral("shared-secret-0001"), QStringLiteral("doc"), QStringLiteral("actor"),
            serverNonce, clientNonce);
        QVERIFY(PropertySyncChannel::sameDigest(proof, PropertySyncChannel::helloProof(
            QStringLiteral("shared-secret-0001"), QStringLiteral("doc"), QStringLiteral("actor"),
            serverNonce, clientNonce)));
        QVERIFY(!PropertySyncChannel::sameDigest(proof, PropertySyncChannel::helloProof(
            QStringLiteral("shared-secret-0001"), QStringLiteral("doc"), QStringLiteral("actor"),
            PropertySyncChannel::createNonce(), clientNonce)));
    }

    void unauthenticatedNetworkPeerCannotSubmit()
    {
        Network network;
        QVERIFY(network.start());
        QTcpSocket raw;
        raw.connectToHost(QHostAddress::LocalHost, network.server.networkPort());
        QVERIFY(raw.waitForConnected(5000));
        QTRY_VERIFY_WITH_TIMEOUT(raw.canReadLine(), 5000);
        const auto challenge = QJsonDocument::fromJson(raw.readLine()).object();
        QCOMPARE(challenge.value(QStringLiteral("type")).toString(), QStringLiteral("challenge"));
        // An old-style hello that sends the credential itself is refused.
        raw.write(QJsonDocument(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("hello")},
            {QStringLiteral("actor"), QString::fromLatin1(ActorA)},
            {QStringLiteral("document"), network.documentId},
            {QStringLiteral("credential"), QString::fromLatin1(CredentialA)}})
                      .toJson(QJsonDocument::Compact) + '\n');
        QTRY_VERIFY_WITH_TIMEOUT(raw.canReadLine(), 5000);
        const auto reply = QJsonDocument::fromJson(raw.readLine()).object();
        QCOMPARE(reply.value(QStringLiteral("type")).toString(), QStringLiteral("reject"));
        QTRY_COMPARE_WITH_TIMEOUT(raw.state(), QAbstractSocket::UnconnectedState, 5000);
        QCOMPARE(network.service.operationCount(), 0);
        QCOMPARE(network.server.sessionCount(), 0);
    }

    void networkPeersConvergeAfterOfflineEdits()
    {
        Network network;
        QVERIFY(network.start());
        auto a = network.peer(ActorA, CredentialA);
        auto b = network.peer(ActorB, CredentialB);
        QVERIFY(a && b);
        QVERIFY(a->socket.connectToServer());
        QVERIFY(b->socket.connectToServer());
        QTRY_VERIFY_WITH_TIMEOUT(a->socket.isConnected() && b->socket.isConnected(), 5000);
        QVERIFY(a->socket.mayEdit());
        QCOMPARE(network.server.sessionCount(), 2);

        QVERIFY(a->client.setValue(network.entity.toString(), gain(), 1.0));
        QTRY_COMPARE_WITH_TIMEOUT(network.valueOf(*b), 1.0, 5000);

        a->socket.disconnectFromServer();
        b->socket.disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(!a->socket.isConnected() && !b->socket.isConnected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(network.server.sessionCount(), 0, 5000);
        QVERIFY(a->client.setValue(network.entity.toString(), gain(), 2.0));
        QVERIFY(a->client.setValue(network.entity.toString(), name(), QStringLiteral("offline A")));
        QVERIFY(b->client.setValue(network.entity.toString(), gain(), 3.0));
        QVERIFY(a->socket.connectToServer());
        QVERIFY(b->socket.connectToServer());
        // Equal logical time on both gain edits: actor B wins on every replica.
        QTRY_COMPARE_WITH_TIMEOUT(network.service.operationCount(), 4, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(network.valueOf(*a), 3.0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(network.valueOf(*b), 3.0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            b->document->value(network.entity, name()).toString(), QStringLiteral("offline A"),
            5000);
    }

    void permissionChangesApplyToLiveSessions()
    {
        Network network;
        QVERIFY(network.start());
        auto a = network.peer(ActorA, CredentialA);
        QVERIFY(a);
        QSignalSpy permission(&a->socket, &PropertySyncSocketClient::permissionChanged);
        QSignalSpy revoked(&a->socket, &PropertySyncSocketClient::revoked);
        QSignalSpy rejected(&a->socket, &PropertySyncSocketClient::operationRejected);
        QSignalSpy closed(&a->socket, &PropertySyncSocketClient::disconnected);
        QVERIFY(a->socket.connectToServer());
        QTRY_VERIFY_WITH_TIMEOUT(a->socket.isConnected(), 5000);

        QVERIFY(network.server.grant(QString::fromLatin1(ActorA),
                                     QString::fromLatin1(CredentialA), false));
        QTRY_VERIFY_WITH_TIMEOUT(!a->socket.mayEdit(), 5000);
        QVERIFY(a->client.isReadOnly());
        QVERIFY(!a->client.setValue(network.entity.toString(), gain(), 4.0));
        QCOMPARE(a->local.operationCount(), 0);

        QVERIFY(network.server.grant(QString::fromLatin1(ActorA),
                                     QString::fromLatin1(CredentialA), true));
        QTRY_VERIFY_WITH_TIMEOUT(a->socket.mayEdit(), 5000);
        QVERIFY(a->client.setValue(network.entity.toString(), gain(), 4.0));
        QTRY_COMPARE_WITH_TIMEOUT(network.service.operationCount(), 1, 5000);
        QVERIFY(permission.size() >= 3);

        // Rotating the credential ends the session; the old secret no longer
        // authenticates, the new one does.
        QVERIFY(network.server.grant(QString::fromLatin1(ActorA),
                                     QStringLiteral("rotated-credential-aaaa"), true));
        QTRY_COMPARE_WITH_TIMEOUT(revoked.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        QCOMPARE(network.server.sessionCount(), 0);
        QVERIFY(a->socket.connectToServer());
        QTRY_COMPARE_WITH_TIMEOUT(rejected.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 2, 5000);
        QVERIFY(!a->socket.isConnected());
        QCOMPARE(network.server.sessionCount(), 0);
        a->socket.setCredential(QStringLiteral("rotated-credential-aaaa"));
        QVERIFY(a->socket.connectToServer());
        QTRY_VERIFY_WITH_TIMEOUT(a->socket.isConnected(), 5000);

        QVERIFY(network.server.revoke(QString::fromLatin1(ActorA)));
        QTRY_COMPARE_WITH_TIMEOUT(revoked.size(), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 3, 5000);
        QVERIFY(!a->socket.mayEdit());
        QCOMPARE(network.service.operationCount(), 1);
    }

    void silentSessionExpiresAndPresenceLeaves()
    {
        Network network;
        QVERIFY(network.start());
        network.server.setIdleTimeout(800);
        auto a = network.peer(ActorA, CredentialA);
        auto b = network.peer(ActorB, CredentialB);
        QVERIFY(a && b);
        a->socket.setHeartbeatInterval(0);   // never pings: looks like a dead host
        b->socket.setHeartbeatInterval(50);
        QSignalSpy present(&b->client, &LocalPropertySyncClient::presenceChanged);
        QSignalSpy left(&b->client, &LocalPropertySyncClient::presenceLeft);
        QVERIFY(b->socket.connectToServer());
        QVERIFY(a->socket.connectToServer());
        QTRY_VERIFY_WITH_TIMEOUT(a->socket.isConnected() && b->socket.isConnected(), 5000);
        a->client.setPresence({{QStringLiteral("label"), QStringLiteral("Silent")}});
        QTRY_COMPARE_WITH_TIMEOUT(present.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(left.size(), 1, 5000);
        QCOMPARE(left.first().at(0).toString(), QString::fromLatin1(ActorA));
        QTRY_VERIFY_WITH_TIMEOUT(!a->socket.isConnected(), 5000);
        QVERIFY(b->socket.isConnected());
        QCOMPARE(network.service.operationCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TstPropertySyncNetwork)
#include "tst_property_sync_network.moc"
