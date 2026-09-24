// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-041 document guides: one undoable step per edit, snapping an object to a
// guide under zoom, the saved record, and save/reopen through a session.
#include "canvas_document.h"
#include "canvas_document_format.h"
#include "canvas_snap_model.h"
#include "canvas_transform_controller.h"
#include "document_codec.h"
#include "document_manager.h"
#include "undo_service.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
CanvasObjectId square(CanvasDocument &document, qreal x, qreal y)
{
    CanvasObject object;
    object.shape = CanvasShape::Rect;
    object.rect = QRectF(0, 0, 10, 10);
    object.transform = QTransform::fromTranslate(x, y);
    object.fill = Qt::red;
    return document.insert(object, false);
}

bool saved(DocumentSession *session, const QString &path)
{
    QSignalSpy finished(session, &DocumentSession::saveFinished);
    if (!session->requestSaveAs(path) || !finished.wait(10000)) return false;
    return finished.constFirst().at(0).toBool();
}

QVariantMap guide(const QString &id, const QString &orientation, qreal position)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("orientation"), orientation},
            {QStringLiteral("position"), position}};
}
} // namespace

class TstCanvasGuides final : public QObject {
    Q_OBJECT
private slots:
    void guideEditsAreSingleUndoableSteps();
    void objectSnapsToDocumentGuideUnderZoom();
    void guidesSurviveSessionSaveAndReopen();
    void invalidGuideRecordsAreRejected();
};

void TstCanvasGuides::guideEditsAreSingleUndoableSteps()
{
    CanvasDocument document;
    const quint64 revision = document.revision();
    QVERIFY(document.addGuide(QStringLiteral("diagonal"), 10).isEmpty());
    QVERIFY(document.addGuide(QStringLiteral("vertical"), qQNaN()).isEmpty());
    QCOMPARE(document.revision(), revision);

    const QString first = document.addGuide(QStringLiteral("vertical"), 120);
    QCOMPARE(first, QStringLiteral("guide-1"));
    QCOMPARE(document.undoText(), QStringLiteral("Add guide"));
    const QString second = document.addGuide(QStringLiteral("horizontal"), -40.25);
    QCOMPARE(second, QStringLiteral("guide-2"));
    QCOMPARE(document.revision(), revision + 2);
    QCOMPARE(document.guideList(),
             (QVariantList{guide(first, QStringLiteral("vertical"), 120),
                           guide(second, QStringLiteral("horizontal"), -40.25)}));

    QVERIFY(document.moveGuide(first, 130.5));
    QCOMPARE(document.undoText(), QStringLiteral("Move guide"));
    QVERIFY(!document.moveGuide(first, 130.5)); // unchanged: no step
    QVERIFY(!document.moveGuide(QStringLiteral("missing"), 1));
    QVERIFY(!document.moveGuide(first, qInf()));
    QVERIFY(document.removeGuide(second));
    QCOMPARE(document.undoText(), QStringLiteral("Delete guide"));
    QVERIFY(!document.removeGuide(second));
    QCOMPARE(document.revision(), revision + 4);

    QVERIFY(document.undo());
    QCOMPARE(document.guideRecords().size(), 2);
    QVERIFY(document.undo());
    QCOMPARE(document.guideRecords().constFirst().position, 120.0);
    QVERIFY(document.undo());
    QVERIFY(document.redo());
    QCOMPARE(document.guideRecords().at(1).id, second);
    QVERIFY(document.undo());
    QVERIFY(document.undo());
    QVERIFY(document.guideRecords().isEmpty());
    QCOMPARE(document.addGuide(QStringLiteral("vertical"), 5), QStringLiteral("guide-3"));
}

void TstCanvasGuides::objectSnapsToDocumentGuideUnderZoom()
{
    CanvasDocument document;
    const CanvasObjectId id = square(document, 0, 0);
    const QString rail = document.addGuide(QStringLiteral("vertical"), 50);
    CanvasViewState pose;
    CanvasSnapModel snap;
    snap.setDocument(&document);
    snap.setViewState(&pose);
    snap.setTolerancePixels(8);
    snap.setGuides(document.guideList()); // QML binds snapModel.guides: document.guides
    ToolController tools;
    tools.setDocument(&document);
    tools.setSelection({id});
    CanvasTransformController transform;
    transform.setDocument(&document);
    transform.setTools(&tools);
    transform.setSnapModel(&snap);
    transform.setViewState(&pose);

    for (const qreal zoom : {0.5, 4.0}) {
        pose.setZoom(zoom);
        const qreal outside = 10 / zoom; // view pixels beyond the tolerance
        const qreal inside = 6 / zoom;
        QVERIFY(transform.update(QStringLiteral("move"), {0, 5}, {50 + outside, 5}));
        QCOMPARE(transform.previewBounds().left(), 50 + outside);
        QVERIFY(transform.update(QStringLiteral("move"), {0, 5}, {50 + inside, 5}));
        QCOMPARE(transform.previewBounds().left(), 50.0);
        QVERIFY(transform.finish(QStringLiteral("move")));
        QCOMPARE(document.objectInfo(id).value(QStringLiteral("bounds")).toRectF().left(), 50.0);
        QVERIFY(document.undo());
    }
    QVERIFY(document.moveGuide(rail, 80));
    snap.setGuides(document.guideList());
    QVERIFY(transform.update(QStringLiteral("move"), {0, 5}, {80.5, 5}));
    QCOMPARE(transform.previewBounds().left(), 80.0);
    QVERIFY(transform.cancel());
}

void TstCanvasGuides::guidesSurviveSessionSaveAndReopen()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DocumentManager manager;
    manager.recoveryManager()->setDirectory(temp.path());
    auto owned = std::make_unique<CanvasDocument>();
    CanvasDocument *document = owned.get();
    DocumentSession *session = manager.create(std::move(owned));
    QVERIFY(session);
    QCOMPARE(document->undoService(), manager.undoService()); // shared session history
    const QString vertical = document->addGuide(QStringLiteral("vertical"), 64.125);
    const QString horizontal = document->addGuide(QStringLiteral("horizontal"), -12.5);
    QVERIFY(document->moveGuide(vertical, 72.375));
    QCOMPARE(document->undoText(), QStringLiteral("Move guide"));
    QVERIFY(session->isDirty());
    session->setExtensions(QJsonObject{{QStringLiteral("host.note"), QStringLiteral("kept")}});

    const QString path = temp.filePath(QStringLiteral("guides.qtkc"));
    QVERIFY(saved(session, path));
    const QString documentId = session->documentId();
    QVERIFY(manager.closeDocument(documentId, DocumentManager::ClosePolicy::Cancel));
    DocumentSession *reopened = manager.openDocument(path);
    QVERIFY2(reopened, qPrintable(manager.lastError()));
    auto *restored = qobject_cast<CanvasDocument *>(reopened->modelObject());
    QVERIFY(restored);
    QCOMPARE(restored->guideList(),
             (QVariantList{guide(vertical, QStringLiteral("vertical"), 72.375),
                           guide(horizontal, QStringLiteral("horizontal"), -12.5)}));
    // The guide record is document content, not host extension data.
    QCOMPARE(reopened->extensions(),
             QJsonObject({{QStringLiteral("host.note"), QStringLiteral("kept")}}));
    QVERIFY(!reopened->isDirty());
    QCOMPARE(restored->addGuide(QStringLiteral("vertical"), 1), QStringLiteral("guide-3"));

    // A document without guides saves no guide record at all.
    CanvasDocument plain;
    QVERIFY(!CanvasDocumentFormat::extensionsWithGuides(plain.state())
                 .contains(QLatin1String(CanvasDocumentFormat::GuideExtensionKey)));
}

void TstCanvasGuides::invalidGuideRecordsAreRejected()
{
    CanvasDocumentLimits limits;
    limits.maximumGuides = 2;
    const QString key = QString::fromLatin1(CanvasDocumentFormat::GuideExtensionKey);
    const auto item = [](const QString &id, const QString &orientation, const QJsonValue &position) {
        return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("orientation"), orientation},
                           {QStringLiteral("position"), position}};
    };
    const auto record = [&](const QJsonArray &items, const QJsonValue &next = 3) {
        return QJsonObject{{key, QJsonObject{{QStringLiteral("next"), next},
                                             {QStringLiteral("items"), items}}},
                           {QStringLiteral("host"), 1}};
    };
    CanvasDocumentState state;
    QVERIFY(!CanvasDocumentFormat::takeGuides(record({item("a", "vertical", 1)}), limits, &state));
    QCOMPARE(state.guides.size(), 1);
    QCOMPARE(state.nextGuide, quint64(3));
    QCOMPARE(state.extensions, QJsonObject({{QStringLiteral("host"), 1}}));

    const QJsonObject invalid[] = {
        {{key, QStringLiteral("not a record")}},
        record({item("a", "diagonal", 1)}),
        record({item("a", "vertical", QStringLiteral("1"))}),
        record({item("a", "vertical", 1), item("a", "horizontal", 2)}),
        record({item("", "vertical", 1)}),
        record({item("a", "vertical", 1), item("b", "vertical", 2), item("c", "vertical", 3)}),
        record({item("a", "vertical", 1)}, 0),
        record({item("a", "vertical", 1)}, 1.5),
    };
    for (const QJsonObject &extensions : invalid)
        QVERIFY(CanvasDocumentFormat::takeGuides(extensions, limits, &state));

    // Guides share the extensions budget, so a save that would overflow it
    // fails explicitly instead of writing a file the decoder rejects.
    CanvasDocumentLimits tight;
    tight.maximumExtensionBytes = 64;
    const DocumentCodecPtr codec = makeCanvasDocumentCodec(tight);
    CanvasDocument document;
    OperationError error;
    QVERIFY(codec->capture(document, {}, &error));
    QVERIFY(!document.addGuide(QStringLiteral("vertical"), 1).isEmpty());
    QVERIFY(!codec->capture(document, {}, &error));
    QCOMPARE(error.code, ErrorCode::Unsupported);
}

QTEST_MAIN(TstCanvasGuides)
#include "tst_canvas_guides.moc"
