// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-082 log compaction (D-204): an authoritative log folds into a state
// snapshot, so a document outlives the 4,096-operation packet cap; replicas
// that were offline, that join late, or that reopen the store converge on
// the same document, and redelivered folded operations change nothing.
// Connected edits the log refuses are taken back instead of staying visible
// only in memory.
#include "local_property_sync.h"
#include "reference_document_codec.h"
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
constexpr auto ActorA = "00000000-0000-0000-0000-00000000000a";
constexpr auto ActorB = "00000000-0000-0000-0000-00000000000b";
constexpr auto ActorC = "00000000-0000-0000-0000-00000000000c";

QString gain() { return QStringLiteral("gain"); }
QString name() { return QStringLiteral("name"); }
QString body() { return QStringLiteral("body"); }

PropertyDescriptor describe(const QString &id, QMetaType type, const QVariant &defaultValue)
{
    PropertyDescriptor descriptor;
    descriptor.id = id;
    descriptor.type = type;
    descriptor.defaultValue = defaultValue;
    return descriptor;
}

struct Fixture {
    EntityId entity = EntityId::create();
    QString documentId;
    QByteArray bytes;
    Fixture()
    {
        PropertyDocument base;
        const bool defined =
            base.defineProperty(describe(gain(), QMetaType::fromType<double>(), 0.0))
            && base.defineProperty(describe(name(), QMetaType::fromType<QString>(),
                                            QStringLiteral("base")))
            && base.defineProperty(describe(body(), QMetaType::fromType<QString>(), QString()))
            && base.addEntity(entity, {{body(), QStringLiteral("hello")}});
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        if (defined && snapshot) bytes = snapshot->payload;
        documentId = base.documentIdString();
    }
    std::unique_ptr<PropertyDocument> clone() const
    {
        auto decoded = ReferenceDocumentCodec::decode(bytes);
        return decoded ? std::move(decoded->document) : nullptr;
    }
    QString id() const { return entity.toString(); }
};

void configure(LocalPropertySyncClient &client, PropertyDocument *document,
               LocalPropertySyncService *service, const char *actor)
{
    client.setDocument(document);
    client.setService(service);
    client.setActorId(QString::fromLatin1(actor));
    client.setTextProperties({body()});
}

QStringList dump(const PropertyDocument &document)
{
    QStringList rows;
    for (const auto &change : document.committedValues())
        rows.append(change.target.toString() + QLatin1Char('/') + change.propertyId
                    + QLatin1Char('=') + change.after.toString());
    return rows;
}
}

class TstCollaborationCompaction final : public QObject {
    Q_OBJECT
private slots:
    void documentOutlivesTheOperationCap()
    {
        // In memory: 4,400 synced store commits would only measure the disk.
        Fixture fixture;
        LocalPropertySyncService service;
        service.setDocumentId(fixture.documentId);
        service.setAuthoritative(true);
        service.setCompactAt(64);
        QVERIFY(service.open());
        auto first = fixture.clone();
        auto second = fixture.clone();
        QVERIFY(first && second);
        LocalPropertySyncClient a, b;
        configure(a, first.get(), &service, ActorA);
        configure(b, second.get(), &service, ActorB);
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        const QString x = a.createEntity({{gain(), 1.0}});
        QVERIFY(!x.isEmpty());
        QVERIFY(a.replaceText(fixture.id(), body(), 5, 0, QStringLiteral(" you")));
        const QString gone = b.createEntity({{gain(), 2.0}});
        QVERIFY(!gone.isEmpty());
        QVERIFY(a.deleteEntity(gone));
        const QVector<PropertySyncOperation> early = service.operations();

        // Far more edits than one log may hold (4,096 packets): the log
        // compacts and every edit is still accepted and shared.
        for (int i = 0; i < 4400; ++i) {
            LocalPropertySyncClient &author = i % 2 ? b : a;
            QVERIFY2(author.setValue(x, gain(), double(i)), qPrintable(author.lastError()));
            if (i % 400 == 0)
                QVERIFY(author.replaceText(fixture.id(), body(), 0, 1, QStringLiteral("H")));
        }
        QVERIFY(!service.baseId().isEmpty());
        QVERIFY(service.operationCount() <= 64);
        QCOMPARE(first->value(EntityId::parse(x), gain()).toDouble(), 4399.0);
        QCOMPARE(dump(*first), dump(*second));

        // B goes offline, the log compacts twice more, B's offline edits
        // still land and win where they are newer.
        const QString before = service.baseId();
        b.disconnectFromService();
        QVERIFY(b.setValue(fixture.id(), name(), QStringLiteral("offline B")));
        QVERIFY(b.replaceText(fixture.id(), body(), 5, 0, QStringLiteral("!")));
        for (int i = 0; i < 150; ++i) QVERIFY(a.setValue(x, gain(), 10000.0 + i));
        QVERIFY(service.baseId() != before);
        QVERIFY(b.connectToService());
        QCOMPARE(b.pendingCount(), 0);
        QCOMPARE(second->value(fixture.entity, name()).toString(), QStringLiteral("offline B"));
        QCOMPARE(first->value(fixture.entity, body()).toString(), QStringLiteral("Hello! you"));
        QCOMPARE(dump(*first), dump(*second));

        // A late replica starts from the base; redelivered folded operations
        // (a relay that missed the compaction) change nothing.
        auto third = fixture.clone();
        QVERIFY(third);
        LocalPropertySyncClient c;
        configure(c, third.get(), &service, ActorC);
        QVERIFY2(c.connectToService(), qPrintable(c.lastError()));
        QCOMPARE(dump(*third), dump(*first));
        for (const auto &operation : early)
            QVERIFY2(c.receive(operation), qPrintable(c.lastError()));
        QCOMPARE(dump(*third), dump(*first));

        // Undo still reverses A's own last step after the fold.
        QVERIFY(a.undo());
        QCOMPARE(third->value(EntityId::parse(x), gain()).toDouble(), 10148.0);
        QVERIFY(a.redo());
        QCOMPARE(dump(*third), dump(*first));
    }

    void compactedStoreReopensAsBaseAndTail()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Fixture fixture;
        const QString store = directory.filePath(QStringLiteral("hub.json"));
        LocalPropertySyncService service;
        service.setDocumentId(fixture.documentId);
        service.setStoragePath(store);
        service.setAuthoritative(true);
        service.setCompactAt(16);
        QVERIFY(service.open());
        auto first = fixture.clone();
        QVERIFY(first);
        LocalPropertySyncClient a;
        configure(a, first.get(), &service, ActorA);
        QVERIFY(a.connectToService());
        const QString x = a.createEntity({{gain(), 1.0}});
        QVERIFY(!x.isEmpty());
        QVERIFY(a.deleteEntity(fixture.id()));
        for (int i = 0; i < 40; ++i) QVERIFY(a.setValue(x, gain(), double(i)));
        QVERIFY(a.replaceText(x, body(), 0, 0, QStringLiteral("kept")));
        QVERIFY(!service.baseId().isEmpty());
        QVERIFY(QFile::exists(store + QStringLiteral(".base.") + service.baseId()));
        a.disconnectFromService();

        // The store reopens as base + tail on a fresh process.
        LocalPropertySyncService reopened;
        reopened.setDocumentId(fixture.documentId);
        reopened.setStoragePath(store);
        reopened.setAuthoritative(true);
        QVERIFY2(reopened.open(), qPrintable(reopened.lastError()));
        QCOMPARE(reopened.baseId(), service.baseId());
        auto fourth = fixture.clone();
        QVERIFY(fourth);
        LocalPropertySyncClient d;
        configure(d, fourth.get(), &reopened, ActorC);
        QVERIFY2(d.connectToService(), qPrintable(d.lastError()));
        QCOMPARE(dump(*fourth), dump(*first));
    }

    void headlessHubCompactsFromItsBaseline()
    {
        Fixture fixture;
        LocalPropertySyncService hub;
        hub.setDocumentId(fixture.documentId);
        hub.setAuthoritative(true);
        hub.setCompactAt(16);
        QVERIFY(hub.open());
        // Operations arrive from peers only: no replica is attached here.
        auto source = fixture.clone();
        QVERIFY(source);
        LocalPropertySyncService peerStore;
        peerStore.setDocumentId(fixture.documentId);
        QVERIFY(peerStore.open());
        LocalPropertySyncClient a;
        configure(a, source.get(), &peerStore, ActorA);
        QVERIFY(a.connectToService());
        for (int i = 0; i < 20; ++i) QVERIFY(a.setValue(fixture.id(), gain(), double(i)));
        QVERIFY(a.replaceText(fixture.id(), body(), 0, 5, QStringLiteral("world")));
        const auto log = peerStore.operations();
        QCOMPARE(log.size(), 22);   // 20 values, then a text delete + insert
        for (int i = 0; i < 15; ++i) QVERIFY(hub.submit(log.at(i)));
        QVERIFY(!hub.compact());   // nothing to fold with yet
        hub.setCompactionBaseline(fixture.bytes, {body()});
        for (int i = 15; i < log.size(); ++i) QVERIFY(hub.submit(log.at(i)));
        QVERIFY(!hub.baseId().isEmpty());
        QVERIFY(hub.operationCount() < 16);
        auto late = fixture.clone();
        QVERIFY(late);
        LocalPropertySyncClient b;
        configure(b, late.get(), &hub, ActorB);
        QVERIFY2(b.connectToService(), qPrintable(b.lastError()));
        QCOMPARE(dump(*late), dump(*source));
    }

    void refusedConnectedEditIsTakenBack()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Fixture fixture;
        LocalPropertySyncService service;
        service.setDocumentId(fixture.documentId);
        service.setStoragePath(directory.filePath(QStringLiteral("store.json")));
        QVERIFY(service.open());
        auto first = fixture.clone();
        QVERIFY(first);
        LocalPropertySyncClient a;
        configure(a, first.get(), &service, ActorA);
        QVERIFY(a.connectToService());
        QVERIFY(a.setValue(fixture.id(), gain(), 1.0));
        // The store becomes unwritable: the edit cannot be made durable, so
        // it must not stay applied (it would exist in memory only).
        QVERIFY(QFile::setPermissions(directory.path(), QFileDevice::ReadOwner
                                                           | QFileDevice::ExeOwner));
        const bool accepted = a.setValue(fixture.id(), gain(), 2.0);
        QVERIFY(QFile::setPermissions(directory.path(), QFileDevice::ReadOwner
                                          | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QVERIFY(!accepted);
        QCOMPARE(first->value(fixture.entity, gain()).toDouble(), 1.0);
        QCOMPARE(a.pendingCount(), 0);
        QCOMPARE(service.operationCount(), 1);
        QVERIFY(a.setValue(fixture.id(), gain(), 3.0));
        QCOMPARE(service.operationCount(), 2);
    }
};

QTEST_GUILESS_MAIN(TstCollaborationCompaction)
#include "tst_collaboration_compaction.moc"
