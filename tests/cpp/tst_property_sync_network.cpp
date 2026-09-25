// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-082 local-network transport: authenticated sealed frames, TCP
// convergence after offline edits, live permission changes, credential
// rotation and revocation, idle-session expiry, and presence between the
// hub's own replicas and its peers.
#include "sync_network_fixture.h"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;
using namespace sync_test;

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

    void hubPresenceReachesPeersBothWays()
    {
        Network network;
        QVERIFY(network.start());
        // The hosting user edits through a replica attached to the hub's own
        // service, not through a socket bridge.
        auto hosted = network.replica();
        QVERIFY(hosted);
        LocalPropertySyncClient host;
        host.setDocument(hosted.get());
        host.setService(&network.service);
        host.setActorId(QString::fromLatin1(ActorC));
        QVERIFY(host.connectToService());
        host.setPresence({{QStringLiteral("label"), QStringLiteral("Host")}});
        QSignalSpy hostSees(&host, &LocalPropertySyncClient::presenceChanged);
        QSignalSpy hostLeft(&host, &LocalPropertySyncClient::presenceLeft);

        auto a = network.peer(ActorA, CredentialA);
        QVERIFY(a);
        QSignalSpy peerSees(&a->client, &LocalPropertySyncClient::presenceChanged);
        QSignalSpy peerLeft(&a->client, &LocalPropertySyncClient::presenceLeft);
        QVERIFY(a->socket.connectToServer());
        QTRY_VERIFY_WITH_TIMEOUT(a->socket.isConnected(), 5000);
        // Snapshot on connect, then live updates in both directions.
        QTRY_COMPARE_WITH_TIMEOUT(peerSees.size(), 1, 5000);
        QCOMPARE(peerSees.first().at(0).toString(), QString::fromLatin1(ActorC));
        QCOMPARE(peerSees.first().at(1).toMap().value(QStringLiteral("label")).toString(),
                 QStringLiteral("Host"));
        a->client.setPresence({{QStringLiteral("label"), QStringLiteral("Peer")}});
        QTRY_COMPARE_WITH_TIMEOUT(hostSees.size(), 1, 5000);
        QCOMPARE(hostSees.first().at(0).toString(), QString::fromLatin1(ActorA));
        host.setPresence({{QStringLiteral("label"), QStringLiteral("Host moved")}});
        QTRY_COMPARE_WITH_TIMEOUT(peerSees.size(), 2, 5000);

        // A hub replica that attaches later still sees the peer.
        auto lateDocument = network.replica();
        QVERIFY(lateDocument);
        LocalPropertySyncClient late;
        late.setDocument(lateDocument.get());
        late.setService(&network.service);
        late.setActorId(QString::fromLatin1(ActorB));
        QSignalSpy lateSees(&late, &LocalPropertySyncClient::presenceChanged);
        QVERIFY(late.connectToService());
        QStringList actors;
        for (const auto &row : lateSees) actors.append(row.at(0).toString());
        QVERIFY(actors.contains(QString::fromLatin1(ActorA)));
        QVERIFY(actors.contains(QString::fromLatin1(ActorC)));

        // Leaves travel both ways as well; presence never entered the log.
        a->socket.disconnectFromServer();
        QTRY_COMPARE_WITH_TIMEOUT(hostLeft.size(), 1, 5000);
        QCOMPARE(hostLeft.first().at(0).toString(), QString::fromLatin1(ActorA));
        QVERIFY(a->socket.connectToServer());
        QTRY_VERIFY_WITH_TIMEOUT(a->socket.isConnected(), 5000);
        const qsizetype before = peerLeft.size();
        host.disconnectFromService();
        QTRY_VERIFY_WITH_TIMEOUT(peerLeft.size() > before, 5000);
        QCOMPARE(peerLeft.last().at(0).toString(), QString::fromLatin1(ActorC));
        QCOMPARE(network.service.operationCount(), 0);
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
