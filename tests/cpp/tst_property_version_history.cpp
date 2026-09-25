// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_version_history.h"
#include "reference_document_codec.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
PropertyDescriptor describe(const QString &id, QMetaType type, const QVariant &defaultValue)
{
    PropertyDescriptor descriptor;
    descriptor.id = id;
    descriptor.type = type;
    descriptor.defaultValue = defaultValue;
    return descriptor;
}
}

class TstPropertyVersionHistory final : public QObject {
    Q_OBJECT
private slots:
    void offlineRestoreIsOneNewOperationAndKeepsCurrentCheckpoint()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PropertyDocument base;
        PropertyDescriptor gain;
        gain.id = QStringLiteral("gain");
        gain.type = QMetaType::fromType<double>();
        gain.defaultValue = 0.0;
        QVERIFY(base.defineProperty(gain));
        PropertyDescriptor name;
        name.id = QStringLiteral("name");
        name.type = QMetaType::fromType<QString>();
        name.defaultValue = QStringLiteral("base");
        QVERIFY(base.defineProperty(name));
        const EntityId entity = EntityId::create();
        QVERIFY(base.addEntity(entity));
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        QVERIFY(snapshot);
        auto first = ReferenceDocumentCodec::decode(snapshot->payload);
        auto second = ReferenceDocumentCodec::decode(snapshot->payload);
        QVERIFY(first && second);

        LocalPropertySyncService service;
        service.setDocumentId(base.documentIdString());
        service.setStoragePath(directory.filePath(QStringLiteral("operations.json")));
        QVERIFY(service.open());
        LocalPropertySyncClient a, b;
        a.setDocument(first->document.get());
        a.setService(&service);
        a.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000a"));
        b.setDocument(second->document.get());
        b.setService(&service);
        b.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000b"));
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());

        PropertyVersionHistory history;
        const QString versionsPath = directory.filePath(QStringLiteral("versions.json"));
        history.setClient(&a);
        history.setStoragePath(versionsPath);
        QVERIFY(history.open());
        QVERIFY(a.setValue(entity.toString(), QStringLiteral("gain"), 1.0));
        QVERIFY(a.setValue(entity.toString(), QStringLiteral("name"), QStringLiteral("old")));
        const QString older = history.checkpoint(QStringLiteral("First pass"));
        QVERIFY(!older.isEmpty());
        QVERIFY(a.setValue(entity.toString(), QStringLiteral("gain"), 3.0));
        QVERIFY(a.setValue(entity.toString(), QStringLiteral("name"), QStringLiteral("new")));
        const QString newer = history.checkpoint(QStringLiteral("Second pass"));
        QVERIFY(!newer.isEmpty());
        QCOMPARE(history.count(), 2);
        QCOMPARE(history.diff(older, newer).size(), 2);
        QCOMPARE(history.inspect(older).value(QStringLiteral("name")).toString(),
                 QStringLiteral("First pass"));
        QCOMPARE(history.inspect(older).value(QStringLiteral("actor")).toString(), a.actorId());
        QCOMPARE(history.diffCurrent(older).size(), 2);

        a.disconnectFromService();
        const auto secondRevision = second->document->revision().value;
        QVERIFY(history.restore(older));
        QCOMPARE(history.count(), 3);
        QCOMPARE(history.versions().first().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Before restore: First pass"));
        QCOMPARE(a.pendingCount(), 1);
        QCOMPARE(first->document->value(entity, QStringLiteral("gain")).toDouble(), 1.0);
        QCOMPARE(second->document->value(entity, QStringLiteral("gain")).toDouble(), 3.0);
        QCOMPARE(service.operationCount(), 4);

        QSignalSpy secondChanged(second->document.get(), &PropertyDocument::changed);
        QVERIFY(a.connectToService());
        QCOMPARE(service.operationCount(), 5);
        QCOMPARE(service.operations().last().kind, PropertySyncOperation::Kind::Restore);
        QCOMPARE(a.pendingCount(), 0);
        QCOMPARE(second->document->value(entity, QStringLiteral("gain")).toDouble(), 1.0);
        QCOMPARE(second->document->value(entity, QStringLiteral("name")).toString(),
                 QStringLiteral("old"));
        QCOMPARE(second->document->revision().value, secondRevision + 1);
        QCOMPARE(secondChanged.size(), 1);

        PropertyVersionHistory reopened;
        reopened.setClient(&a);
        reopened.setStoragePath(versionsPath);
        QVERIFY(reopened.open());
        QCOMPARE(reopened.count(), 3);
        QCOMPARE(reopened.diff(older, newer).size(), 2);
        QCOMPARE(reopened.inspect(newer).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Second pass"));
        QCOMPARE(reopened.diffCurrent(older).size(), 0);
    }

    void sharedCheckpointsListOnEveryReplicaAndSurviveRestart()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PropertyDocument base;
        QVERIFY(base.defineProperty(describe(QStringLiteral("gain"), QMetaType::fromType<double>(), 0.0)));
        const EntityId entity = EntityId::create();
        QVERIFY(base.addEntity(entity));
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        QVERIFY(snapshot);
        auto first = ReferenceDocumentCodec::decode(snapshot->payload);
        auto second = ReferenceDocumentCodec::decode(snapshot->payload);
        auto third = ReferenceDocumentCodec::decode(snapshot->payload);
        QVERIFY(first && second && third);
        const QString store = directory.filePath(QStringLiteral("operations.json"));
        LocalPropertySyncService service;
        service.setDocumentId(base.documentIdString());
        service.setStoragePath(store);
        QVERIFY(service.open());
        LocalPropertySyncClient a, b;
        a.setDocument(first->document.get());
        a.setService(&service);
        a.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000a"));
        b.setDocument(second->document.get());
        b.setService(&service);
        b.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000b"));
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        PropertyVersionHistory historyA, historyB;
        historyA.setClient(&a);
        historyA.setShared(true);
        historyB.setClient(&b);
        historyB.setShared(true);
        QVERIFY(historyA.open());
        QVERIFY(historyB.open());

        QVERIFY(a.setValue(entity.toString(), QStringLiteral("gain"), 1.0));
        const QString draft = historyA.checkpoint(QStringLiteral("Draft"));
        QVERIFY2(!draft.isEmpty(), qPrintable(historyA.lastError()));
        QCOMPARE(historyB.count(), 1);
        const QVariantMap listed = historyB.versions().first().toMap();
        QCOMPARE(listed.value(QStringLiteral("id")).toString(), draft);
        QCOMPARE(listed.value(QStringLiteral("name")).toString(), QStringLiteral("Draft"));
        QCOMPARE(listed.value(QStringLiteral("actor")).toString(), a.actorId());
        QVERIFY(listed.value(QStringLiteral("shared")).toBool());
        QCOMPARE(first->document->value(entity, QStringLiteral("gain")).toDouble(), 1.0);

        // B restores A's shared version; its safety version is shared too.
        QVERIFY(b.setValue(entity.toString(), QStringLiteral("gain"), 5.0));
        QVERIFY2(historyB.restore(draft), qPrintable(historyB.lastError()));
        QCOMPARE(historyA.count(), 2);
        QCOMPARE(historyB.count(), 2);
        QCOMPARE(historyA.versions().first().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Before restore: Draft"));
        QCOMPARE(first->document->value(entity, QStringLiteral("gain")).toDouble(), 1.0);
        QCOMPARE(second->document->value(entity, QStringLiteral("gain")).toDouble(), 1.0);
        QCOMPARE(historyA.diff(draft, historyA.versions().first().toMap()
                                          .value(QStringLiteral("id")).toString()).size(), 1);

        // A fresh replica reads the same list from the durable log.
        a.disconnectFromService();
        b.disconnectFromService();
        LocalPropertySyncService reopened;
        reopened.setDocumentId(base.documentIdString());
        reopened.setStoragePath(store);
        QVERIFY(reopened.open());
        LocalPropertySyncClient c;
        c.setDocument(third->document.get());
        c.setService(&reopened);
        c.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000c"));
        QVERIFY(c.connectToService());
        PropertyVersionHistory historyC;
        historyC.setClient(&c);
        historyC.setShared(true);
        QVERIFY(historyC.open());
        QCOMPARE(historyC.versions(), historyA.versions());
        QCOMPARE(third->document->value(entity, QStringLiteral("gain")).toDouble(), 1.0);
    }

    void restoreRevivesDeletedAndRemovesLaterObjects()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PropertyDocument base;
        QVERIFY(base.defineProperty(describe(QStringLiteral("gain"), QMetaType::fromType<double>(), 0.0)));
        QVERIFY(base.defineProperty(describe(QStringLiteral("body"),
                                              QMetaType::fromType<QString>(), QString())));
        const EntityId entity = EntityId::create();
        QVERIFY(base.addEntity(entity, {{QStringLiteral("body"), QStringLiteral("one")}}));
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        QVERIFY(snapshot);
        auto first = ReferenceDocumentCodec::decode(snapshot->payload);
        auto second = ReferenceDocumentCodec::decode(snapshot->payload);
        QVERIFY(first && second);
        LocalPropertySyncService service;
        service.setDocumentId(base.documentIdString());
        QVERIFY(service.open());
        LocalPropertySyncClient a, b;
        a.setDocument(first->document.get());
        a.setService(&service);
        a.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000a"));
        a.setTextProperties({QStringLiteral("body")});
        b.setDocument(second->document.get());
        b.setService(&service);
        b.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000b"));
        b.setTextProperties({QStringLiteral("body")});
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        PropertyVersionHistory history;
        history.setClient(&a);
        history.setStoragePath(directory.filePath(QStringLiteral("versions.json")));
        QVERIFY(history.open());

        const QString version = history.checkpoint(QStringLiteral("One object"));
        QVERIFY(!version.isEmpty());
        const QString later = a.createEntity({{QStringLiteral("gain"), 2.0}});
        QVERIFY(!later.isEmpty());
        QVERIFY(a.replaceText(entity.toString(), QStringLiteral("body"), 3, 0,
                              QStringLiteral(" two")));
        QVERIFY(b.deleteEntity(entity.toString()));
        QVERIFY(!first->document->contains(entity));
        const QVariantList difference = history.diffCurrent(version);
        QCOMPARE(difference.size(), 4);
        QCOMPARE(difference.first().toMap().value(QStringLiteral("change")).toString(),
                 difference.first().toMap().value(QStringLiteral("entity")).toString()
                         == later ? QStringLiteral("added") : QStringLiteral("removed"));

        QVERIFY2(history.restore(version), qPrintable(history.lastError()));
        for (auto *document : {first->document.get(), second->document.get()}) {
            QVERIFY(document->contains(entity));
            QVERIFY(!document->contains(EntityId::parse(later)));
            QCOMPARE(document->value(entity, QStringLiteral("body")).toString(),
                     QStringLiteral("one"));
        }
        QCOMPARE(history.diffCurrent(version).size(), 0);
    }

    void historyKeepsTheNewestVersionsSoRestoreNeverStops()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PropertyDocument base;
        QVERIFY(base.defineProperty(describe(QStringLiteral("gain"), QMetaType::fromType<double>(), 0.0)));
        const EntityId entity = EntityId::create();
        QVERIFY(base.addEntity(entity));
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        QVERIFY(snapshot);
        auto first = ReferenceDocumentCodec::decode(snapshot->payload);
        auto second = ReferenceDocumentCodec::decode(snapshot->payload);
        QVERIFY(first && second);
        LocalPropertySyncService service;
        service.setDocumentId(base.documentIdString());
        QVERIFY(service.open());
        LocalPropertySyncClient a, b;
        a.setDocument(first->document.get());
        a.setService(&service);
        a.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000a"));
        b.setDocument(second->document.get());
        b.setService(&service);
        b.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000b"));
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        PropertyVersionHistory local, sharedA, sharedB;
        local.setClient(&a);
        local.setStoragePath(directory.filePath(QStringLiteral("versions.json")));
        sharedA.setClient(&a);
        sharedA.setShared(true);
        sharedB.setClient(&b);
        sharedB.setShared(true);
        QVERIFY(local.open() && sharedA.open() && sharedB.open());

        // Past 64 versions both modes keep the newest 64 instead of refusing.
        for (int i = 0; i < 70; ++i) {
            QVERIFY(a.setValue(entity.toString(), QStringLiteral("gain"), double(i)));
            QVERIFY2(!local.checkpoint(QStringLiteral("Local %1").arg(i)).isEmpty(),
                     qPrintable(local.lastError()));
            QVERIFY2(!sharedA.checkpoint(QStringLiteral("Shared %1").arg(i)).isEmpty(),
                     qPrintable(sharedA.lastError()));
        }
        QCOMPARE(local.count(), 64);
        QCOMPARE(sharedB.count(), 64);
        QCOMPARE(sharedB.versions(), sharedA.versions());
        QCOMPARE(local.versions().last().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Local 6"));

        // Restoring the oldest kept version still works: its safety version
        // evicts it, but the restore itself goes through.
        const QString oldest = local.versions().last().toMap().value(QStringLiteral("id")).toString();
        QVERIFY2(local.restore(oldest), qPrintable(local.lastError()));
        QCOMPARE(local.count(), 64);
        QCOMPARE(second->document->value(entity, QStringLiteral("gain")).toDouble(), 6.0);
        const QString sharedOldest =
            sharedB.versions().last().toMap().value(QStringLiteral("id")).toString();
        QVERIFY2(sharedB.restore(sharedOldest), qPrintable(sharedB.lastError()));
        QCOMPARE(first->document->value(entity, QStringLiteral("gain")).toDouble(), 6.0);
        QCOMPARE(sharedA.count(), 64);

        // A local version can be removed explicitly; a shared one cannot.
        QVERIFY(local.remove(oldest) == false);
        const QString newest = local.versions().first().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(local.remove(newest));
        QCOMPARE(local.count(), 63);
        QVERIFY(!sharedA.remove(sharedOldest));
    }

    void restoresVersionsLargerThanOnePacket()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PropertyDocument base;
        QVERIFY(base.defineProperty(describe(QStringLiteral("body"),
                                              QMetaType::fromType<QString>(), QString())));
        const EntityId entity = EntityId::create();
        QVERIFY(base.addEntity(entity));
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        QVERIFY(snapshot);
        auto first = ReferenceDocumentCodec::decode(snapshot->payload);
        auto second = ReferenceDocumentCodec::decode(snapshot->payload);
        QVERIFY(first && second);
        LocalPropertySyncService service;
        service.setDocumentId(base.documentIdString());
        QVERIFY(service.open());
        LocalPropertySyncClient a, b;
        for (auto *client : {&a, &b}) {
            client->setService(&service);
            client->setTextProperties({QStringLiteral("body")});
        }
        a.setDocument(first->document.get());
        a.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000a"));
        b.setDocument(second->document.get());
        b.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000b"));
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());
        PropertyVersionHistory history;
        history.setClient(&a);
        history.setStoragePath(directory.filePath(QStringLiteral("versions.json")));
        QVERIFY(history.open());

        // 45,000 units: over one 4,096-unit insert and a 40 KiB packet.
        QString long_(45000, QLatin1Char('a'));
        for (int i = 0; i < long_.size(); i += 97) long_[i] = QLatin1Char('b');
        QVERIFY2(a.replaceText(entity.toString(), QStringLiteral("body"), 0, 0, long_),
                 qPrintable(a.lastError()));
        QCOMPARE(second->document->value(entity, QStringLiteral("body")).toString(), long_);
        const QString version = history.checkpoint(QStringLiteral("Long"));
        QVERIFY2(!version.isEmpty(), qPrintable(history.lastError()));
        QVERIFY(b.replaceText(entity.toString(), QStringLiteral("body"), 20000, 10,
                              QStringLiteral("CHANGED")));
        const qsizetype before = service.operationCount();
        QVERIFY2(history.restore(version), qPrintable(history.lastError()));
        QCOMPARE(first->document->value(entity, QStringLiteral("body")).toString(), long_);
        QCOMPARE(second->document->value(entity, QStringLiteral("body")).toString(), long_);
        // Restore replaced only the changed middle: one delete, one insert.
        QCOMPARE(service.operationCount() - before, 3);
    }
};

QTEST_GUILESS_MAIN(TstPropertyVersionHistory)
#include "tst_property_version_history.moc"
