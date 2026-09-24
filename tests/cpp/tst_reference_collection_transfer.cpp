// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_manager.h"
#include "drag_drop_service.h"
#include "reference_document_codec.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class TstReferenceCollectionTransfer final : public QObject {
    Q_OBJECT
private slots:
    void remapsNestedReferencesAndSurvivesUndoSaveAndRecovery()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        DocumentManager manager;
        manager.recoveryManager()->setDirectory(temp.filePath(QStringLiteral("recovery")));
        auto *source = manager.createReferenceDocument();
        auto *destination = manager.createReferenceDocument();
        QVERIFY(source && destination);
        PropertyDocument *document = destination->document();
        const EntityId archive = EntityId::create();
        QVERIFY(document->defineProperty({QStringLiteral("items"), QMetaType::fromType<QVariantList>(),
                                          QVariantList{}, {}, true, false}));
        QVERIFY(document->addEntity(archive, {{QStringLiteral("items"), QVariantList{}}}));
        QVERIFY(manager.activate(destination->documentId()));
        const EntityId first = EntityId::create();
        const EntityId second = EntityId::create();
        TransferPayload payload{DocumentId::parse(source->documentId()), source->document()->revision(),
            TransferOperation::Copy,
            {{first, TransferItemKind::Object,
              {{QStringLiteral("id"), first.toString()}, {QStringLiteral("name"), QStringLiteral("First")},
               {QStringLiteral("metadata"), QJsonObject{{QStringLiteral("links"),
                   QJsonArray{QJsonObject{{QStringLiteral("$ref"), second.toString()}}}}}}}},
             {second, TransferItemKind::Object,
              {{QStringLiteral("id"), second.toString()}, {QStringLiteral("name"), QStringLiteral("Second")}}}}};
        PropertyCollectionDropTarget target(document, manager.undoService(), archive,
            QStringLiteral("items"), QStringLiteral("archive"), QStringLiteral("view"));
        DragDropService drag;
        QVERIFY(!drag.beginDrag(payload, {TransferOperation::Copy}, QStringLiteral("source"), 0,
                                 source->document()).isEmpty());
        QVERIFY(drag.previewDrop(&target, TransferOperation::Copy));
        QVERIFY(!drag.commitDrop());
        const QVariantList inserted = document->value(archive, QStringLiteral("items")).toList();
        QCOMPARE(inserted.size(), 2);
        const QVariantMap a = inserted.at(0).toMap(), b = inserted.at(1).toMap();
        QVERIFY(a.value(QStringLiteral("id")).toString() != first.toString());
        QVERIFY(b.value(QStringLiteral("id")).toString() != second.toString());
        QCOMPARE(a.value(QStringLiteral("metadata")).toMap().value(QStringLiteral("links")).toList()
            .first().toMap().value(QStringLiteral("$ref")).toString(), b.value(QStringLiteral("id")).toString());
        QVERIFY(manager.undoService()->undo());
        QVERIFY(document->value(archive, QStringLiteral("items")).toList().isEmpty());
        QVERIFY(manager.undoService()->redo());
        QCOMPARE(document->value(archive, QStringLiteral("items")).toList(), inserted);

        const QString path = temp.filePath(QStringLiteral("collection.qtk"));
        QSignalSpy saved(destination, &DocumentSession::saveFinished);
        QVERIFY(destination->requestSaveAs(path));
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000);
        QVERIFY(saved.first().at(0).toBool());
        const QString id = destination->documentId();
        QVERIFY(manager.closeDocument(id, DocumentManager::ClosePolicy::Discard));
        auto *reopened = manager.openReferenceDocument(path);
        QVERIFY2(reopened, qPrintable(manager.lastError()));
        QCOMPARE(reopened->document()->value(archive, QStringLiteral("items")).toList(), inserted);

        PropertyCollectionDropTarget reopenedTarget(reopened->document(), manager.undoService(), archive,
            QStringLiteral("items"), QStringLiteral("archive"), QStringLiteral("view"));
        DragDropService secondCopy;
        QVERIFY(!secondCopy.beginDrag(payload, {TransferOperation::Copy}, QStringLiteral("source"), 0,
                                      source->document()).isEmpty());
        QVERIFY(secondCopy.previewDrop(&reopenedTarget, TransferOperation::Copy));
        QVERIFY(!secondCopy.commitDrop());
        const QVariantList recoveryRows = reopened->document()->value(archive, QStringLiteral("items")).toList();
        QCOMPARE(recoveryRows.size(), 4);
        const auto frame = ReferenceDocumentCodec::capture(*reopened->document());
        QVERIFY(frame);
        QVERIFY(!manager.recoveryManager()->record(frame));
        QVERIFY(manager.closeDocument(id, DocumentManager::ClosePolicy::Discard));
        auto *recovered = manager.restoreRecovered(id, path);
        QVERIFY2(recovered, qPrintable(manager.lastError()));
        QCOMPARE(recovered->document()->value(archive, QStringLiteral("items")).toList(), recoveryRows);

        TransferPayload unsupported = payload;
        unsupported.items[0].payload.insert(QStringLiteral("resourceId"), QStringLiteral("external-image"));
        DragDropService rejected;
        PropertyCollectionDropTarget recoveredTarget(recovered->document(), manager.undoService(), archive,
            QStringLiteral("items"), QStringLiteral("archive"), QStringLiteral("view"));
        QVERIFY(!rejected.beginDrag(unsupported).isEmpty());
        QVERIFY(!rejected.previewDrop(&recoveredTarget, TransferOperation::Copy));
        QCOMPARE(recovered->document()->value(archive, QStringLiteral("items")).toList(), recoveryRows);
        unsupported.items[0].payload.remove(QStringLiteral("resourceId"));
        unsupported.items[0].payload.insert(QStringLiteral("metadata"),
            QJsonObject{{QStringLiteral("link"), QJsonObject{{QStringLiteral("$ref"), EntityId::create().toString()}}}});
        DragDropService foreignReference;
        QVERIFY(!foreignReference.beginDrag(unsupported).isEmpty());
        QVERIFY(!foreignReference.previewDrop(&recoveredTarget, TransferOperation::Copy));
        QCOMPARE(recovered->document()->value(archive, QStringLiteral("items")).toList(), recoveryRows);
    }
};

QTEST_GUILESS_MAIN(TstReferenceCollectionTransfer)
#include "tst_reference_collection_transfer.moc"
