// SPDX-License-Identifier: LGPL-3.0-or-later
#include "clipboard_service.h"

#include <QBuffer>
#include <QImage>
#include <QtTest>

using namespace QindaTK;

class MemoryClipboard final : public ClipboardAdapter {
public:
    using ClipboardAdapter::ClipboardAdapter;
    const QMimeData *mimeData() const override { return m_data.get(); }
    bool setMimeData(QMimeData *data) override
    {
        if (!data) return false;
        m_data.reset(data);
        emit changed();
        return true;
    }
    void setRaw(const QString &format, const QByteArray &bytes)
    {
        auto *data = new QMimeData;
        data->setData(format, bytes);
        setMimeData(data);
    }
private:
    std::unique_ptr<QMimeData> m_data;
};

class TstClipboardService final : public QObject {
    Q_OBJECT
private:
    struct Fixture {
        PropertyDocument document;
        UndoService undo;
        EntityId target = EntityId::create();
        EntityId source = EntityId::create();
        MemoryClipboard clipboard;
        ClipboardService service;
        Fixture() : clipboard(), service()
        {
            service.setAdapter(&clipboard);
            QVERIFY(document.defineProperty({QStringLiteral("text"), QMetaType::fromType<QString>(),
                                             QString{}, {}, true, false}));
            QVERIFY(document.defineProperty({QStringLiteral("png"), QMetaType::fromType<QByteArray>(),
                                             QByteArray{}, {}, true, false}));
            QVERIFY(document.defineProperty({QStringLiteral("objects"), QMetaType::fromType<QVariantList>(),
                                             QVariantList{}, {}, true, false}));
            QVERIFY(document.addEntity(target));
            QVERIFY(document.addEntity(source));
            QVERIFY(undo.addDocument(&document));
            undo.setActiveDocument(&document);
        }
    };

private slots:
    void externalTextRoundTripsAndUndo()
    {
        Fixture f;
        QVERIFY(f.service.copyText(QStringLiteral("hello λ"), f.document.documentIdString(),
                                   f.source.toString(), QStringLiteral("0")));
        QVERIFY(f.service.canPaste());
        QVERIFY(f.service.availableFormats().contains(QStringLiteral("text/plain")));
        const auto inspection = f.service.inspectPaste(&f.document, f.target, QStringLiteral("text"));
        QCOMPARE(inspection.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QVERIFY(!f.service.paste(&f.document, &f.undo, f.target, QStringLiteral("text")));
        QCOMPARE(f.document.value(f.target, QStringLiteral("text")).toString(), QStringLiteral("hello λ"));
        QVERIFY(f.undo.canUndo());
        QVERIFY(f.undo.undo());
        QCOMPARE(f.document.value(f.target, QStringLiteral("text")).toString(), QString{});
        QVERIFY(f.undo.redo());
        QCOMPARE(f.document.value(f.target, QStringLiteral("text")).toString(), QStringLiteral("hello λ"));
    }

    void objectPasteRemapsIdsAndUsesOneUndoStep()
    {
        Fixture f;
        const EntityId objectId = EntityId::create();
        // Entities already exist, so collection values are set through one reversible edit.
        EditTransaction seed;
        seed.setDocument(&f.document);
        seed.setUndoService(&f.undo);
        QVERIFY(seed.begin({f.source}, QStringLiteral("objects"), QStringLiteral("Seed objects")));
        QVERIFY(!seed.updateValue(QVariantList{QVariantMap{
            {QStringLiteral("id"), objectId.toString()},
            {QStringLiteral("name"), QStringLiteral("Clip")}}}));
        QVERIFY(!seed.commitEdit());
        f.undo.markClean();
        QVERIFY(!f.service.copyObjects(&f.document, f.source, QStringLiteral("objects"), {objectId}));
        QVERIFY(!f.service.paste(&f.document, &f.undo, f.target, QStringLiteral("objects")));
        const QVariantList copied = f.document.value(f.target, QStringLiteral("objects")).toList();
        QCOMPARE(copied.size(), 1);
        const QVariantMap imported = copied.front().toMap();
        QVERIFY(imported.value(QStringLiteral("id")).toString() != objectId.toString());
        QCOMPARE(imported.value(QStringLiteral("name")).toString(), QStringLiteral("Clip"));
        QVERIFY(f.undo.canUndo());
        QCOMPARE(f.undo.undoText(), QStringLiteral("Paste from clipboard"));
        QVERIFY(f.undo.undo());
        QVERIFY(f.document.value(f.target, QStringLiteral("objects")).toList().isEmpty());
        const QVariantList source = f.document.value(f.source, QStringLiteral("objects")).toList();
        QCOMPARE(source.size(), 1);
        QCOMPARE(source.front().toMap().value(QStringLiteral("id")).toString(), objectId.toString());
        QCOMPARE(f.undo.undoText(), QStringLiteral("Seed objects"));
    }

    void malformedInternalPayloadIsReportedAndDoesNotMutate()
    {
        Fixture f;
        f.clipboard.setRaw(QString::fromLatin1(TransferPayloadCodec::InternalMime), QByteArray("{"));
        QVERIFY(!f.service.canPaste());
        const auto inspection = f.service.inspectPaste(&f.document, f.target, QStringLiteral("text"));
        QCOMPARE(inspection.value(QStringLiteral("state")).toString(), QStringLiteral("malformed"));
        QVERIFY(f.service.paste(&f.document, &f.undo, f.target, QStringLiteral("text")));
        QVERIFY(f.document.value(f.target, QStringLiteral("text")).toString().isEmpty());
        QCOMPARE(f.document.revision(), RevisionId{2});
        QVERIFY(!f.undo.canUndo());
    }

    void externalTextAndPngReportConversionLoss()
    {
        Fixture f;
        f.clipboard.setRaw(QStringLiteral("text/plain"), QStringLiteral("from another app").toUtf8());
        const auto textInspection = f.service.inspectPaste(&f.document, f.target, QStringLiteral("text"));
        QCOMPARE(textInspection.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QVERIFY(!textInspection.value(QStringLiteral("lossWarnings")).toStringList().isEmpty());
        QVERIFY(!f.service.paste(&f.document, &f.undo, f.target, QStringLiteral("text")));

        QImage image(2, 2, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        f.clipboard.setRaw(QStringLiteral("image/png"), png);
        const auto imageInspection = f.service.inspectPaste(&f.document, f.target, QStringLiteral("png"));
        QCOMPARE(imageInspection.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QVERIFY(!imageInspection.value(QStringLiteral("lossWarnings")).toStringList().isEmpty());
        QVERIFY(!f.service.paste(&f.document, &f.undo, f.target, QStringLiteral("png")));
        QCOMPARE(f.document.value(f.target, QStringLiteral("png")).toByteArray(), png);
    }

    void resourcesAndForeignReferencesAreExplicitlyRejected()
    {
        Fixture f;
        const EntityId objectId = EntityId::create();
        EditTransaction seed;
        seed.setDocument(&f.document);
        seed.setUndoService(&f.undo);
        QVERIFY(seed.begin({f.source}, QStringLiteral("objects"), QStringLiteral("Seed resource")));
        QVERIFY(!seed.updateValue(QVariantList{QVariantMap{
            {QStringLiteral("id"), objectId.toString()},
            {QStringLiteral("resourceId"), EntityId::create().toString()}}}));
        QVERIFY(!seed.commitEdit());
        f.undo.markClean();
        QVERIFY(!f.service.copyObjects(&f.document, f.source, QStringLiteral("objects"), {objectId}));
        const auto inspection = f.service.inspectPaste(&f.document, f.target, QStringLiteral("objects"));
        QCOMPARE(inspection.value(QStringLiteral("state")).toString(), QStringLiteral("unsupported"));
        QVERIFY(inspection.value(QStringLiteral("message")).toString().contains(QStringLiteral("resource")));
        const RevisionId beforePaste = f.document.revision();
        QVERIFY(f.service.paste(&f.document, &f.undo, f.target, QStringLiteral("objects")));
        QVERIFY(f.document.value(f.target, QStringLiteral("objects")).toList().isEmpty());
        QCOMPARE(f.document.revision(), beforePaste);

        QVariantList sourceObjects = f.document.value(f.source, QStringLiteral("objects")).toList();
        QVariantMap missing = sourceObjects.front().toMap();
        missing.insert(QStringLiteral("missingResource"), true);
        sourceObjects.front() = missing;
        EditTransaction replace;
        replace.setDocument(&f.document);
        replace.setUndoService(&f.undo);
        QVERIFY(replace.begin({f.source}, QStringLiteral("objects"), QStringLiteral("Mark missing resource")));
        QVERIFY(!replace.updateValue(sourceObjects));
        QVERIFY(!replace.commitEdit());
        QVERIFY(!f.service.copyObjects(&f.document, f.source, QStringLiteral("objects"), {objectId}));
        const auto missingInspection = f.service.inspectPaste(&f.document, f.target, QStringLiteral("objects"));
        QCOMPARE(missingInspection.value(QStringLiteral("state")).toString(), QStringLiteral("missing"));
    }

    void linkOperationIsNotSilentlyConvertedToCopy()
    {
        Fixture f;
        const EntityId id = EntityId::create();
        const TransferPayload payload{f.document.documentId(), f.document.revision(),
            TransferOperation::Link,
            {{id, TransferItemKind::Object, {{QStringLiteral("name"), QStringLiteral("Linked")}}}}};
        QVERIFY(!f.service.copyPayload(payload));
        const auto inspection = f.service.inspectPaste(&f.document, f.target, QStringLiteral("objects"));
        QCOMPARE(inspection.value(QStringLiteral("state")).toString(), QStringLiteral("unsupported"));
        QVERIFY(inspection.value(QStringLiteral("message")).toString().contains(QStringLiteral("link adapter")));
        QVERIFY(f.service.paste(&f.document, &f.undo, f.target, QStringLiteral("objects")));
        QVERIFY(f.document.value(f.target, QStringLiteral("objects")).toList().isEmpty());
    }

    void sameDocumentCutMovesCollectionItemsAndUndoRestores()
    {
        Fixture f;
        const EntityId a = EntityId::create(), b = EntityId::create();
        EditTransaction seed;
        seed.setDocument(&f.document);
        seed.setUndoService(&f.undo);
        QVERIFY(seed.begin({f.source}, QStringLiteral("objects"), QStringLiteral("Seed two objects")));
        QVERIFY(!seed.updateValue(QVariantList{
            QVariantMap{{QStringLiteral("id"), a.toString()}, {QStringLiteral("name"), QStringLiteral("A")}},
            QVariantMap{{QStringLiteral("id"), b.toString()}, {QStringLiteral("name"), QStringLiteral("B")}}}));
        QVERIFY(!seed.commitEdit());
        f.undo.markClean();
        QVERIFY(!f.service.cutObjects(&f.document, f.source, QStringLiteral("objects"), {a}));
        QVERIFY(f.service.cutPending());
        QVERIFY(!f.service.paste(&f.document, &f.undo, f.source, QStringLiteral("objects")));
        QVERIFY(!f.service.cutPending());
        const QVariantList moved = f.document.value(f.source, QStringLiteral("objects")).toList();
        QCOMPARE(moved.size(), 2);
        QCOMPARE(moved[0].toMap().value(QStringLiteral("id")).toString(), b.toString());
        QCOMPARE(moved[1].toMap().value(QStringLiteral("id")).toString(), a.toString());
        QVERIFY(f.undo.undo());
        const QVariantList restored = f.document.value(f.source, QStringLiteral("objects")).toList();
        QCOMPARE(restored[0].toMap().value(QStringLiteral("id")).toString(), a.toString());
        QCOMPARE(restored[1].toMap().value(QStringLiteral("id")).toString(), b.toString());
    }

    void cutRejectsStaleSourceWithoutMutation()
    {
        Fixture f;
        const EntityId id = EntityId::create();
        EditTransaction seed;
        seed.setDocument(&f.document);
        seed.setUndoService(&f.undo);
        QVERIFY(seed.begin({f.source}, QStringLiteral("objects"), QStringLiteral("Seed object")));
        QVERIFY(!seed.updateValue(QVariantList{QVariantMap{
            {QStringLiteral("id"), id.toString()}, {QStringLiteral("name"), QStringLiteral("A")}}}));
        QVERIFY(!seed.commitEdit());
        QVERIFY(!f.service.cutObjects(&f.document, f.source, QStringLiteral("objects"), {id}));
        const EntityId unrelated = EntityId::create();
        QVERIFY(f.document.addEntity(unrelated));
        const auto before = f.document.value(f.source, QStringLiteral("objects"));
        const auto failure = f.service.paste(&f.document, &f.undo, f.source, QStringLiteral("objects"));
        QVERIFY(failure);
        QCOMPARE(failure.code, ErrorCode::StaleRevision);
        QCOMPARE(f.document.value(f.source, QStringLiteral("objects")), before);
        QVERIFY(!f.service.cutPending());
    }
};

QTEST_MAIN(TstClipboardService)
#include "tst_clipboard_service.moc"
