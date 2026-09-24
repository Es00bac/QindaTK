// SPDX-License-Identifier: LGPL-3.0-or-later
// ENG-03 combined graphics qualification: tiled raster strokes and masks
// (WP-045) placed as canvas image objects (WP-040) over a vector path
// (WP-044), transformed, compared, undone, persisted and converted to an
// ICC output profile (WP-043). Probes are known answers, not re-derivations.
#include "../../src/core/canvas_document.h"
#include "../../src/core/canvas_document_format.h"
#include "../../src/core/canvas_render.h"
#include "../../src/core/image_compare_item.h"
#include "../../src/core/tiled_raster_document.h"

#include <QColorSpace>
#include <QFile>
#include <QtTest>
#include <cmath>

using namespace QindaTK;

namespace {
const QColor Gray(128, 128, 128);
const QColor Vector(200, 40, 40);

QVariantMap hardBrush(qreal diameter)
{
    return {{QStringLiteral("diameter"), diameter}, {QStringLiteral("hardness"), 1.0},
            {QStringLiteral("spacing"), 0.1}, {QStringLiteral("opacity"), 1.0},
            {QStringLiteral("pressureSize"), 0.0}, {QStringLiteral("pressureOpacity"), 0.0}};
}

// One device pixel per document unit; the probe's unit square is pixel (32, 32).
QColor probe(const CanvasDocumentAdapter &doc, const QPointF &point, const CanvasColorContext &color = {})
{
    const QRectF rect(std::floor(point.x()) - 32, std::floor(point.y()) - 32, 64, 64);
    return CanvasPainter::renderTile(doc, rect, 64, color).pixelColor(32, 32);
}

bool stroke(TiledRasterDocument &raster, bool mask, const QColor &color, qreal diameter,
            const QPointF &from, const QPointF &to)
{
    return raster.beginStroke(QStringLiteral("ink"), mask, color, hardBrush(diameter))
        && raster.sampleStroke(from.x(), from.y()) && raster.sampleStroke(to.x(), to.y())
        && raster.endStroke();
}
} // namespace

class TstRasterCanvas final : public QObject {
    Q_OBJECT
private slots:
    void graphicsSliceComposesTransformsUndoesAndOutputs();
    void placementAndPublishStayBounded();
};

void TstRasterCanvas::graphicsSliceComposesTransformsUndoesAndOutputs()
{
    TiledRasterDocument raster;
    QVERIFY(raster.resizeCanvas(300, 120));
    QVERIFY(raster.createLayer(QStringLiteral("ink"), QStringLiteral("Ink")));
    QVERIFY(raster.addMask(QStringLiteral("ink")));
    // A hard 20 px sRGB-gray stroke across the 256 px tile seam, then a hard
    // 12 px mask dab that removes its coverage around raster (230, 60).
    QVERIFY(stroke(raster, false, Gray, 20, {200, 60}, {290, 60}));
    QVERIFY(stroke(raster, true, Qt::black, 12, {230, 60}, {230, 60}));
    const QImage flat = raster.composite();
    QCOMPARE(flat.pixelColor(250, 60), Gray);
    QCOMPARE(flat.pixelColor(262, 60), Gray);
    QCOMPARE(flat.pixelColor(230, 60).alpha(), 0);

    CanvasDocument canvas;
    canvas.setPageRect(QRectF(0, 0, 800, 600));
    const QVariantList square{QVariantMap{{"x", 250}, {"y", 80}}, QVariantMap{{"x", 320}, {"y", 80}},
                              QVariantMap{{"x", 320}, {"y", 140}}, QVariantMap{{"x", 250}, {"y", 140}}};
    const qulonglong path = canvas.addPath(square, true, Vector, {}, 0);
    QVERIFY(path);

    // Placement is one canvas step; raster (x, y) lands at document (x + 50, y + 50).
    const qulonglong placed = raster.placeOnCanvas(&canvas, 50, 50);
    QVERIFY2(placed, qPrintable(raster.error()));
    QCOMPARE(canvas.imageCount(), 1);
    QCOMPARE(canvas.undoText(), QStringLiteral("Place raster"));
    QCOMPARE(probe(canvas, {300, 110}), Gray);          // raster (250, 60), over the path
    QCOMPARE(probe(canvas, {312, 110}), Gray);          // raster (262, 60), past the seam
    QCOMPARE(probe(canvas, {280, 110}), Vector);        // the mask hole reveals the path
    QCOMPARE(probe(canvas, {20, 20}), QColor(Qt::white));

    // Rotate 90 degrees and scale 2x: raster (x, y) -> document (500 - 2y, 40 + 2x).
    QVERIFY(canvas.setObjectTransform(placed, {0, 2, -2, 0, 500, 40}));
    QCOMPARE(probe(canvas, {380, 540}), Gray);          // raster (250, 60)
    QCOMPARE(probe(canvas, {380, 564}), Gray);          // raster (262, 60)
    QCOMPARE(probe(canvas, {380, 500}), QColor(Qt::white)); // masked raster (230, 60)
    QCOMPARE(probe(canvas, {300, 110}), Vector);        // nothing left at the old place
    const QPointF pixel = raster.canvasToRaster(&canvas, placed, 380, 540);
    QVERIFY(std::abs(pixel.x() - 250) < 1e-9 && std::abs(pixel.y() - 60) < 1e-9);
    QVERIFY(canvas.undo());
    QCOMPARE(probe(canvas, {300, 110}), Gray);
    QVERIFY(canvas.redo());

    // A later raster stroke inside tile 0 publishes as one step that shares tile 1.
    const QRectF region(400, 64, 128, 128);
    const QImage beforeRender = CanvasPainter::renderTile(canvas, region, 128, {});
    const CanvasTiledImagePtr before = canvas.image(canvas.object(placed)->image);
    QVERIFY(stroke(raster, false, Qt::blue, 10, {20, 20}, {60, 20}));
    QVERIFY2(raster.publishToCanvas(&canvas, placed), qPrintable(raster.error()));
    QCOMPARE(canvas.undoText(), QStringLiteral("Update raster pixels"));
    const CanvasTiledImagePtr after = canvas.image(canvas.object(placed)->image);
    QVERIFY(after != before);
    QVERIFY(after->tiles.at(0).cacheKey() != before->tiles.at(0).cacheKey());
    QCOMPARE(after->tiles.at(1).cacheKey(), before->tiles.at(1).cacheKey());
    QCOMPARE(probe(canvas, {460, 120}), QColor(Qt::blue)); // raster (40, 20)
    const quint64 published = canvas.revision();
    QVERIFY(raster.publishToCanvas(&canvas, placed));      // identical pixels: no edit
    QCOMPARE(canvas.revision(), published);

    // CompareView math on the two renders isolates exactly the new stroke.
    ImageCompareItem compare;
    QVERIFY(compare.setImages(beforeRender, CanvasPainter::renderTile(canvas, region, 128, {})));
    compare.setMode(QStringLiteral("difference"));
    const QImage difference = compare.renderImage(QSize(128, 128));
    QCOMPARE(difference.pixelColor(60, 56), QColor(255, 255, 0)); // white vs blue at (460, 120)
    QCOMPARE(difference.pixelColor(20, 36), QColor(0, 0, 0));     // untouched (420, 100)

    // Canvas undo walks back publish, transform and placement; redo restores all.
    QVERIFY(canvas.undo());
    QCOMPARE(probe(canvas, {460, 120}), QColor(Qt::white));
    QVERIFY(canvas.undo());
    QVERIFY(canvas.undo());
    QCOMPARE(canvas.imageCount(), 0);
    QVERIFY(!canvas.object(placed));
    QCOMPARE(probe(canvas, {300, 110}), Vector);
    QVERIFY(canvas.redo() && canvas.redo() && canvas.redo());
    QCOMPARE(probe(canvas, {460, 120}), QColor(Qt::blue));

    // Output color: the composed tile converts to the document's ICC output
    // (the WP-043 fixture encodes sRGB 128 gray as 55).
    QFile fixture(QStringLiteral(QINDATK_COLOR_FIXTURES) + QStringLiteral("/linear-srgb.icc"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const QByteArray icc = fixture.readAll();
    QVERIFY(canvas.setOutputProfileIcc(icc));
    CanvasColorContext output;
    output.display = QColorSpace::fromIccProfile(icc);
    const QColor encoded = probe(canvas, {380, 540}, output);
    QVERIFY2(std::abs(encoded.red() - 55) <= 4 && encoded.red() == encoded.blue(),
             qPrintable(encoded.name()));

    // The canvas file keeps the published pixels and output profile; the
    // reopened document is still a publish target with the same pixels.
    OperationError error;
    const auto state = CanvasDocumentFormat::decode(CanvasDocumentFormat::encode(canvas.state()), {}, &error);
    QVERIFY2(state.has_value(), qPrintable(error.message));
    const std::unique_ptr<CanvasDocument> reopened = CanvasDocument::fromState(*state);
    QVERIFY(reopened);
    QCOMPARE(reopened->outputProfileIcc(), icc);
    QCOMPARE(probe(*reopened, {380, 540}), Gray);
    QCOMPARE(probe(*reopened, {460, 120}), QColor(Qt::blue));
    const quint64 reopenedRevision = reopened->revision();
    QVERIFY(raster.publishToCanvas(reopened.get(), placed));
    QCOMPARE(reopened->revision(), reopenedRevision);
}

void TstRasterCanvas::placementAndPublishStayBounded()
{
    TiledRasterDocument raster;
    QVERIFY(raster.resizeCanvas(300, 120));
    QVERIFY(raster.createLayer(QStringLiteral("ink"), QStringLiteral("Ink")));

    // The canvas image budget rejects the whole placement: no image, object or step.
    CanvasDocument small;
    CanvasDocumentLimits limits = small.limits();
    limits.maximumImageBytes = 64 * 1024;
    small.setLimits(limits);
    QCOMPARE(raster.placeOnCanvas(&small, 0, 0), 0ULL);
    QVERIFY(!raster.error().isEmpty());
    QCOMPARE(small.imageCount(), 0);
    QCOMPARE(small.objectCount(), 0);
    QVERIFY(!small.canUndo());

    CanvasDocument canvas;
    const qulonglong rect = canvas.addRect(0, 0, 10, 10, Qt::red);
    QVERIFY(!raster.publishToCanvas(&canvas, rect));   // not an image object
    QVERIFY(!raster.publishToCanvas(nullptr, rect));
    QVERIFY(!raster.placeOnCanvas(&canvas, qQNaN(), 0));
    const qulonglong placed = raster.placeOnCanvas(&canvas, 0, 0);
    QVERIFY(placed);
    QVERIFY(raster.beginStroke(QStringLiteral("ink"), false, Qt::red, hardBrush(8)));
    QVERIFY(!raster.publishToCanvas(&canvas, placed)); // never publishes a stroke in flight
    raster.cancelStroke();
    QVERIFY(std::isnan(raster.canvasToRaster(&canvas, rect, 1, 1).x()));

    TiledRasterDocument other;
    QVERIFY(other.resizeCanvas(64, 64));
    QVERIFY(!other.publishToCanvas(&canvas, placed));  // a different pixel size
}

QTEST_MAIN(TstRasterCanvas)
#include "tst_raster_canvas.moc"
