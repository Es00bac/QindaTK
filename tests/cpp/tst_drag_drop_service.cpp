// SPDX-License-Identifier: LGPL-3.0-or-later
#include "drag_drop_service.h"
#include "edit_transaction.h"
#include "undo_service.h"

#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;

namespace {
struct Fixture {
    PropertyDocument document;
    UndoService undo;
    EntityId collection = EntityId::create();

    bool initialize()
    {
        return document.defineProperty({QStringLiteral("items"),
                                        QMetaType::fromType<QVariantList>(),
                                        QVariantList{}, {}, true, false})
            && document.addEntity(collection, {{QStringLiteral("items"), QVariantList{}}})
            && undo.addDocument(&document);
    }

    QVariantList items() const
    {
        return document.value(collection, QStringLiteral("items")).toList();
    }
};

class CollectionDropTarget final : public DropTarget {
public:
    explicit CollectionDropTarget(Fixture *fixture, QObject *parent = nullptr)
        : DropTarget(parent), m_fixture(fixture) {}

    QString stableTargetId() const override { return QStringLiteral("collection-target"); }
    QString viewId() const override { return QStringLiteral("items-view"); }
    quint64 viewGeneration() const override { return m_generation; }
    SnapshotHeader snapshot() const override { return m_fixture->document.header(); }
    bool supportsAtomicCrossDocumentMove() const override { return m_atomicMove; }

    DropPreview previewDrop(const QString &sessionId, const TransferPayload &payload,
                            TransferOperation operation,
                            const QVariantMap &) override
    {
        ++previewCount;
        if (rejectPreview)
            return {false, {}, QStringLiteral("Target policy rejected the item."),
                    ErrorCode::Validation, {}, false};
        if (operation != TransferOperation::Copy)
            return {false, {}, QStringLiteral("Fixture supports Copy only."),
                    ErrorCode::Unsupported, {}, false};
        m_candidate = m_fixture->items();
        for (const TransferItem &item : payload.items) {
            QVariantMap copy = item.payload.toVariantMap();
            // Copy creates fresh destination identities; source IDs never leak
            // into this document's object collection.
            copy.insert(QStringLiteral("id"), EntityId::create().toString());
            m_candidate.append(copy);
        }
        m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_session = sessionId;
        if (serviceToCancel) serviceToCancel->cancel();
        return {true, QStringLiteral("Copy %1 item(s)").arg(payload.items.size()),
                {}, ErrorCode::None, m_token, false};
    }

    OperationError commitDrop(const QString &sessionId,
                              const QString &previewToken) override
    {
        ++commitCount;
        if (sessionId != m_session || previewToken != m_token)
            return {ErrorCode::Conflict, QStringLiteral("Preview token mismatch."), {}, {}};
        EditTransaction edit;
        edit.setDocument(&m_fixture->document);
        edit.setUndoService(&m_fixture->undo);
        if (!edit.begin({m_fixture->collection}, QStringLiteral("items"),
                        QStringLiteral("Drop items")))
            return edit.error();
        if (const OperationError error = edit.updateValue(m_candidate); error)
            return error;
        return edit.commitEdit();
    }

    void cancelDrop(const QString &sessionId, const QString &previewToken) override
    {
        if (sessionId == m_session && (previewToken.isEmpty() || previewToken == m_token))
            ++cancelCount;
        m_candidate.clear();
        m_token.clear();
    }

    quint64 m_generation = 1;
    bool rejectPreview = false;
    bool m_atomicMove = false;
    DragDropService *serviceToCancel = nullptr;
    int previewCount = 0;
    int commitCount = 0;
    int cancelCount = 0;

private:
    Fixture *m_fixture = nullptr;
    QVariantList m_candidate;
    QString m_session;
    QString m_token;
};

TransferPayload payloadFor(const PropertyDocument &source)
{
    TransferPayload payload;
    payload.sourceDocumentId = source.documentId();
    payload.sourceRevision = source.revision();
    payload.operation = TransferOperation::Copy;
    const EntityId itemId = EntityId::create();
    payload.items.append({itemId, TransferItemKind::Object,
                          {{QStringLiteral("id"), itemId.toString()},
                           {QStringLiteral("label"), QStringLiteral("sample")}}});
    return payload;
}
}

class TstDragDropService final : public QObject {
    Q_OBJECT
private slots:
    void acceptedCopyUsesOneUndoableCollectionEdit()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        const TransferPayload payload = payloadFor(fixture.document);
        PropertyCollectionDropTarget target(
            &fixture.document, &fixture.undo, fixture.collection,
            QStringLiteral("items"), QStringLiteral("collection-target"),
            QStringLiteral("items-view"));
        DragDropService service;
        QVERIFY(!service.beginDrag(payload, {TransferOperation::Copy},
                                  QStringLiteral("source-view"), 4,
                                  &fixture.document).isEmpty());
        QVERIFY(service.previewDrop(&target, TransferOperation::Copy,
                                    {{QStringLiteral("index"), 0}}));
        QCOMPARE(fixture.items().size(), 0); // Preview does not mutate models.
        QVERIFY(!service.commitDrop());
        QCOMPARE(fixture.items().size(), 1);
        QCOMPARE(fixture.items().first().toMap().value(QStringLiteral("label")).toString(),
                 QStringLiteral("sample"));
        QVERIFY(fixture.items().first().toMap().value(QStringLiteral("id")).toString()
                    != payload.items.first().entityId.toString());
        QVERIFY(fixture.undo.canUndo());
        QCOMPARE(fixture.undo.undoText(), QStringLiteral("Drop objects"));
        QVERIFY(fixture.undo.undo());
        QCOMPARE(fixture.items().size(), 0);
        QVERIFY(!fixture.undo.canUndo());
    }

    void rejectedPreviewAndCancellationLeaveModelUnchanged()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        CollectionDropTarget target(&fixture);
        target.rejectPreview = true;
        DragDropService rejected;
        QVERIFY(!rejected.beginDrag(payloadFor(fixture.document)).isEmpty());
        QVERIFY(!rejected.previewDrop(&target, TransferOperation::Copy));
        const OperationError rejectedCommit = rejected.commitDrop();
        QVERIFY(rejectedCommit);
        QCOMPARE(rejectedCommit.code, ErrorCode::Unavailable);
        QCOMPARE(fixture.items().size(), 0);
        QCOMPARE(target.commitCount, 0);

        target.rejectPreview = false;
        DragDropService cancelled;
        QVERIFY(!cancelled.beginDrag(payloadFor(fixture.document)).isEmpty());
        QVERIFY(cancelled.previewDrop(&target, TransferOperation::Copy));
        cancelled.escape();
        QCOMPARE(cancelled.phase(), QStringLiteral("cancelled"));
        QVERIFY(cancelled.preview().isEmpty());
        QCOMPARE(fixture.items().size(), 0);
        QVERIFY(!fixture.undo.canUndo());
    }

    void duplicateCommitCannotApplyTwice()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        CollectionDropTarget target(&fixture);
        DragDropService service;
        QVERIFY(!service.beginDrag(payloadFor(fixture.document)).isEmpty());
        QVERIFY(service.previewDrop(&target, TransferOperation::Copy));
        QVERIFY(!service.commitDrop());
        const OperationError duplicate = service.commitDrop();
        QVERIFY(duplicate);
        QCOMPARE(service.phase(), QStringLiteral("committed"));
        QCOMPARE(target.commitCount, 1);
        QCOMPARE(fixture.items().size(), 1);
        QVERIFY(fixture.undo.undo());
        QCOMPARE(fixture.items().size(), 0);
    }

    void staleTargetViewRejectsCommit()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        CollectionDropTarget target(&fixture);
        DragDropService service;
        QVERIFY(!service.beginDrag(payloadFor(fixture.document)).isEmpty());
        QVERIFY(service.previewDrop(&target, TransferOperation::Copy));
        ++target.m_generation;
        const OperationError stale = service.commitDrop();
        QVERIFY(stale);
        QCOMPARE(stale.code, ErrorCode::StaleGeneration);
        QCOMPARE(target.commitCount, 0);
        QCOMPARE(fixture.items().size(), 0);
        QCOMPARE(target.cancelCount, 1);
    }

    void crossDocumentMoveNeedsAtomicEndpointProof()
    {
        Fixture source;
        Fixture destination;
        QVERIFY(source.initialize());
        QVERIFY(destination.initialize());
        CollectionDropTarget target(&destination);
        DragDropService service;
        TransferPayload payload = payloadFor(source.document);
        QVERIFY(!service.beginDrag(payload, {TransferOperation::Move},
                                  QStringLiteral("source"), 1,
                                  &source.document).isEmpty());
        QVERIFY(!service.previewDrop(&target, TransferOperation::Move));
        QCOMPARE(target.previewCount, 0);
        QCOMPARE(source.items().size(), 0);
        QCOMPARE(destination.items().size(), 0);
        QCOMPARE(service.errorMap().value(QStringLiteral("code")).toInt(),
                 static_cast<int>(ErrorCode::Unsupported));
    }

    void destroyedTargetCannotBeCommitted()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        auto *target = new CollectionDropTarget(&fixture);
        DragDropService service;
        QVERIFY(!service.beginDrag(payloadFor(fixture.document)).isEmpty());
        QVERIFY(service.previewDrop(target, TransferOperation::Copy));
        delete target;
        const OperationError expired = service.commitDrop();
        QVERIFY(expired);
        QCOMPARE(expired.code, ErrorCode::StaleGeneration);
        QCOMPARE(fixture.items().size(), 0);
        QVERIFY(!fixture.undo.canUndo());
    }

    void autoscrollAlwaysReceivesItsStopEdge()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        CollectionDropTarget target(&fixture);
        DragDropService service;
        QVERIFY(!service.beginDrag(payloadFor(fixture.document)).isEmpty());
        QVERIFY(service.previewDrop(&target, TransferOperation::Copy));
        QSignalSpy scroll(&service, &DragDropService::autoscrollRequested);
        service.requestAutoscroll(QStringLiteral("items-view"), QStringLiteral("down"), true);
        service.requestAutoscroll(QStringLiteral("items-view"), QStringLiteral("down"), true);
        service.requestAutoscroll(QStringLiteral("items-view"), QStringLiteral("down"), false);
        QCOMPARE(scroll.size(), 2);
        QCOMPARE(scroll.at(0).at(2).toBool(), true);
        QCOMPARE(scroll.at(1).at(2).toBool(), false);
    }

    void collectionAdapterRejectsUnresolvedReferences()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        TransferPayload payload = payloadFor(fixture.document);
        payload.items[0].payload.insert(QStringLiteral("resourceId"),
                                        QStringLiteral("logical-media-id"));
        PropertyCollectionDropTarget target(
            &fixture.document, &fixture.undo, fixture.collection,
            QStringLiteral("items"), QStringLiteral("collection-target"),
            QStringLiteral("items-view"));
        DragDropService service;
        QVERIFY(!service.beginDrag(payload).isEmpty());
        QVERIFY(!service.previewDrop(&target, TransferOperation::Copy));
        QCOMPARE(service.errorMap().value(QStringLiteral("code")).toInt(),
                 static_cast<int>(ErrorCode::Unsupported));
        QVERIFY(fixture.items().isEmpty());
        QVERIFY(!fixture.undo.canUndo());
    }

    void collectionAdapterRejectsMissingIncomingIdAndDuplicateStoredIds()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        PropertyCollectionDropTarget target(
            &fixture.document, &fixture.undo, fixture.collection,
            QStringLiteral("items"), QStringLiteral("collection-target"),
            QStringLiteral("items-view"));
        DragDropService missingId;
        TransferPayload missing = payloadFor(fixture.document);
        missing.items[0].payload.remove(QStringLiteral("id"));
        QVERIFY(!missingId.beginDrag(missing).isEmpty());
        QVERIFY(!missingId.previewDrop(&target, TransferOperation::Copy));
        QVERIFY(fixture.items().isEmpty());

        const QString duplicateId = EntityId::create().toString();
        EditTransaction seed;
        seed.setDocument(&fixture.document);
        seed.setUndoService(&fixture.undo);
        QVERIFY(seed.begin({fixture.collection}, QStringLiteral("items"),
                           QStringLiteral("Seed invalid duplicate IDs")));
        const QVariantMap row{{QStringLiteral("id"), duplicateId}};
        QVERIFY(!seed.updateValue(QVariantList{row, row}));
        QVERIFY(!seed.commitEdit());
        const QVariantList unchanged = fixture.items();
        DragDropService duplicateStored;
        QVERIFY(!duplicateStored.beginDrag(payloadFor(fixture.document)).isEmpty());
        QVERIFY(!duplicateStored.previewDrop(&target, TransferOperation::Copy));
        QCOMPARE(duplicateStored.errorMap().value(QStringLiteral("code")).toInt(),
                 static_cast<int>(ErrorCode::Validation));
        QCOMPARE(fixture.items(), unchanged);
    }

    void recycledSourceViewCancelsTheDrag()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        CollectionDropTarget target(&fixture);
        DragDropService service;
        QVERIFY(!service.beginDrag(payloadFor(fixture.document),
                                  {TransferOperation::Copy},
                                  QStringLiteral("source-view"), 7,
                                  &fixture.document).isEmpty());
        QVERIFY(service.previewDrop(&target, TransferOperation::Copy));
        service.invalidateView(QStringLiteral("source-view"));
        QCOMPARE(service.phase(), QStringLiteral("rejected"));
        QVERIFY(service.preview().isEmpty());
        QCOMPARE(target.cancelCount, 1);
        QVERIFY(fixture.items().isEmpty());
    }

    void reentrantCancellationReleasesUnpublishedTargetPlan()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        CollectionDropTarget target(&fixture);
        DragDropService service;
        target.serviceToCancel = &service;
        QVERIFY(!service.beginDrag(payloadFor(fixture.document)).isEmpty());
        QVERIFY(!service.previewDrop(&target, TransferOperation::Copy));
        QCOMPARE(service.phase(), QStringLiteral("cancelled"));
        QCOMPARE(target.cancelCount, 1);
        QCOMPARE(target.commitCount, 0);
        QVERIFY(fixture.items().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstDragDropService)
#include "tst_drag_drop_service.moc"
