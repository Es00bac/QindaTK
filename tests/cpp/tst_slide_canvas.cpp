// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-056 / OFF-19: SlideCanvas on the WP-042 canvas stack. The
// SlideCanvasDocument adapter shows a slide through CanvasDocumentAdapter
// with the output painter's pixels, and canvas gestures (SpatialTransform
// resize/rotate with SnapModel, select-tool moves, removals) reach the
// deck as one SlideDeck undo step each (docs/slides.md#canvas).
#include "canvas_render.h"
#include "canvas_snap_model.h"
#include "canvas_transform_controller.h"
#include "canvas_view_state.h"
#include "rich_text_document.h"
#include "slide_canvas_document.h"
#include "slide_deck.h"
#include "tool_controller.h"
#include "undo_service.h"

#include <QPainter>
#include <QtTest>
#include <cmath>

using namespace QindaTK;

namespace {
QVariantMap box(double x, double y, double w, double h)
{
    return {{"x", x}, {"y", y}, {"width", w}, {"height", h}};
}
QVariantMap objectWithId(const SlideDeck &deck, const QString &slideId, const QString &id)
{
    for (const QVariant &v : deck.objects(slideId))
        if (v.toMap().value("id").toString() == id) return v.toMap();
    return {};
}
QRectF boundsOf(const QVariantMap &object)
{
    const QVariantMap b = object.value("bounds").toMap();
    return {b.value("x").toDouble(), b.value("y").toDouble(), b.value("width").toDouble(), b.value("height").toDouble()};
}
// Mean absolute channel difference of two same-sized images (0-255).
double meanDifference(const QImage &a, const QImage &b)
{
    const QImage x = a.convertToFormat(QImage::Format_RGB32), y = b.convertToFormat(QImage::Format_RGB32);
    if (x.size() != y.size()) return 255;
    double sum = 0;
    for (int j = 0; j < x.height(); ++j)
        for (int i = 0; i < x.width(); ++i) {
            const QRgb p = x.pixel(i, j), q = y.pixel(i, j);
            sum += std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) + std::abs(qBlue(p) - qBlue(q));
        }
    return sum / (3.0 * x.width() * x.height());
}
struct Fixture {
    SlideDeck deck;
    SlideCanvasDocument canvas;
    ToolController tools;
    CanvasSnapModel snap;
    CanvasTransformController transform;
    CanvasViewState view; // zoom 1: snap tolerance is 8 slide px
    QString slide, shape, text;
    Fixture()
    {
        slide = deck.slideIds().first();
        shape = deck.insertObject(slide, "shape", box(100, 300, 200, 100));
        text = deck.insertObject(slide, "text", box(400, 300, 300, 80), QStringLiteral("Body"));
        canvas.setDeck(&deck);
        canvas.setSlideId(slide);
        tools.setDocument(&canvas);
        snap.setDocument(&canvas);
        snap.setViewState(&view);
        snap.setGuides(canvas.guides());
        transform.setDocument(&canvas);
        transform.setTools(&tools);
        transform.setSnapModel(&snap);
    }
    CanvasObjectId id(const QString &slideObjectId) const { return canvas.canvasObjectId(slideObjectId); }
};
} // namespace

class SlideCanvasTest : public QObject {
    Q_OBJECT
private slots:
    void adapterShowsTheResolvedSlide()
    {
        Fixture f;
        QCOMPARE(f.canvas.objectCount(), 4); // title, subtitle, shape, text
        QCOMPARE(f.canvas.pageRect(), QRectF(0, 0, 960, 540));
        QVERIFY(f.deck.setMasterBackground("master-default", "#102030", "#f0f0f0"));
        QCOMPARE(f.canvas.pageColor(), QColor("#102030"));
        const CanvasObject *shape = f.canvas.object(f.id(f.shape));
        QVERIFY(shape);
        QCOMPARE(shape->shape, CanvasShape::Image);
        QCOMPARE(shape->transform.mapRect(shape->rect), QRectF(100, 300, 200, 100));
        QCOMPARE(f.canvas.slideObjectId(shape->id), f.shape);
        QCOMPARE(f.canvas.guides().size(), 2);
        // Unchanged objects keep their rendered image across edits.
        const int renders = f.canvas.imageRenders();
        QVERIFY(f.deck.moveObject(f.shape, box(120, 300, 200, 100)));
        QCOMPARE(f.canvas.imageRenders(), renders);
        QVERIFY(f.deck.setObjectFill(f.shape, "#ff0000"));
        QCOMPARE(f.canvas.imageRenders(), renders + 1);
    }

    void canvasPixelsMatchTheAudienceOutput()
    {
        Fixture f;
        QVERIFY(f.deck.setMasterBackground("master-default", "#10202c", "#eef6fa"));
        QVERIFY(f.deck.setPlaceholderDefault("master-default", "title", QStringLiteral("Signals in Motion")));
        QVERIFY(f.deck.setObjectFill(f.shape, "#16c7d8"));
        QVERIFY(f.deck.transformObjects({QVariantMap{{"id", f.shape}, {"bounds", box(100, 300, 200, 100)}, {"rotation", 20.0}}}));
        RichTextDocument *document = f.deck.beginTextEdit(f.text);
        QVERIFY(document);
        QVERIFY(document->insertText(document->endPosition(), QStringLiteral(" text with more words")));
        QVERIFY(f.deck.commitTextEdit());
        f.canvas.setShowPrompts(false); // the only canvas-only pixels
        const QSize size(480, 270);
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(f.canvas.pageColor());
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.scale(0.5, 0.5);
        CanvasPainter::paint(&painter, f.canvas, f.canvas.pageRect(), 0.5);
        painter.end();
        const QImage output = f.deck.renderFrame(f.slide, -1, "0", size.width(), size.height());
        // Same painter code: only resampling of the 2x object images differs.
        const double difference = meanDifference(image, output);
        QVERIFY2(difference < 3.0, qPrintable(QString::number(difference)));
        // A theme-coloured page with plain labels (the old canvas) would not match.
        QImage wrong(size, QImage::Format_RGB32);
        wrong.fill(Qt::white);
        QVERIFY(meanDifference(wrong, output) > 50);
    }

    void resizeAndRotateAreOneDeckStepEach()
    {
        Fixture f;
        f.tools.setSelection({f.id(f.shape)});
        QCOMPARE(f.transform.selectionBounds(), QRectF(100, 300, 200, 100));
        // Drag the bottom-right handle by (+100, +50): the document stays put
        // until finish, then one step records the new bounds.
        QVERIFY(f.transform.update("bottomRight", QPointF(300, 400), QPointF(350, 440)));
        QCOMPARE(boundsOf(objectWithId(f.deck, f.slide, f.shape)), QRectF(100, 300, 200, 100));
        QVERIFY(f.transform.update("bottomRight", QPointF(300, 400), QPointF(400, 450)));
        QVERIFY(f.transform.finish("bottomRight"));
        QCOMPARE(boundsOf(objectWithId(f.deck, f.slide, f.shape)), QRectF(100, 300, 300, 150));
        QCOMPARE(f.deck.undoService()->undoText(&f.deck), QStringLiteral("Resize selection"));

        QVERIFY(f.transform.rotateBy(90));
        const QVariantMap rotated = objectWithId(f.deck, f.slide, f.shape);
        QCOMPARE(rotated.value("rotation").toDouble(), 90.0);
        const QRectF r = boundsOf(rotated);
        QVERIFY(std::abs(r.center().x() - 250) < 1e-6 && std::abs(r.center().y() - 375) < 1e-6);
        QVERIFY(std::abs(r.width() - 300) < 1e-6 && std::abs(r.height() - 150) < 1e-6);
        QVERIFY(f.deck.undo());
        QCOMPARE(objectWithId(f.deck, f.slide, f.shape).value("rotation").toDouble(), 0.0);
        QVERIFY(f.deck.undo());
        QCOMPARE(boundsOf(objectWithId(f.deck, f.slide, f.shape)), QRectF(100, 300, 200, 100));
    }

    void movesSnapToTheSlideCentre()
    {
        Fixture f;
        f.tools.setSelection({f.id(f.shape)});
        // The pointer lands 3 px from the vertical centre guide (x = 480).
        QVERIFY(f.transform.update("move", QPointF(200, 350), QPointF(477, 350)));
        QVERIFY(f.transform.finish("move"));
        QCOMPARE(boundsOf(objectWithId(f.deck, f.slide, f.shape)).x(), 380.0);
        f.snap.setEnabled(false);
        QVERIFY(f.transform.update("move", QPointF(480, 350), QPointF(477, 350)));
        QVERIFY(f.transform.finish("move"));
        QCOMPARE(boundsOf(objectWithId(f.deck, f.slide, f.shape)).x(), 377.0);
    }

    void groupEditsAndRemovalsAreOneStep()
    {
        Fixture f;
        const QString effect = f.deck.addEffect(f.text, "fade-in");
        QVERIFY(!effect.isEmpty());
        // A two-object move built like the select tool's edit.
        CanvasEdit move;
        move.label = QStringLiteral("Move");
        for (const QString &id : {f.shape, f.text}) {
            const CanvasObject before = *f.canvas.object(f.id(id));
            CanvasObject after = before;
            after.transform = before.transform * QTransform::fromTranslate(10, -20);
            move.changes.append({before.id, before, after});
        }
        QVERIFY(f.canvas.commit(move));
        QCOMPARE(boundsOf(objectWithId(f.deck, f.slide, f.shape)), QRectF(110, 280, 200, 100));
        QCOMPARE(boundsOf(objectWithId(f.deck, f.slide, f.text)), QRectF(410, 280, 300, 80));
        QCOMPARE(f.deck.undoService()->undoText(&f.deck), QStringLiteral("Move"));
        QVERIFY(f.deck.undo());
        QCOMPARE(boundsOf(objectWithId(f.deck, f.slide, f.shape)), QRectF(100, 300, 200, 100));
        QCOMPARE(boundsOf(objectWithId(f.deck, f.slide, f.text)), QRectF(400, 300, 300, 80));

        // An edit whose "before" no longer matches the slide is refused as
        // stale (the adapter compares content, like CanvasDocument).
        QVERIFY(f.deck.moveObject(f.shape, box(90, 300, 200, 100)));
        QVERIFY(!f.canvas.commit(move));
        QCOMPARE(boundsOf(objectWithId(f.deck, f.slide, f.text)), QRectF(400, 300, 300, 80));
        QVERIFY(f.deck.undo());
        // Inserts go through the deck, not the canvas.
        CanvasEdit insert;
        CanvasObject fresh = *f.canvas.object(f.id(f.shape));
        fresh.id = f.canvas.reserveObjectId();
        insert.changes.append({fresh.id, std::nullopt, fresh});
        QVERIFY(!f.canvas.commit(insert));

        // Removing through the canvas deletes the object and its effects.
        CanvasEdit remove;
        remove.changes.append({f.id(f.text), *f.canvas.object(f.id(f.text)), std::nullopt});
        QVERIFY(f.canvas.commit(remove));
        QVERIFY(objectWithId(f.deck, f.slide, f.text).isEmpty());
        QVERIFY(f.deck.effects(f.slide).isEmpty());
        QCOMPARE(f.canvas.objectCount(), 3);
        QVERIFY(f.deck.removeObjects({f.shape}));
        QCOMPARE(f.canvas.objectCount(), 2);
        QVERIFY(f.deck.undo() && f.deck.undo());
        QCOMPARE(f.canvas.objectCount(), 4);
        // Undo restores the same canvas identity.
        QCOMPARE(f.canvas.slideObjectId(f.id(f.text)), f.text);
    }

    void lockedObjectsCannotBeTransformed()
    {
        Fixture f;
        QVERIFY(f.deck.fromJson(f.deck.toJson().replace(QStringLiteral("\"locked\":false,\"own\":[],\"role\":\"\",\"text\""),
                                                        QStringLiteral("\"locked\":true,\"own\":[],\"role\":\"\",\"text\""))));
        const CanvasObject *shape = f.canvas.object(f.id(f.shape));
        QVERIFY(shape && shape->locked);
        f.tools.setSelection({shape->id});
        QVERIFY(!f.transform.update("move", QPointF(150, 350), QPointF(200, 350)));
        QVERIFY(!f.deck.removeObjects({f.shape}));
    }
};

QTEST_MAIN(SlideCanvasTest)
#include "tst_slide_canvas.moc"
