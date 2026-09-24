// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_version_history.h"
#include "reference_document_codec.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

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
};

QTEST_GUILESS_MAIN(TstPropertyVersionHistory)
#include "tst_property_version_history.moc"
