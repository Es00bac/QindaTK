// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_property_sync.h"
#include "reference_document_codec.h"
#include "undo_service.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class TstLocalPropertySync final : public QObject {
    Q_OBJECT
private:
    static constexpr auto ActorA = "00000000-0000-0000-0000-00000000000a";
    static constexpr auto ActorB = "00000000-0000-0000-0000-00000000000b";
    static constexpr auto ActorC = "00000000-0000-0000-0000-00000000000c";

    struct Fixture {
        EntityId entity = EntityId::create();
        PropertyDocument base;
        QByteArray bytes;
        Fixture()
        {
            PropertyDescriptor gain;
            gain.id = QStringLiteral("gain");
            gain.type = QMetaType::fromType<double>();
            gain.defaultValue = 0.0;
            Q_ASSERT(base.defineProperty(gain));
            PropertyDescriptor name;
            name.id = QStringLiteral("name");
            name.type = QMetaType::fromType<QString>();
            name.defaultValue = QStringLiteral("base");
            Q_ASSERT(base.defineProperty(name));
            Q_ASSERT(base.addEntity(entity));
            const auto snapshot = ReferenceDocumentCodec::capture(base);
            Q_ASSERT(snapshot);
            bytes = snapshot->payload;
        }
        std::unique_ptr<PropertyDocument> clone() const
        {
            auto decoded = ReferenceDocumentCodec::decode(bytes);
            return decoded ? std::move(decoded->document) : nullptr;
        }
    };

    static void configure(LocalPropertySyncClient &client, PropertyDocument *document,
                          LocalPropertySyncService *service, const char *actor)
    {
        client.setDocument(document);
        client.setService(service);
        client.setActorId(QString::fromLatin1(actor));
    }

private slots:
    void concurrentOfflineEditsConvergeAndPersist()
    {
        Fixture fixture;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString store = directory.filePath(QStringLiteral("operations.json"));
        LocalPropertySyncService service;
        service.setDocumentId(fixture.base.documentIdString());
        service.setStoragePath(store);
        QVERIFY(service.open());

        auto first = fixture.clone();
        auto second = fixture.clone();
        QVERIFY(first && second);
        LocalPropertySyncClient a, b;
        configure(a, first.get(), &service, ActorA);
        configure(b, second.get(), &service, ActorB);
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        QSignalSpy lost(&a, &LocalPropertySyncClient::operationLost);
        QSignalSpy presence(&b, &LocalPropertySyncClient::presenceChanged);
        QSignalSpy presenceLeft(&b, &LocalPropertySyncClient::presenceLeft);
        a.setPresence({{QStringLiteral("label"), QStringLiteral("Alice")},
                       {QStringLiteral("selection"), fixture.entity.toString()}});
        QCOMPARE(presence.size(), 1);
        QCOMPARE(service.operationCount(), 0);

        QVERIFY(a.setValue(fixture.entity.toString(), QStringLiteral("gain"), 1.0));
        QCOMPARE(second->value(fixture.entity, QStringLiteral("gain")).toDouble(), 1.0);
        a.disconnectFromService();
        QCOMPARE(presenceLeft.size(), 1);
        b.disconnectFromService();
        QVERIFY(a.setValue(fixture.entity.toString(), QStringLiteral("gain"), 2.0));
        QVERIFY(a.setValue(fixture.entity.toString(), QStringLiteral("name"),
                           QStringLiteral("offline A")));
        QVERIFY(b.setValue(fixture.entity.toString(), QStringLiteral("gain"), 3.0));
        QCOMPARE(a.pendingCount(), 2);
        QCOMPARE(b.pendingCount(), 1);
        QCOMPARE(service.operationCount(), 1);

        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        QCOMPARE(a.pendingCount(), 0);
        QCOMPARE(b.pendingCount(), 0);
        QCOMPARE(service.operationCount(), 4);
        // Equal logical time on the conflicting edits: actor B wins regardless
        // of which offline queue reached the service first.
        QCOMPARE(first->value(fixture.entity, QStringLiteral("gain")).toDouble(), 3.0);
        QCOMPARE(second->value(fixture.entity, QStringLiteral("gain")).toDouble(), 3.0);
        QCOMPARE(first->value(fixture.entity, QStringLiteral("name")).toString(),
                 QStringLiteral("offline A"));
        QCOMPARE(second->value(fixture.entity, QStringLiteral("name")).toString(),
                 QStringLiteral("offline A"));
        QCOMPARE(lost.size(), 1);

        // A third actual model receives packets in reverse order, then the
        // service replays duplicates on connect. Neither changes the result.
        auto reordered = fixture.clone();
        QVERIFY(reordered);
        LocalPropertySyncClient c;
        configure(c, reordered.get(), &service, ActorC);
        for (auto it = service.operations().crbegin(); it != service.operations().crend(); ++it)
            QVERIFY(c.receive(*it));
        QVERIFY(c.receive(service.operations().first()));
        QVERIFY(c.connectToService());
        QCOMPARE(reordered->value(fixture.entity, QStringLiteral("gain")).toDouble(), 3.0);
        QCOMPARE(reordered->value(fixture.entity, QStringLiteral("name")).toString(),
                 QStringLiteral("offline A"));

        a.disconnectFromService();
        b.disconnectFromService();
        c.disconnectFromService();
        LocalPropertySyncService reopened;
        reopened.setDocumentId(fixture.base.documentIdString());
        reopened.setStoragePath(store);
        QVERIFY(reopened.open());
        QCOMPARE(reopened.operationCount(), 4);
        auto restartedDocument = fixture.clone();
        QVERIFY(restartedDocument);
        LocalPropertySyncClient restarted;
        configure(restarted, restartedDocument.get(), &reopened, ActorA);
        QVERIFY(restarted.connectToService());
        QCOMPARE(restartedDocument->value(fixture.entity, QStringLiteral("gain")).toDouble(), 3.0);
        QCOMPARE(restartedDocument->value(fixture.entity, QStringLiteral("name")).toString(),
                 QStringLiteral("offline A"));
    }

    void rejectsTypeAndExternalMutation()
    {
        Fixture fixture;
        LocalPropertySyncService service;
        service.setDocumentId(fixture.base.documentIdString());
        auto document = fixture.clone();
        QVERIFY(document);
        LocalPropertySyncClient client;
        configure(client, document.get(), &service, ActorA);
        QVERIFY(!client.connectToService());
        QVERIFY(service.open());
        QVERIFY(client.connectToService());
        QVERIFY(!client.setValue(fixture.entity.toString(), QStringLiteral("gain"),
                                 QStringLiteral("wrong type")));
        QCOMPARE(service.operationCount(), 0);
        QVERIFY(document->removeEntity(fixture.entity));
        QVERIFY(!client.setValue(fixture.entity.toString(), QStringLiteral("gain"), 2.0));
        QCOMPARE(service.operationCount(), 0);
    }

    void synchronizedValueInvalidatesLinearUndo()
    {
        Fixture fixture;
        auto document = fixture.clone();
        QVERIFY(document);
        UndoService undo;
        QVERIFY(undo.addDocument(document.get()));
        auto step = std::make_shared<const PropertyCommand>(QVector<PropertyChange>{
            {fixture.entity, QStringLiteral("gain"), 0.0, 1.0}});
        QVERIFY(!undo.commit(document.get(), QStringLiteral("local edit"), {}, step));
        QVERIFY(undo.canUndo(document.get()));
        QVERIFY(!document->applySynchronizedValue(fixture.entity, QStringLiteral("gain"), 2.0));
        QCOMPARE(document->value(fixture.entity, QStringLiteral("gain")).toDouble(), 2.0);
        QVERIFY(!undo.canUndo(document.get()));
        QVERIFY(!undo.isClean(document.get()));
    }
};

QTEST_GUILESS_MAIN(TstLocalPropertySync)
#include "tst_local_property_sync.moc"
