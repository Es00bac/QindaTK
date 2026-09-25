// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-082 recovery over TCP: an offline edit the hub refuses for good is
// discarded so the replica converges (D-205); peers adopt the hub's
// compacted base live and after being offline (D-204); a session closed by
// a tampered, dropped or replayed frame reconnects and converges.
#include "sync_network_fixture.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace QindaTK;
using namespace sync_test;

namespace {
// A TCP relay between one peer and the hub that can alter, drop or replay
// the next sealed frame the peer sends (one frame per line).
struct Proxy : QObject {
    enum Fault { None, Tamper, Drop, Replay };
    QTcpServer server;
    QPointer<QTcpSocket> inner;
    QPointer<QTcpSocket> outer;
    QByteArray buffer;
    QByteArray lastSealed;
    quint16 hub = 0;
    Fault fault = None;
    int faults = 0;

    bool start(quint16 hubPort)
    {
        hub = hubPort;
        connect(&server, &QTcpServer::newConnection, this, [this] {
            if (inner) inner->abort();
            if (outer) outer->abort();
            outer = server.nextPendingConnection();
            inner = new QTcpSocket(this);
            buffer.clear();
            lastSealed.clear();
            connect(inner, &QTcpSocket::readyRead, this, [this] {
                if (outer) outer->write(inner->readAll());
            });
            connect(outer, &QTcpSocket::readyRead, this, &Proxy::forward);
            connect(inner, &QTcpSocket::disconnected, this, [this] {
                if (outer) outer->disconnectFromHost();
            });
            connect(outer, &QTcpSocket::disconnected, this, [this] {
                if (inner) inner->disconnectFromHost();
            });
            inner->connectToHost(QHostAddress::LocalHost, hub);
            inner->waitForConnected(5000);
        });
        return server.listen(QHostAddress::LocalHost);
    }
    quint16 port() const { return server.serverPort(); }

    void forward()
    {
        buffer.append(outer->readAll());
        for (qsizetype end = buffer.indexOf('\n'); end >= 0; end = buffer.indexOf('\n')) {
            QByteArray line = buffer.left(end + 1);
            buffer.remove(0, end + 1);
            const bool sealed = line.contains("\"mac\"");
            if (sealed && fault != None) {
                const Fault now = fault;
                fault = None;
                ++faults;
                if (now == Drop) continue;
                if (now == Tamper) {
                    QJsonObject frame = QJsonDocument::fromJson(line).object();
                    frame.insert(QStringLiteral("body"),
                                 frame.value(QStringLiteral("body")).toString() + QLatin1Char(' '));
                    line = QJsonDocument(frame).toJson(QJsonDocument::Compact) + '\n';
                }
                if (now == Replay && !lastSealed.isEmpty()) {
                    inner->write(line);
                    line = lastSealed;
                }
            }
            if (sealed) lastSealed = line;
            inner->write(line);
        }
    }
};
}

class TstPropertySyncRecovery final : public QObject {
    Q_OBJECT
private slots:
    void offlineEditRefusedAfterDowngradeIsDiscarded()
    {
        Network network;
        QVERIFY(network.start());
        auto a = network.peer(ActorA, CredentialA);
        auto b = network.peer(ActorB, CredentialB);
        QVERIFY(a && b);
        QVERIFY(a->socket.connectToServer());
        QVERIFY(b->socket.connectToServer());
        QTRY_VERIFY_WITH_TIMEOUT(a->socket.isConnected() && b->socket.isConnected(), 5000);
        QVERIFY(b->client.setValue(network.entity.toString(), gain(), 1.0));
        QTRY_COMPARE_WITH_TIMEOUT(network.valueOf(*a), 1.0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(b->local.outboxCount(), 0, 5000);

        // A edits offline; meanwhile the host makes A a viewer.
        a->socket.disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(!a->socket.isConnected(), 5000);
        QVERIFY(a->client.setValue(network.entity.toString(), gain(), 7.0));
        QVERIFY(a->client.setValue(network.entity.toString(), name(), QStringLiteral("A offline")));
        QCOMPARE(a->local.outboxCount(), 2);
        QVERIFY(network.server.grant(QString::fromLatin1(ActorA),
                                     QString::fromLatin1(CredentialA), false));

        QSignalSpy discarded(&a->socket, &PropertySyncSocketClient::operationDiscarded);
        QSignalSpy rejected(&a->socket, &PropertySyncSocketClient::operationRejected);
        QVERIFY(a->socket.connectToServer());
        QTRY_COMPARE_WITH_TIMEOUT(discarded.size(), 2, 5000);
        // The refused edits leave A's replica and store; A shows the hub again.
        QCOMPARE(a->local.outboxCount(), 0);
        QCOMPARE(network.valueOf(*a), 1.0);
        QCOMPARE(a->document->value(network.entity, name()).toString(), QStringLiteral("base"));
        QCOMPARE(dump(*a->document), dump(*b->document));
        QCOMPARE(network.service.operationCount(), 1);

        // Nothing is resent on the next reconnect.
        a->socket.disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(!a->socket.isConnected(), 5000);
        const qsizetype before = rejected.size();
        QVERIFY(a->socket.connectToServer());
        QTRY_VERIFY_WITH_TIMEOUT(a->socket.isConnected(), 5000);
        QVERIFY(b->client.setValue(network.entity.toString(), gain(), 2.0));
        QTRY_COMPARE_WITH_TIMEOUT(network.valueOf(*a), 2.0, 5000);
        QCOMPARE(rejected.size(), before);
    }

    void peersAdoptTheHubBaseLiveAndAfterBeingOffline()
    {
        Network network;
        network.service.setCompactAt(16);
        QVERIFY(network.start());
        auto a = network.peer(ActorA, CredentialA);
        auto b = network.peer(ActorB, CredentialB);
        QVERIFY(a && b);
        QVERIFY(a->socket.connectToServer());
        QTRY_VERIFY_WITH_TIMEOUT(a->socket.isConnected(), 5000);

        // B is offline with its own edits while A's edits fold on the hub.
        QVERIFY(b->client.setValue(network.entity.toString(), name(), QStringLiteral("from B")));
        for (int i = 1; i <= 40; ++i) {
            QVERIFY2(a->client.setValue(network.entity.toString(), gain(), double(i)),
                     qPrintable(a->client.lastError()));
            QTRY_COMPARE_WITH_TIMEOUT(a->local.outboxCount(), 0, 5000);
        }
        QVERIFY(!network.service.baseId().isEmpty());
        QVERIFY(network.service.operationCount() < 16);
        // A adopted each new base while connected: its store stays small.
        QTRY_COMPARE_WITH_TIMEOUT(a->local.baseId(), network.service.baseId(), 5000);
        QVERIFY(a->local.operationCount() < 16);
        QCOMPARE(network.valueOf(*a), 40.0);

        QVERIFY(b->socket.connectToServer());
        QTRY_VERIFY_WITH_TIMEOUT(b->socket.isConnected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(b->local.outboxCount(), 0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            a->document->value(network.entity, name()).toString(), QStringLiteral("from B"),
            5000);
        QCOMPARE(b->local.baseId(), network.service.baseId());
        QCOMPARE(network.valueOf(*b), 40.0);
        QCOMPARE(dump(*a->document), dump(*b->document));
    }

    void tamperedDroppedOrReplayedFrameEndsSessionThenReconnectConverges()
    {
        Network network;
        QVERIFY(network.start());
        Proxy proxy;
        QVERIFY(proxy.start(network.server.networkPort()));
        auto a = network.peer(ActorA, CredentialA);
        auto b = network.peer(ActorB, CredentialB);
        QVERIFY(a && b);
        a->socket.setHost(QStringLiteral("127.0.0.1"), proxy.port());
        QSignalSpy closed(&a->socket, &PropertySyncSocketClient::disconnected);
        QVERIFY(b->socket.connectToServer());
        const auto reconnect = [&] {
            QVERIFY(a->socket.connectToServer());
            QTRY_VERIFY_WITH_TIMEOUT(a->socket.isConnected(), 5000);
        };
        reconnect();
        QTRY_VERIFY_WITH_TIMEOUT(b->socket.isConnected(), 5000);
        const QString entity = network.entity.toString();

        // Each fault on A's stream to the hub ends the session; the edit
        // stays in A's outbox, and the next session delivers it.
        int value = 0;
        for (const auto fault : {Proxy::Tamper, Proxy::Drop, Proxy::Replay}) {
            const qsizetype sessions = closed.size();
            proxy.fault = fault;
            QVERIFY(a->client.setValue(entity, gain(), double(++value)));
            if (fault == Proxy::Drop)   // the gap shows on the next frame
                QVERIFY(a->client.setValue(entity, name(), QStringLiteral("after gap")));
            QTRY_VERIFY_WITH_TIMEOUT(closed.size() > sessions, 5000);
            QVERIFY(a->local.outboxCount() > 0 || fault == Proxy::Replay);
            // Edits made while cut off converge once A is back.
            QVERIFY(b->client.setValue(entity, name(), QStringLiteral("B %1").arg(value)));
            proxy.fault = Proxy::None;
            reconnect();
            QTRY_COMPARE_WITH_TIMEOUT(a->local.outboxCount(), 0, 5000);
            QTRY_COMPARE_WITH_TIMEOUT(network.valueOf(*b), double(value), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(dump(*a->document), dump(*b->document), 5000);
        }
        QCOMPARE(proxy.faults, 3);
    }
};

QTEST_GUILESS_MAIN(TstPropertySyncRecovery)
#include "tst_property_sync_recovery.moc"
