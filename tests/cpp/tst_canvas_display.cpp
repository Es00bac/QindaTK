// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_surface.h"
#include "canvas_style_model.h"
#include "canvas_document_format.h"

#include <QFile>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <cmath>

using namespace QindaTK;

namespace {
CanvasObjectId box(CanvasDocument &doc, QRectF rect, QColor fill)
{
    CanvasObject object;
    object.shape = CanvasShape::Rect;
    object.rect = QRectF(QPointF(0, 0), rect.size());
    object.transform = QTransform::fromTranslate(rect.x(), rect.y());
    object.fill = fill;
    return doc.insert(object, false);
}
QImage grabImage(QQuickItem *item)
{
    const auto grab = item->grabToImage();
    if (!grab) return {};
    QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
    if (grab->image().isNull() && !ready.wait(5000)) return {};
    return grab->image();
}
QColor itemPixelAt(QQuickItem *item, QPointF point)
{
    const QImage image = grabImage(item);
    if (image.isNull()) return {};
    const qreal dpr = image.devicePixelRatio();
    return image.pixelColor(int(point.x() * dpr), int(point.y() * dpr));
}
}

class TstCanvasDisplay : public QObject {
    Q_OBJECT
private slots:
    void softwarePixelsKnownAnswer();
    void managedDisplayTransform();
    void managedDisplaySceneGraph();
    void typedOutputProfileRhi();
};

void TstCanvasDisplay::softwarePixelsKnownAnswer()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 100, 100));
    box(doc, QRectF(10, 10, 20, 20), Qt::red);
    const CanvasObjectId hidden = box(doc, QRectF(50, 50, 20, 20), Qt::green);
    QVERIFY(doc.setObjectVisible(hidden, false));
    const CanvasObjectId flat = box(doc, QRectF(50, 10, 20, 20), Qt::blue);
    QVERIFY(doc.setObjectTransform(flat, {0.0, 0.0, 0.0, 0.0, 50.0, 10.0}));
    const QImage tile = CanvasPainter::renderTile(doc, QRectF(0, 0, 128, 128), 128, {});
    QCOMPARE(tile.pixelColor(20, 20), QColor(Qt::red));
    QCOMPARE(tile.pixelColor(5, 5), QColor(Qt::white));
    QCOMPARE(tile.pixelColor(60, 60), QColor(Qt::white));
    QCOMPARE(tile.pixelColor(51, 11), QColor(Qt::white));
    QCOMPARE(tile.pixelColor(110, 110).alpha(), 0);
    const QImage fine = CanvasPainter::renderTile(doc, QRectF(16, 16, 8, 8), 32, {});
    QCOMPARE(fine.pixelColor(16, 16), QColor(Qt::red));
    CanvasPaintStats stats;
    const QImage coarse = CanvasPainter::renderTile(doc, QRectF(0, 0, 4096, 4096), 16, {}, &stats);
    QCOMPARE(coarse.width(), 16);
    QCOMPARE(stats.dots, 1);
    QCOMPARE(stats.skipped, 1);
}

void TstCanvasDisplay::managedDisplayTransform()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 64, 64));
    doc.setPageColor(QColor(128, 128, 128));
    CanvasColorContext context;
    context.display = QColorSpace(QColorSpace::SRgbLinear);
    QVERIFY(context.validationError().isEmpty());
    const QImage tile = CanvasPainter::renderTile(doc, QRectF(0, 0, 64, 64), 64, context);
    QVERIFY(!tile.isNull());
    QCOMPARE(tile.colorSpace(), context.display);
    const QColor pixel = tile.pixelColor(20, 20);
    QVERIFY2(std::abs(pixel.red() - 55) <= 2, qPrintable(QString::number(pixel.red())));
    QCOMPARE(pixel.red(), pixel.green());

    RenderSurfaceAdapter adapter;
    adapter.setDocument(&doc);
    CanvasRenderRequest request;
    request.viewSize = QSizeF(64, 64);
    adapter.setColorContext(context);
    adapter.setSceneGraphAvailable(true, {});
    adapter.setRequestedBackend(QStringLiteral("scenegraph"));
    auto frame = adapter.prepare(request);
    QVERIFY(!frame->items.isEmpty());
    QVERIFY(!frame->items.first().content->raster.isNull());
    QCOMPARE(frame->items.first().content->raster.colorSpace(), context.display);
    QVERIFY(adapter.colorError().isEmpty());

    context.working = QColorSpace(QColorSpace::SRgbLinear);
    adapter.setColorContext(context);
    frame = adapter.prepare(request);
    QVERIFY(!adapter.colorError().isEmpty());
    QVERIFY(frame->items.isEmpty());
    QCOMPARE(adapter.cachedTiles(), 0);
    QVERIFY(CanvasPainter::renderTile(doc, QRectF(0, 0, 64, 64), 64, context).isNull());
}

void TstCanvasDisplay::managedDisplaySceneGraph()
{
    if (qEnvironmentVariable("QT_QUICK_BACKEND") != QLatin1String("rhi"))
        QSKIP("managed scene graph readback runs only on the RHI registration");
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 100, 100));
    doc.setPageColor(QColor(128, 128, 128));
    QQuickWindow window;
    window.resize(300, 300);
    CanvasSurface view(window.contentItem());
    view.setSize(QSizeF(300, 300));
    view.setDocument(&doc);
    view.setFitMode(QStringLiteral("page"));
    CanvasColorContext context;
    context.display = QColorSpace(QColorSpace::SRgbLinear);
    view.renderSurface()->setColorContext(context);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software
        || window.rendererInterface()->graphicsApi() == QSGRendererInterface::Null)
        QSKIP("no hardware-backed RHI available");
    const QPointF probe(50, 50);
    QTRY_COMPARE(view.renderSurface()->effectiveBackend(), QStringLiteral("scenegraph"));
    QTRY_VERIFY(std::abs(itemPixelAt(&view, view.mapFromDocument(probe)).red() - 55) <= 4);
    const QColor gpu = itemPixelAt(&view, view.mapFromDocument(probe));
    const QString capturePath = qEnvironmentVariable("QINDATK_DISPLAY_CAPTURE");
    if (!capturePath.isEmpty())
        QVERIFY(grabImage(&view).save(capturePath + QStringLiteral("-scenegraph.png")));
    view.setBackend(QStringLiteral("software"));
    QTRY_VERIFY(std::abs(itemPixelAt(&view, view.mapFromDocument(probe)).red() - gpu.red()) <= 2);
    if (!capturePath.isEmpty())
        QVERIFY(grabImage(&view).save(capturePath + QStringLiteral("-software.png")));
    QVERIFY(view.renderSurface()->colorError().isEmpty());
}

void TstCanvasDisplay::typedOutputProfileRhi()
{
    if (qEnvironmentVariable("QT_QUICK_BACKEND") != QLatin1String("rhi"))
        QSKIP("typed output readback runs on the RHI registration");
    QFile fixture(QStringLiteral(QINDATK_COLOR_FIXTURES) + QStringLiteral("/linear-srgb.icc"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const QByteArray icc = fixture.readAll();
    CanvasDocument doc;
    doc.setPageRect(QRectF(0,0,100,100));
    QVERIFY(doc.setOutputProfileIcc(icc));
    const CanvasObjectId target = box(doc, QRectF(0,0,100,100), Qt::white);
    ColorPickerModel picker;
    QVERIFY(picker.previewSrgb(QColor(128,128,128)));
    QVERIFY(picker.commitEdit());
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    QVERIFY(styles.applyPicker({QString::number(target)}, &picker));
    OperationError error;
    const auto reopened = CanvasDocumentFormat::decode(CanvasDocumentFormat::encode(doc.state()), {}, &error);
    QVERIFY2(reopened.has_value(), qPrintable(error.message));
    QCOMPARE(reopened->outputProfileIcc, icc);
    QCOMPARE(reopened->objects.value(target).managedFill, doc.object(target)->managedFill);

    CanvasColorContext context;
    context.display = QColorSpace::fromIccProfile(icc);
    const QImage reference = CanvasPainter::renderTile(doc, QRectF(0,0,100,100), 100, context);
    QVERIFY(!reference.isNull());
    QVERIFY(std::abs(reference.pixelColor(50,50).red() - 55) <= 4);
    QQuickWindow window;
    window.resize(300,300);
    CanvasSurface view(window.contentItem());
    view.setSize(QSizeF(300,300));
    view.setDocument(&doc);
    view.setFitMode(QStringLiteral("page"));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software
        || window.rendererInterface()->graphicsApi() == QSGRendererInterface::Null)
        QSKIP("no hardware-backed RHI available");
    QTRY_COMPARE(view.renderSurface()->effectiveBackend(), QStringLiteral("scenegraph"));
    const QPointF probe = view.mapFromDocument(QPointF(50,50));
    QTRY_VERIFY(itemPixelAt(&view, probe).isValid());
    const QColor gpu = itemPixelAt(&view, probe);
    QVERIFY(std::abs(gpu.red() - reference.pixelColor(50,50).red()) <= 4);
    const QString capture = qEnvironmentVariable("QINDATK_WP043_CAPTURE");
    if (!capture.isEmpty()) QVERIFY(grabImage(&view).save(capture + QStringLiteral("-rhi.png")));
    view.setBackend(QStringLiteral("software"));
    QTRY_VERIFY(std::abs(itemPixelAt(&view, probe).red() - gpu.red()) <= 2);
    if (!capture.isEmpty()) QVERIFY(grabImage(&view).save(capture + QStringLiteral("-software.png")));
    CanvasColorContext unsupported;
    unsupported.intent = ColorIntent::Perceptual;
    QVERIFY(unsupported.validationError().contains("relative colorimetric"));
    unsupported.intent = ColorIntent::RelativeColorimetric;
    unsupported.proofRequested = true;
    QVERIFY(unsupported.validationError().contains("unsupported"));
}

QTEST_MAIN(TstCanvasDisplay)
#include "tst_canvas_display.moc"
