// SPDX-License-Identifier: LGPL-3.0-or-later
#include "edit_transaction.h"
#include "reference_document_codec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

using namespace QindaTK;

class TstReferenceDocumentCodec final : public QObject {
    Q_OBJECT
private slots:
    void roundtripKeepsExactIdentityValuesAndMetadata()
    {
        PropertyDocument source;
        QVERIFY(source.defineProperty({QStringLiteral("ticks"), QMetaType::fromType<qlonglong>(),
                                       QVariant::fromValue(0LL), QStringLiteral("samples"), true, true}));
        QVERIFY(source.defineProperty({QStringLiteral("label"), QMetaType::fromType<QString>(),
                                       QString{}, {}, true, false}));
        QVERIFY(source.defineProperty({QStringLiteral("payload"), QMetaType::fromType<QByteArray>(),
                                       QByteArray{}, {}, true, false}));
        const EntityId id = EntityId::create();
        const auto exact = QVariant::fromValue(9007199254740993LL);
        QVERIFY(source.addEntity(id, {{QStringLiteral("ticks"), exact},
                                      {QStringLiteral("label"), QStringLiteral("Café 🔊")},
                                      {QStringLiteral("payload"), QByteArray("\0\1\xff", 3)}}));
        QVERIFY(source.setTargetAnimated(id, QStringLiteral("ticks"), true));
        QVERIFY(source.setTargetWritable(id, QStringLiteral("payload"), false));
        OperationError error;
        const QJsonObject extensions{{QStringLiteral("vendor.blob"),
                                      QJsonObject{{QStringLiteral("name"), QStringLiteral("keep")}}}};
        const SnapshotPtr snapshot = ReferenceDocumentCodec::capture(source, extensions, &error);
        QVERIFY(snapshot != nullptr);
        QVERIFY(!error);
        QCOMPARE(snapshot->header.documentId, source.documentId());
        QCOMPARE(snapshot->header.revision, source.revision());
        auto restored = ReferenceDocumentCodec::decode(snapshot->payload, &error);
        QVERIFY(restored.has_value());
        QVERIFY(!error);
        QVERIFY(!restored->migratedFromV1);
        QCOMPARE(restored->document->documentId(), source.documentId());
        QCOMPARE(restored->document->revision(), source.revision());
        QCOMPARE(restored->document->resetGeneration(), source.resetGeneration());
        QCOMPARE(restored->document->committedValue(id, QStringLiteral("ticks")), exact);
        QCOMPARE(restored->document->committedValue(id, QStringLiteral("label")), QVariant(QStringLiteral("Café 🔊")));
        QCOMPARE(restored->document->committedValue(id, QStringLiteral("payload")), QVariant(QByteArray("\0\1\xff", 3)));
        QVERIFY(restored->document->isTargetAnimated(id, QStringLiteral("ticks")));
        QVERIFY(!restored->document->isTargetWritable(id, QStringLiteral("payload")));
        QCOMPARE(restored->extensions, extensions);

        // The immutable bytes still describe the captured revision after a
        // newer edit mutates the live document.
        UndoService undo;
        QVERIFY(undo.addDocument(&source));
        EditTransaction edit;
        edit.setDocument(&source);
        edit.setUndoService(&undo);
        QVERIFY(edit.begin({id}, QStringLiteral("ticks"), QStringLiteral("Move")));
        QVERIFY(!edit.updateValue(QVariant::fromValue(9007199254740995LL)));
        QVERIFY(!edit.commitEdit());
        QCOMPARE(restored->document->committedValue(id, QStringLiteral("ticks")), exact);
        QCOMPARE(snapshot->header.revision.value + 1, source.revision().value);
    }

    void migratesVersionOneAndRejectsMalformedOrFutureData()
    {
        PropertyDocument source;
        QVERIFY(source.defineProperty({QStringLiteral("gain"), QMetaType::fromType<double>(),
                                       0.0, {}, true, false}));
        const EntityId id = EntityId::create();
        QVERIFY(source.addEntity(id, {{QStringLiteral("gain"), 1.25}}));
        const auto snapshot = ReferenceDocumentCodec::capture(source);
        QVERIFY(snapshot != nullptr);
        QJsonObject old = QJsonDocument::fromJson(snapshot->payload).object();
        old.insert(QStringLiteral("version"), 1);
        old.remove(QStringLiteral("resetGeneration"));
        old.remove(QStringLiteral("extensions"));
        QJsonArray entities = old.value(QStringLiteral("entities")).toArray();
        QJsonObject entity = entities[0].toObject();
        entity.remove(QStringLiteral("readOnly"));
        entity.remove(QStringLiteral("animated"));
        QJsonObject values = entity.value(QStringLiteral("values")).toObject();
        QJsonObject gain = values.value(QStringLiteral("gain")).toObject();
        gain.insert(QStringLiteral("value"), 1.25);
        values.insert(QStringLiteral("gain"), gain);
        entity.insert(QStringLiteral("values"), values);
        entities[0] = entity;
        old.insert(QStringLiteral("entities"), entities);
        OperationError error;
        auto migrated = ReferenceDocumentCodec::decode(QJsonDocument(old).toJson(), &error);
        QVERIFY(migrated.has_value());
        QVERIFY(migrated->migratedFromV1);
        QCOMPARE(migrated->document->committedValue(id, QStringLiteral("gain")), QVariant(1.25));
        QCOMPARE(migrated->document->resetGeneration(), 0);
        const auto upgraded = ReferenceDocumentCodec::capture(*migrated->document);
        QVERIFY(upgraded != nullptr);
        QCOMPARE(QJsonDocument::fromJson(upgraded->payload).object().value(QStringLiteral("version")).toInt(), 2);

        QJsonObject future = old;
        future.insert(QStringLiteral("version"), 9);
        QVERIFY(!ReferenceDocumentCodec::decode(QJsonDocument(future).toJson(), &error));
        QCOMPARE(error.code, ErrorCode::Unsupported);
        QVERIFY(!ReferenceDocumentCodec::decode(snapshot->payload.left(20), &error));
        QCOMPARE(error.code, ErrorCode::InvalidArgument);
        QJsonObject duplicate = QJsonDocument::fromJson(snapshot->payload).object();
        QJsonArray properties = duplicate.value(QStringLiteral("properties")).toArray();
        properties.append(properties.first());
        duplicate.insert(QStringLiteral("properties"), properties);
        QVERIFY(!ReferenceDocumentCodec::decode(QJsonDocument(duplicate).toJson(), &error));
        QCOMPARE(error.code, ErrorCode::InvalidArgument);
        QJsonObject unknown = QJsonDocument::fromJson(snapshot->payload).object();
        unknown.insert(QStringLiteral("futureMetadata"), 42);
        QVERIFY(!ReferenceDocumentCodec::decode(QJsonDocument(unknown).toJson(), &error));
        QCOMPARE(error.code, ErrorCode::Unsupported);
    }
};

QTEST_GUILESS_MAIN(TstReferenceDocumentCodec)
#include "tst_reference_document_codec.moc"
