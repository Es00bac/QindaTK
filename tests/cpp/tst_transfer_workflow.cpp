// SPDX-License-Identifier: LGPL-3.0-or-later
#include "clipboard_service.h"
#include "drag_drop_service.h"
#include <QtTest>

using namespace QindaTK;

namespace {
class MemoryClipboard final : public ClipboardAdapter {
public:
    using ClipboardAdapter::ClipboardAdapter;
    const QMimeData *mimeData() const override { return m_data.get(); }
    bool setMimeData(QMimeData *data) override {
        m_data.reset(data);
        emit changed();
        return true;
    }
private:
    std::unique_ptr<QMimeData> m_data;
};
}

class TstTransferWorkflow final : public QObject {
    Q_OBJECT
private slots:
    void clipboardPayloadCopiesIntoAnotherDocumentThroughDropAndPaste()
    {
        const EntityId originalId = EntityId::create();
        const EntityId sourceCollection = EntityId::create();
        const EntityId destinationCollection = EntityId::create();
        PropertyDocument source;
        PropertyDocument destination;
        const PropertyDescriptor property{QStringLiteral("items"),
            QMetaType::fromType<QVariantList>(), QVariantList{}, {}, true, false};
        QVERIFY(source.defineProperty(property));
        QVERIFY(destination.defineProperty(property));
        QVERIFY(source.addEntity(sourceCollection, {{QStringLiteral("items"),
            QVariantList{QVariantMap{{QStringLiteral("id"), originalId.toString()},
                                     {QStringLiteral("name"), QStringLiteral("Cross document")}}}}}));
        QVERIFY(destination.addEntity(destinationCollection,
            {{QStringLiteral("items"), QVariantList{}}}));
        UndoService undo;
        QVERIFY(undo.addDocument(&destination));

        MemoryClipboard adapter;
        ClipboardService clipboard;
        clipboard.setAdapter(&adapter);
        QVERIFY(!clipboard.copyObjects(&source, sourceCollection,
            QStringLiteral("items"), {originalId}));
        const QMimeData *mime = adapter.mimeData();
        QVERIFY(mime);
        QVERIFY(mime->formats().contains(
            QString::fromLatin1(TransferPayloadCodec::InternalMime)));
        QVERIFY(!mime->formats().contains(QStringLiteral("text/plain")));
        OperationError decodedError;
        const auto payload = TransferPayloadCodec::decode(
            mime->data(QString::fromLatin1(TransferPayloadCodec::InternalMime)),
            &decodedError);
        QVERIFY2(payload.has_value(), qPrintable(decodedError.message));

        PropertyCollectionDropTarget target(&destination, &undo,
            destinationCollection, QStringLiteral("items"),
            QStringLiteral("destination-collection"), QStringLiteral("destination-view"));
        DragDropService drag;
        QVERIFY(!drag.beginDrag(*payload, {TransferOperation::Copy},
            QStringLiteral("source-view"), 1, &source).isEmpty());
        QVERIFY(drag.previewDrop(&target, TransferOperation::Copy));
        QVERIFY(destination.value(destinationCollection,
            QStringLiteral("items")).toList().isEmpty());
        QVERIFY(!drag.commitDrop());
        const QVariantList dropped = destination.value(destinationCollection,
            QStringLiteral("items")).toList();
        QCOMPARE(dropped.size(), 1);
        const QString droppedId = dropped.first().toMap().value(
            QStringLiteral("id")).toString();
        QVERIFY(droppedId != originalId.toString());
        QVERIFY(undo.undo());
        QVERIFY(destination.value(destinationCollection,
            QStringLiteral("items")).toList().isEmpty());

        const auto inspection = clipboard.inspectPaste(&destination,
            destinationCollection, QStringLiteral("items"));
        QCOMPARE(inspection.value(QStringLiteral("state")).toString(),
                 QStringLiteral("ready"));
        QVERIFY(!clipboard.paste(&destination, &undo, destinationCollection,
                                QStringLiteral("items")));
        const QVariantList pasted = destination.value(destinationCollection,
            QStringLiteral("items")).toList();
        QCOMPARE(pasted.size(), 1);
        const QString pastedId = pasted.first().toMap().value(
            QStringLiteral("id")).toString();
        QVERIFY(pastedId != originalId.toString());
        QVERIFY(pastedId != droppedId);
        QVERIFY(undo.undo());
        QVERIFY(destination.value(destinationCollection,
            QStringLiteral("items")).toList().isEmpty());
        const QVariantList original = source.value(sourceCollection,
            QStringLiteral("items")).toList();
        QCOMPARE(original.size(), 1);
        QCOMPARE(original.first().toMap().value(QStringLiteral("id")).toString(),
                 originalId.toString());
    }
};

QTEST_MAIN(TstTransferWorkflow)
#include "tst_transfer_workflow.moc"
