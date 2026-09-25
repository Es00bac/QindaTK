// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-082 domain operations: entity create/delete with observed-remove,
// plain-text merge, collaborative undo, and a deterministic network-fault
// fixture (reordered, duplicated and dropped packets, then replay).
#include "local_property_sync.h"
#include "reference_document_codec.h"
#include <QSignalSpy>
#include <QtTest>
#include <algorithm>
#include <random>

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
    PropertyDocument base;
    QByteArray bytes;
    Fixture()
    {
        const auto gainDescriptor = describe(gain(), QMetaType::fromType<double>(), 0.0);
        const auto nameDescriptor = describe(name(), QMetaType::fromType<QString>(),
                                             QStringLiteral("base"));
        const auto bodyDescriptor = describe(body(), QMetaType::fromType<QString>(), QString());
        const bool defined = base.defineProperty(gainDescriptor)
            && base.defineProperty(nameDescriptor) && base.defineProperty(bodyDescriptor)
            && base.addEntity(entity, {{body(), QStringLiteral("hello")}});
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        if (defined && snapshot) bytes = snapshot->payload;
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

// Every committed value as "entity/property=value", in the document's
// sorted order: equal dumps mean equal entities and values.
QStringList dump(const PropertyDocument &document)
{
    QStringList rows;
    for (const auto &change : document.committedValues())
        rows.append(change.target.toString() + QLatin1Char('/') + change.propertyId
                    + QLatin1Char('=') + change.after.toString());
    return rows;
}

QString text(const PropertyDocument &document, const Fixture &fixture)
{
    return document.value(fixture.entity, body()).toString();
}
}

class TstCollaborationDomain final : public QObject {
    Q_OBJECT
private slots:
    void unobservedEditRevivesConcurrentlyDeletedObject()
    {
        Fixture fixture;
        LocalPropertySyncService service;
        service.setDocumentId(fixture.base.documentIdString());
        QVERIFY(service.open());
        auto first = fixture.clone();
        auto second = fixture.clone();
        QVERIFY(first && second);
        LocalPropertySyncClient a, b;
        configure(a, first.get(), &service, ActorA);
        configure(b, second.get(), &service, ActorB);
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());

        const QString created = a.createEntity({{gain(), 1.0}});
        QVERIFY2(!created.isEmpty(), qPrintable(a.lastError()));
        const EntityId x = EntityId::parse(created);
        QVERIFY(second->contains(x));
        QCOMPARE(second->value(x, gain()).toDouble(), 1.0);
        QCOMPARE(second->value(x, name()).toString(), QStringLiteral("base"));

        // A deletes while B, offline and unaware, edits the same object.
        a.disconnectFromService();
        b.disconnectFromService();
        QVERIFY(a.deleteEntity(created));
        QVERIFY(!first->contains(x));
        QVERIFY(b.setValue(created, gain(), 5.0));
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        // The committed edit is not erased: the object returns on both.
        QVERIFY(first->contains(x));
        QVERIFY(second->contains(x));
        QCOMPARE(first->value(x, gain()).toDouble(), 5.0);
        QCOMPARE(second->value(x, gain()).toDouble(), 5.0);

        // A delete that has observed every edit removes the object everywhere,
        // including a baseline object.
        QVERIFY(b.deleteEntity(created));
        QVERIFY(!first->contains(x));
        QVERIFY(!second->contains(x));
        QVERIFY(a.deleteEntity(fixture.id()));
        QVERIFY(!second->contains(fixture.entity));
        QCOMPARE(dump(*first), dump(*second));
        QVERIFY(!a.setValue(fixture.id(), gain(), 2.0));
    }

    void concurrentTextEditsMerge()
    {
        Fixture fixture;
        LocalPropertySyncService service;
        service.setDocumentId(fixture.base.documentIdString());
        QVERIFY(service.open());
        auto first = fixture.clone();
        auto second = fixture.clone();
        QVERIFY(first && second);
        LocalPropertySyncClient a, b;
        configure(a, first.get(), &service, ActorA);
        configure(b, second.get(), &service, ActorB);

        QVERIFY(a.replaceText(fixture.id(), body(), 5, 0, QStringLiteral(" world")));
        QCOMPARE(text(*first, fixture), QStringLiteral("hello world"));
        QVERIFY(b.replaceText(fixture.id(), body(), 0, 1, QStringLiteral("J")));
        QCOMPARE(text(*second, fixture), QStringLiteral("Jello"));
        QVERIFY(!b.setValue(fixture.id(), body(), QStringLiteral("whole value")));
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        QCOMPARE(text(*first, fixture), QStringLiteral("Jello world"));
        QCOMPARE(text(*second, fixture), QStringLiteral("Jello world"));

        // Equal-time inserts at one position order by actor, on every replica.
        a.disconnectFromService();
        b.disconnectFromService();
        QVERIFY(a.replaceText(fixture.id(), body(), 11, 0, QStringLiteral("!A")));
        QVERIFY(b.replaceText(fixture.id(), body(), 11, 0, QStringLiteral("!B")));
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        QCOMPARE(text(*first, fixture), QStringLiteral("Jello world!B!A"));
        QCOMPARE(text(*second, fixture), QStringLiteral("Jello world!B!A"));
        QVERIFY(!a.replaceText(fixture.id(), body(), 14, 2, QStringLiteral("x")));
    }

    void undoReversesOnlyOwnUnsupersededSteps()
    {
        Fixture fixture;
        LocalPropertySyncService service;
        service.setDocumentId(fixture.base.documentIdString());
        QVERIFY(service.open());
        auto first = fixture.clone();
        auto second = fixture.clone();
        QVERIFY(first && second);
        LocalPropertySyncClient a, b;
        configure(a, first.get(), &service, ActorA);
        configure(b, second.get(), &service, ActorB);
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());

        QVERIFY(a.setValue(fixture.id(), gain(), 1.0));
        QVERIFY(b.setValue(fixture.id(), name(), QStringLiteral("bee")));
        QVERIFY(a.canUndo());
        QVERIFY(a.undo());
        QCOMPARE(second->value(fixture.entity, gain()).toDouble(), 0.0);
        QCOMPARE(first->value(fixture.entity, name()).toString(), QStringLiteral("bee"));
        QCOMPARE(second->value(fixture.entity, name()).toString(), QStringLiteral("bee"));
        QVERIFY(a.canRedo());
        QVERIFY(a.redo());
        QCOMPARE(second->value(fixture.entity, gain()).toDouble(), 1.0);

        // A newer remote edit supersedes A's step: undo refuses to erase it.
        QVERIFY(a.setValue(fixture.id(), gain(), 2.0));
        QVERIFY(b.setValue(fixture.id(), gain(), 3.0));
        QSignalSpy conflict(&a, &LocalPropertySyncClient::undoConflict);
        QVERIFY(!a.undo());
        QCOMPARE(conflict.size(), 1);
        QCOMPARE(first->value(fixture.entity, gain()).toDouble(), 3.0);
        QCOMPARE(second->value(fixture.entity, gain()).toDouble(), 3.0);

        QVERIFY(a.replaceText(fixture.id(), body(), 0, 5, QStringLiteral("howdy")));
        QCOMPARE(text(*second, fixture), QStringLiteral("howdy"));
        QVERIFY(a.undo());
        QCOMPARE(text(*first, fixture), QStringLiteral("hello"));
        QCOMPARE(text(*second, fixture), QStringLiteral("hello"));
        QVERIFY(a.redo());
        QCOMPARE(text(*second, fixture), QStringLiteral("howdy"));

        const QString created = a.createEntity();
        QVERIFY(!created.isEmpty());
        const EntityId x = EntityId::parse(created);
        QVERIFY(second->contains(x));
        QVERIFY(a.undo());
        QVERIFY(!second->contains(x));
        QVERIFY(a.redo());
        QVERIFY(second->contains(x));
        QCOMPARE(dump(*first), dump(*second));
    }

    void undoOfCreateKeepsCollaboratorEdit()
    {
        Fixture fixture;
        LocalPropertySyncService service;
        service.setDocumentId(fixture.base.documentIdString());
        QVERIFY(service.open());
        auto first = fixture.clone();
        auto second = fixture.clone();
        QVERIFY(first && second);
        LocalPropertySyncClient a, b;
        configure(a, first.get(), &service, ActorA);
        configure(b, second.get(), &service, ActorB);
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());

        // A creates, B names the object, A undoes: B's committed edit stays.
        const QString created = a.createEntity();
        QVERIFY(!created.isEmpty());
        const EntityId x = EntityId::parse(created);
        QVERIFY(b.setValue(created, name(), QStringLiteral("named by B")));
        QSignalSpy conflict(&a, &LocalPropertySyncClient::undoConflict);
        QVERIFY(!a.undo());
        QCOMPARE(conflict.size(), 1);
        QVERIFY(first->contains(x));
        QVERIFY(second->contains(x));
        QCOMPARE(first->value(x, name()).toString(), QStringLiteral("named by B"));

        // A text edit by another actor also protects the object.
        const QString texted = a.createEntity();
        QVERIFY(b.replaceText(texted, body(), 0, 0, QStringLiteral("B wrote")));
        QVERIFY(!a.undo());
        QVERIFY(second->contains(EntityId::parse(texted)));

        // Only A's own edits on it: the undo still deletes everywhere.
        const QString own = a.createEntity({{gain(), 3.0}});
        QVERIFY(a.setValue(own, name(), QStringLiteral("mine")));
        QVERIFY(a.undo());
        QVERIFY(a.undo());
        QVERIFY(!second->contains(EntityId::parse(own)));
        QCOMPARE(dump(*first), dump(*second));
    }

    void failedTextInsertLeavesNoUnsentDelete()
    {
        // The insert would pass 65,536 elements (deleted units stay as
        // tombstones), so the whole replacement fails and nothing is applied.
        PropertyDocument base;
        QVERIFY(base.defineProperty(describe(body(), QMetaType::fromType<QString>(), QString())));
        const EntityId entity = EntityId::create();
        const QString full(65534, QLatin1Char('a'));
        QVERIFY(base.addEntity(entity, {{body(), full}}));
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        QVERIFY(snapshot);
        auto first = ReferenceDocumentCodec::decode(snapshot->payload);
        auto second = ReferenceDocumentCodec::decode(snapshot->payload);
        QVERIFY(first && second);
        LocalPropertySyncService service;
        service.setDocumentId(base.documentIdString());
        QVERIFY(service.open());
        LocalPropertySyncClient a, b;
        configure(a, first->document.get(), &service, ActorA);
        configure(b, second->document.get(), &service, ActorB);
        QVERIFY(!a.replaceText(entity.toString(), body(), 0, 2, QStringLiteral("xyz")));
        QCOMPARE(first->document->value(entity, body()).toString(), full);
        QCOMPARE(a.pendingCount(), 0);
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        QCOMPARE(dump(*first->document), dump(*second->document));
        QVERIFY(a.replaceText(entity.toString(), body(), 0, 2, QStringLiteral("x")));
        QCOMPARE(second->document->value(entity, body()).toString().size(), 65533);
    }

    void remoteSelectionFollowsEditedText()
    {
        Fixture fixture;
        LocalPropertySyncService service;
        service.setDocumentId(fixture.base.documentIdString());
        QVERIFY(service.open());
        auto first = fixture.clone();
        auto second = fixture.clone();
        QVERIFY(first && second);
        LocalPropertySyncClient a, b;
        configure(a, first.get(), &service, ActorA);
        configure(b, second.get(), &service, ActorB);
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        const QString id = fixture.id();
        QVERIFY(a.replaceText(id, body(), 5, 0, QStringLiteral(" world")));

        // A shares its selection of "world" as stable text anchors.
        QSignalSpy seen(&b, &LocalPropertySyncClient::presenceChanged);
        a.setPresence({{QStringLiteral("selection"),
                        QVariantMap{{QStringLiteral("start"), a.textAnchor(id, body(), 6)},
                                    {QStringLiteral("end"), a.textAnchor(id, body(), 11)}}}});
        QCOMPARE(seen.size(), 1);
        const QVariantMap selection =
            seen.first().at(1).toMap().value(QStringLiteral("selection")).toMap();
        const QString start = selection.value(QStringLiteral("start")).toString();
        const QString end = selection.value(QStringLiteral("end")).toString();
        const auto selected = [&](LocalPropertySyncClient &client, PropertyDocument &document) {
            const int from = client.textPosition(id, body(), start);
            const int to = client.textPosition(id, body(), end);
            return text(document, fixture).mid(from, to - from);
        };
        QCOMPARE(selected(b, *second), QStringLiteral("world"));

        // Concurrent edits before and inside the selection: it follows them.
        a.disconnectFromService();
        b.disconnectFromService();
        QVERIFY(b.replaceText(id, body(), 0, 0, QStringLiteral("Oh, ")));
        QVERIFY(a.replaceText(id, body(), 6, 1, QStringLiteral("W")));
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        QCOMPARE(text(*second, fixture), QStringLiteral("Oh, hello World"));
        QCOMPARE(selected(b, *second), QStringLiteral("World"));
        QCOMPARE(selected(a, *first), QStringLiteral("World"));
        // The start anchor's own character is deleted: the range collapses
        // onto the gap it left.
        QVERIFY(b.replaceText(id, body(), 9, 1, QString()));
        QCOMPARE(selected(a, *first), QStringLiteral("World"));
        QCOMPARE(a.textPosition(id, body(), QStringLiteral("unknown")), -1);
        QCOMPARE(a.textAnchor(id, body(), 0), QString());
    }

    void faultFixtureConvergesAfterReplay()
    {
        Fixture fixture;
        LocalPropertySyncService service;
        service.setDocumentId(fixture.base.documentIdString());
        QVERIFY(service.open());
        auto first = fixture.clone();
        auto second = fixture.clone();
        QVERIFY(first && second);
        LocalPropertySyncClient a, b;
        configure(a, first.get(), &service, ActorA);
        configure(b, second.get(), &service, ActorB);
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());

        const QString x = a.createEntity({{gain(), 2.0}});
        QVERIFY(!x.isEmpty());
        QVERIFY(b.setValue(x, name(), QStringLiteral("made by B")));
        QVERIFY(a.replaceText(fixture.id(), body(), 5, 0, QStringLiteral(" there")));
        QVERIFY(b.replaceText(fixture.id(), body(), 0, 1, QStringLiteral("J")));
        const auto early = ReferenceDocumentCodec::capture(*first);
        QVERIFY(early);
        const QString y = b.createEntity();
        QVERIFY(!y.isEmpty());
        QVERIFY(a.deleteEntity(y));
        QVERIFY(a.setValue(fixture.id(), gain(), 4.0));
        a.disconnectFromService();
        b.disconnectFromService();
        QVERIFY(a.deleteEntity(x));
        QVERIFY(b.setValue(x, gain(), 9.0));
        QVERIFY(a.replaceText(fixture.id(), body(), 0, 0, QStringLiteral("<")));
        QVERIFY(b.replaceText(fixture.id(), body(), 2, 0, QStringLiteral("-")));
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        QCOMPARE(dump(*first), dump(*second));
        QVERIFY(first->contains(EntityId::parse(x)));
        QVERIFY(!first->contains(EntityId::parse(y)));
        QCOMPARE(first->value(EntityId::parse(x), gain()).toDouble(), 9.0);

        // Restore (per-key winners sharing one operation ID, revival of a
        // deleted object, text edits), a shared checkpoint and undo/redo
        // operations join the log the fixture shuffles.
        const QString z = b.createEntity({{gain(), 6.0}});
        QVERIFY(!z.isEmpty());
        QVERIFY(a.deleteEntity(x));
        QVERIFY2(a.restoreVersion(early->payload), qPrintable(a.lastError()));
        QVERIFY(first->contains(EntityId::parse(x)));
        QVERIFY(!second->contains(EntityId::parse(z)));
        QCOMPARE(text(*second, fixture), QStringLiteral("Jello there"));
        const auto current = ReferenceDocumentCodec::capture(*second);
        QVERIFY(current);
        QVERIFY(!b.shareCheckpoint(QStringLiteral("Shared"), current->payload).isEmpty());
        QVERIFY(b.setValue(x, gain(), 11.0));
        QVERIFY(b.undo());
        QVERIFY(b.redo());
        QVERIFY(a.replaceText(fixture.id(), body(), 0, 1, QStringLiteral("M")));
        QVERIFY(a.undo());
        QCOMPARE(dump(*first), dump(*second));
        QCOMPARE(first->value(EntityId::parse(x), gain()).toDouble(), 11.0);

        // A third replica gets a shuffled delivery with duplicates and a third
        // of the packets dropped. Every packet must still apply cleanly; the
        // later full replay (reconnect) then yields the same document.
        const QVector<PropertySyncOperation> log = service.operations();
        QVERIFY(log.size() >= 20);
        const auto has = [&](PropertySyncOperation::Kind kind) {
            return std::any_of(log.cbegin(), log.cend(),
                               [&](const PropertySyncOperation &op) { return op.kind == kind; });
        };
        QVERIFY(has(PropertySyncOperation::Kind::Restore));
        QVERIFY(has(PropertySyncOperation::Kind::Checkpoint));
        for (const unsigned seed : {1u, 7u, 42u}) {
            QVector<PropertySyncOperation> delivery = log;
            std::mt19937 generator(seed);
            std::shuffle(delivery.begin(), delivery.end(), generator);
            delivery.append(delivery.mid(0, 3));
            auto replica = fixture.clone();
            QVERIFY(replica);
            LocalPropertySyncClient c;
            configure(c, replica.get(), &service, ActorC);
            for (qsizetype i = 0; i < delivery.size(); ++i)
                if (i % 3 != 2)
                    QVERIFY2(c.receive(delivery.at(i)), qPrintable(c.lastError()));
            QVERIFY2(c.connectToService(), qPrintable(c.lastError()));
            QCOMPARE(dump(*replica), dump(*first));
            QCOMPARE(c.sharedCheckpoints().size(), 1);
            c.disconnectFromService();
        }
    }
};

QTEST_GUILESS_MAIN(TstCollaborationDomain)
#include "tst_collaboration_domain.moc"
