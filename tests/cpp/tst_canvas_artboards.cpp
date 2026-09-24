// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-042 artboards: one undo step per board edit with object coordinates
// kept, backgrounds painted below objects, export regions, session
// save/reopen with the layer tree, rejected records, the ArtboardView model,
// the artboard alignment reference, and a fitted view that keeps navigation.
#include "canvas_alignment_model.h"
#include "canvas_artboard_model.h"
#include "canvas_document.h"
#include "canvas_document_format.h"
#include "canvas_render.h"
#include "canvas_selection_model.h"
#include "canvas_surface.h"
#include "canvas_svg_export.h"
#include "document_manager.h"

#include <QJsonArray>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
bool saved(DocumentSession *session, const QString &path)
{
    QSignalSpy finished(session, &DocumentSession::saveFinished);
    if (!session->requestSaveAs(path) || !finished.wait(10000)) return false;
    return finished.constFirst().at(0).toBool();
}

QRgb pixel(const CanvasDocument &document, int x, int y)
{
    return CanvasPainter::renderTile(document, QRectF(0, 0, 64, 64), 64, {}).pixel(x, y);
}
} // namespace

class TstCanvasArtboards final : public QObject {
    Q_OBJECT
private slots:
    void boardEditsAreSingleUndoableSteps();
    void backgroundsPaintBelowObjectsAndExportRegions();
    void sceneSurvivesSessionSaveAndReopen();
    void invalidBoardRecordsAreRejected();
    void artboardModelPreviewsAndCommitsOneStep();
    void alignmentUsesTheArtboardReference();
    void fittedViewKeepsExternalNavigation();
};

void TstCanvasArtboards::boardEditsAreSingleUndoableSteps()
{
    CanvasDocument doc;
    const CanvasObjectId shape = doc.addRect(30, 40, 20, 10, QColor(Qt::red));
    const CanvasObject shapeBefore = *doc.object(shape);
    const quint64 revision = doc.revision();
    const QString cover = doc.addArtboard(QStringLiteral("Cover"), QRectF(0, 0, 100, 80));
    QCOMPARE(cover, QStringLiteral("artboard-1"));
    QCOMPARE(doc.undoText(), QStringLiteral("Add artboard"));
    const QString back = doc.addArtboard(QStringLiteral("Back"), QRectF(120, 0, 100, 80),
                                         QColor(200, 220, 240));
    QCOMPARE(back, QStringLiteral("artboard-2"));
    QCOMPARE(doc.artboardCount(), 2);
    QCOMPARE(doc.revision(), revision + 2);

    QVERIFY(doc.setArtboardRect(cover, QRectF(10, 5, 100, 80)));
    QCOMPARE(doc.undoText(), QStringLiteral("Move artboard"));
    QVERIFY(doc.setArtboardRect(cover, QRectF(10, 5, 140, 90)));
    QCOMPARE(doc.undoText(), QStringLiteral("Resize artboard"));
    QVERIFY(*doc.object(shape) == shapeBefore); // boards never move objects
    QVERIFY(doc.renameArtboard(back, QStringLiteral("Back cover")));
    QCOMPARE(doc.artboard(back)->name, QStringLiteral("Back cover"));
    QVERIFY(doc.setArtboardBleed(cover, 3));
    QCOMPARE(doc.artboard(cover)->exportRect(), QRectF(7, 2, 146, 96));
    QCOMPARE(doc.artboardInfo(cover).value(QStringLiteral("exportRect")).toRectF(),
             QRectF(7, 2, 146, 96));
    QVERIFY(doc.moveArtboard(back, 0));
    QCOMPARE(doc.artboardRecords().constFirst().id, back);
    QCOMPARE(doc.artboardAt(130, 10), cover); // the later board is on top
    QCOMPARE(doc.artboardAt(300, 300), QString());

    const quint64 edited = doc.revision();
    QVERIFY(!doc.setArtboardRect(cover, QRectF(0, 0, 0, 10)));
    QVERIFY(!doc.lastError().isEmpty());
    QVERIFY(!doc.setArtboardBleed(cover, -1));
    QVERIFY(!doc.setArtboardRect(QStringLiteral("missing"), QRectF(0, 0, 1, 1)));
    QVERIFY(!doc.setArtboardRect(cover, QRectF(10, 5, 140, 90))); // unchanged: no step
    QVERIFY(doc.lastError().isEmpty());
    QCOMPARE(doc.revision(), edited);
    QVERIFY(doc.removeArtboard(back));
    QCOMPARE(doc.undoText(), QStringLiteral("Delete artboard"));

    for (int step = 0; step < 8; ++step) QVERIFY(doc.undo());
    QCOMPARE(doc.artboardCount(), 0);
    QVERIFY(*doc.object(shape) == shapeBefore);
    // Ids come from a counter that undo never rewinds.
    QCOMPARE(doc.addArtboard(QStringLiteral("Next"), QRectF(0, 0, 10, 10)), QStringLiteral("artboard-3"));
}

void TstCanvasArtboards::backgroundsPaintBelowObjectsAndExportRegions()
{
    CanvasDocument doc;
    const QString board = doc.addArtboard(QStringLiteral("Board"), QRectF(0, 0, 32, 64),
                                          QColor(0, 128, 0));
    QVERIFY(doc.addRect(16, 16, 32, 32, QColor(Qt::red)) != 0);
    QCOMPARE(doc.backdrops().size(), 1);
    QCOMPARE(pixel(doc, 8, 8), qRgb(0, 128, 0));       // board background
    QCOMPARE(pixel(doc, 56, 8), qRgb(255, 255, 255));  // page outside the board
    QCOMPARE(pixel(doc, 24, 24), qRgb(255, 0, 0));     // objects paint above boards

    // Moving a board repaints its old and new area; the object stays put.
    QVector<QRectF> damage;
    connect(&doc, &CanvasDocumentAdapter::damaged, &doc,
            [&damage](const QVector<QRectF> &regions, quint64) { damage += regions; });
    QVERIFY(doc.setArtboardRect(board, QRectF(32, 0, 32, 64)));
    QVERIFY(damage.contains(QRectF(0, 0, 32, 64)));
    QVERIFY(damage.contains(QRectF(32, 0, 32, 64)));
    QCOMPARE(pixel(doc, 8, 8), qRgb(255, 255, 255));
    QCOMPARE(pixel(doc, 56, 8), qRgb(0, 128, 0));
    QCOMPARE(pixel(doc, 24, 24), qRgb(255, 0, 0));

    // The export region is the board plus its bleed, over its background.
    QVERIFY(doc.setArtboardBleed(board, 2));
    const CanvasArtboard *record = doc.artboard(board);
    QCOMPARE(record->exportRect(), QRectF(30, -2, 36, 68));
    const QByteArray svg = CanvasSvgExport::encodeRegion(doc, record->exportRect(), record->background);
    QVERIFY(svg.contains("viewBox=\"30 -2 36 68\""));
    QVERIFY(svg.contains("fill=\"#008000\""));
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("board.svg"));
    QVERIFY(doc.exportArtboardSvg(board, path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), svg);
    QVERIFY(!doc.exportArtboardSvg(QStringLiteral("missing"), path));
}

void TstCanvasArtboards::sceneSurvivesSessionSaveAndReopen()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DocumentManager manager;
    manager.recoveryManager()->setDirectory(temp.path());
    auto owned = std::make_unique<CanvasDocument>();
    CanvasDocument *document = owned.get();
    DocumentSession *session = manager.create(std::move(owned));
    QVERIFY(session);
    const CanvasLayerId base = document->activeLayer();
    const CanvasObjectId shape = document->addRect(10, 10, 20, 20, QColor(Qt::red));
    const CanvasLayerId group = document->groupLayers({base}, QStringLiteral("Group"));
    const CanvasLayerId nested = document->addLayerAt(QStringLiteral("Nested"), true, group, 0);
    QVERIFY(group && nested);
    const QString cover = document->addArtboard(QStringLiteral("Cover"), QRectF(0, 0, 200, 150),
                                                QColor(250, 240, 230));
    const QString back = document->addArtboard(QString(QChar(0xFEFF)) + QStringLiteral("Back ✓"),
                                               QRectF(220.5, 0, 180.25, 150));
    QVERIFY(document->setArtboardBleed(back, 4.5));
    QVERIFY(document->removeArtboard(cover));
    QVERIFY(document->undo()); // the cover returns; the id counter stays
    // Many boards too: 120 more, each its own step, all saved exactly.
    for (int i = 0; i < 120; ++i)
        QVERIFY(!document->addArtboard(QStringLiteral("Board %1").arg(i),
                                       QRectF(i * 30.25, 400, 25, 40)).isEmpty());
    QCOMPARE(document->artboardCount(), 122);
    session->setExtensions(QJsonObject{{QStringLiteral("host.note"), QStringLiteral("kept")}});
    const QString path = temp.filePath(QStringLiteral("scene.qtkc"));
    QVERIFY(saved(session, path));
    const QVector<CanvasArtboard> boards = document->artboardRecords();
    const QVariantList layers = document->layerList();
    QVERIFY(manager.closeDocument(session->documentId(), DocumentManager::ClosePolicy::Cancel));

    DocumentSession *reopened = manager.openDocument(path);
    QVERIFY2(reopened, qPrintable(manager.lastError()));
    auto *restored = qobject_cast<CanvasDocument *>(reopened->modelObject());
    QVERIFY(restored);
    QVERIFY(restored->artboardRecords() == boards);
    QCOMPARE(restored->layerList(), layers);
    QCOMPARE(restored->layer(nested)->parent, group);
    QVERIFY(restored->layer(group)->group);
    QCOMPARE(restored->object(shape)->layer, base);
    QCOMPARE(restored->artboard(back)->bleed, 4.5);
    // Board and tree records are document content, not host extensions.
    QCOMPARE(reopened->extensions(),
             QJsonObject({{QStringLiteral("host.note"), QStringLiteral("kept")}}));
    QVERIFY(!reopened->isDirty());
    QCOMPARE(restored->addArtboard(QStringLiteral("Next"), QRectF(0, 600, 50, 50)),
             QStringLiteral("artboard-123"));
}

void TstCanvasArtboards::invalidBoardRecordsAreRejected()
{
    CanvasDocumentLimits limits;
    limits.maximumArtboards = 2;
    const QString key = QString::fromLatin1(CanvasDocumentFormat::ArtboardExtensionKey);
    const auto board = [](const QString &id, double width, double bleed, const QString &color) {
        return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), QStringLiteral("B")},
                           {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0},
                           {QStringLiteral("width"), width}, {QStringLiteral("height"), 10},
                           {QStringLiteral("background"), color}, {QStringLiteral("bleed"), bleed}};
    };
    const auto record = [&](const QJsonArray &items, const QJsonValue &next = 3) {
        return QJsonObject{{key, QJsonObject{{QStringLiteral("next"), next},
                                             {QStringLiteral("items"), items}}},
                           {QStringLiteral("host"), 1}};
    };
    CanvasDocumentState state;
    QVERIFY(!CanvasDocumentFormat::takeSavedExtensions(
        record({board("a", 10, 1, "#ff102030")}), limits, &state));
    QCOMPARE(state.artboards.size(), 1);
    QCOMPARE(state.artboards.constFirst().background, QColor(0x10, 0x20, 0x30));
    QCOMPARE(state.artboards.constFirst().exportRect(), QRectF(-1, -1, 12, 12));
    QCOMPARE(state.nextArtboard, quint64(3));
    QCOMPARE(state.extensions, QJsonObject({{QStringLiteral("host"), 1}}));

    const QJsonObject invalid[] = {
        {{key, QStringLiteral("not a record")}},
        record({board("a", 10, 0, "#ffffff")}, 0),
        record({board("a", 0, 0, "#ffffff")}),
        record({board("a", 10, -1, "#ffffff")}),
        record({board("a", 10, 0, "not a colour")}),
        record({board("", 10, 0, "#ffffff")}),
        record({board("a", 10, 0, "#ffffff"), board("a", 5, 0, "#000000")}),
        record({board("a", 10, 0, "#fff"), board("b", 10, 0, "#fff"), board("c", 10, 0, "#fff")}),
    };
    for (const QJsonObject &extensions : invalid) {
        CanvasDocumentState rejected;
        QVERIFY(CanvasDocumentFormat::takeSavedExtensions(extensions, limits, &rejected));
    }
}

void TstCanvasArtboards::artboardModelPreviewsAndCommitsOneStep()
{
    CanvasDocument doc;
    const QString id = doc.addArtboard(QStringLiteral("A"), QRectF(0, 0, 100, 50));
    CanvasArtboardModel model;
    model.setDocument(&doc);
    QVERIFY(model.activate(id));
    QCOMPARE(model.activeBoardId(), id);
    const quint64 revision = doc.revision();

    QVERIFY(model.previewArrange(id, QPointF(10, 10), QPointF(30, 25)));
    QVERIFY(model.previewing());
    QCOMPARE(model.activeRect(), QRectF(20, 15, 100, 50));
    QCOMPARE(model.boards().constFirst().toMap().value(QStringLiteral("rect")).toRectF(),
             QRectF(20, 15, 100, 50));
    QCOMPARE(doc.revision(), revision); // a preview never edits
    QVERIFY(model.finish(id, QPointF(40, 10)));
    QCOMPARE(doc.artboard(id)->rect, QRectF(30, 0, 100, 50));
    QCOMPARE(doc.revision(), revision + 1);
    QCOMPARE(doc.undoText(), QStringLiteral("Move artboard"));

    model.setMinimumSize(8);
    QVERIFY(model.previewResize(id, QPointF(130, 50), QPointF(20, 60)));
    QCOMPARE(model.activeRect(), QRectF(30, 0, 8, 60)); // width floored at the minimum
    model.cancel();
    QCOMPARE(model.activeRect(), QRectF(30, 0, 100, 50));
    QCOMPARE(doc.revision(), revision + 1);

    // A board list changed elsewhere (here: undo) drops the running preview.
    QVERIFY(model.previewResize(id, QPointF(130, 50), QPointF(140, 70)));
    QVERIFY(doc.undo());
    QVERIFY(!model.previewing());
    QVERIFY(!model.finish(id, QPointF(140, 70)));
    QCOMPARE(doc.artboard(id)->rect, QRectF(0, 0, 100, 50));

    const QString created = model.createBoard(QRectF(200, 100, -50, -40));
    QVERIFY(!created.isEmpty());
    QCOMPARE(doc.artboard(created)->rect, QRectF(150, 60, 50, 40));
    QCOMPARE(doc.artboard(created)->name, QStringLiteral("Artboard 2"));
    QCOMPARE(model.activeBoardId(), created);
    QVERIFY(model.createBoard(QRectF(0, 0, 0, 10)).isEmpty());
    QVERIFY(!model.lastError().isEmpty());
    QCOMPARE(model.boardAt(QPointF(160, 70)), created);
    QVERIFY(model.removeActive());
    QVERIFY(model.activeBoardId().isEmpty());
    QCOMPARE(doc.artboardCount(), 1);
}

void TstCanvasArtboards::alignmentUsesTheArtboardReference()
{
    CanvasDocument doc;
    const CanvasObjectId a = doc.addRect(20, 20, 10, 10, QColor(Qt::red));
    const CanvasObjectId b = doc.addRect(60, 40, 20, 10, QColor(Qt::blue));
    const CanvasObject beforeA = *doc.object(a);
    const CanvasObject beforeB = *doc.object(b);
    CanvasSelectionModel selection;
    selection.setDocument(&doc);
    CanvasAlignmentModel alignment;
    alignment.setDocument(&doc);
    alignment.setSelectionModel(&selection);
    selection.setSelection({a, b});
    QCOMPARE(alignment.selectedIds(), (QStringList{QString::number(a), QString::number(b)}));
    QCOMPARE(alignment.referenceOptions().size(), 3); // no boards: no board reference

    const QString board = doc.addArtboard(QStringLiteral("Board"), QRectF(100, 100, 200, 100));
    const QVariantList options = alignment.referenceOptions();
    QCOMPARE(options.size(), 4);
    QCOMPARE(options.constLast().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("artboard"));
    QVERIFY(!options.constLast().toMap().value(QStringLiteral("available")).toBool());
    const QVariantList ids{QString::number(a), QString::number(b)};
    QVERIFY(!alignment.apply(QStringLiteral("align.left"), ids, QStringLiteral("artboard")));
    alignment.setArtboardId(board);
    QVERIFY(alignment.referenceOptions().constLast().toMap().value(QStringLiteral("available")).toBool());
    QVERIFY(alignment.apply(QStringLiteral("align.left"), ids, QStringLiteral("artboard")));
    QCOMPARE(doc.objectInfo(a).value(QStringLiteral("bounds")).toRectF().left(), 100.0);
    QCOMPARE(doc.objectInfo(b).value(QStringLiteral("bounds")).toRectF().left(), 100.0);
    QCOMPARE(doc.undoText(), QStringLiteral("Align objects"));
    // One object may align to a board, like to the page.
    QVERIFY(alignment.apply(QStringLiteral("align.bottom"), {QString::number(a)}, QStringLiteral("artboard")));
    QCOMPARE(doc.objectInfo(a).value(QStringLiteral("bounds")).toRectF().bottom(), 200.0);
    QVERIFY(doc.undo());
    QVERIFY(doc.undo());
    QVERIFY(*doc.object(a) == beforeA);
    QVERIFY(*doc.object(b) == beforeB);
}

void TstCanvasArtboards::fittedViewKeepsExternalNavigation()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 400, 300));
    QQuickWindow window;
    CanvasSurface view(window.contentItem());
    view.setSize(QSizeF(300, 200));
    view.setDocument(&doc);
    view.setFitMode(QStringLiteral("page"));
    QCOMPARE(view.pose().center, QPointF(200, 150));

    // A NavigatorView (or a synchronized view) moves the shared pose.
    QSignalSpy poses(view.viewState(), &CanvasViewState::changed);
    view.viewState()->setCenter(320, 60);
    QCOMPARE(poses.count(), 1); // both coordinates in one change
    QVERIFY(doc.addRect(0, 0, 10, 10, QColor(Qt::red)) != 0);
    QCOMPARE(view.pose().center, QPointF(320, 60)); // an edit no longer refits over it
    view.setSize(QSizeF(320, 220));
    QCOMPARE(view.pose().center, QPointF(320, 60));

    // An explicit fit resumes fitting; a limit change alone is not navigation.
    view.fitDocument();
    QCOMPARE(view.pose().center, QPointF(200, 150));
    view.viewState()->setMinZoom(view.viewState()->minZoom() / 2);
    doc.setPageRect(QRectF(0, 0, 800, 300));
    QCOMPARE(view.pose().center, QPointF(400, 150));
}

QTEST_MAIN(TstCanvasArtboards)
#include "tst_canvas_artboards.moc"
