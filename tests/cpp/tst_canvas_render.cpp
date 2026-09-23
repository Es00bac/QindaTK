// SPDX-License-Identifier: LGPL-3.0-or-later
// Rendering boundary: tile planning, bounded work, damage, resources.
#include "canvas_surface.h"
#include "canvas_surface_nodes.h"
#include "render_surface_adapter.h"

#include <QElapsedTimer>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <memory>

using namespace QindaTK;

namespace {

CanvasObjectId box(CanvasDocument &doc, QRectF r, QColor fill)
{
    CanvasObject o;
    o.shape = CanvasShape::Rect;
    o.rect = QRectF(QPointF(0, 0), r.size());
    o.transform = QTransform::fromTranslate(r.x(), r.y());
    o.fill = fill;
    return doc.insert(o, false);
}

// Renders until nothing is pending; returns the number of prepare() calls.
int settle(RenderSurfaceAdapter &adapter, const CanvasRenderRequest &request, int budget,
           std::shared_ptr<const CanvasRenderFrame> *last = nullptr)
{
    int frames = 0;
    do {
        auto frame = adapter.prepare(request);
        ++frames;
        if (adapter.lastRendered() > budget) {
            return -1;
        }
        if (last) {
            *last = frame;
        }
    } while (adapter.hasPendingWork() && frames < 1000);
    return frames;
}

QColor pixelAt(QQuickWindow *window, QPointF p)
{
    const QImage image = window->grabWindow();
    const qreal dpr = image.devicePixelRatio();
    return image.pixelColor(int(p.x() * dpr), int(p.y() * dpr));
}

bool close(QColor a, QColor b, int tolerance = 12)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance
        && std::abs(a.blue() - b.blue()) <= tolerance;
}

} // namespace

class TstCanvasRender : public QObject {
    Q_OBJECT
private slots:
    void levelOfDetail();
    void visibleTilesCoverAndAreBounded();
    void tenThousandObjectsBoundedWork_data();
    void tenThousandObjectsBoundedWork();
    void damageInvalidatesOnlyTouchedTiles();
    void revisionAndColorContext();
    void cacheIsByteBounded();
    void softwarePixelsKnownAnswer();
    void tessellationCoversShapes();
    void synchronizedViews();
    void contextLossAndBackendFallback();
    void windowChangeReleasesResources();
    void pointerCancellationInView();
    void resizeAndDevicePixelRatio();
    void sceneGraphBackend();
};

void TstCanvasRender::levelOfDetail()
{
    QCOMPARE(CanvasTiles::levelOfDetail(1.0, 1.0), 0);
    QCOMPARE(CanvasTiles::levelOfDetail(1.0, 2.0), 1);
    QCOMPARE(CanvasTiles::levelOfDetail(0.3, 1.0), -1);
    QCOMPARE(CanvasTiles::levelOfDetail(3.0, 1.0), 2);
    QCOMPARE(CanvasTiles::levelOfDetail(1.5, 1.5), 2); // 2.25 -> 4 px per unit
    QCOMPARE(CanvasTiles::tileDocumentSize(-3, 256), 2048.0);
    QCOMPARE(CanvasTiles::tileRect({1, 2, -1}, 256), QRectF(256, -128, 128, 128));
}

void TstCanvasRender::visibleTilesCoverAndAreBounded()
{
    const QSizeF view(1280, 800);
    for (qreal dpr : {1.0, 1.25, 2.0}) {
        for (qreal zoom : {0.013, 0.37, 1.0, 5.5}) {
            for (qreal rotation : {0.0, 17.0, 45.0, -120.0}) {
                const CanvasViewPose pose{QPointF(333, -77), zoom, rotation};
                const auto keys = CanvasTiles::visible(pose, view, dpr, 256);
                QVERIFY(keys.size() <= CanvasTiles::maxVisible(view, dpr, 256));
                // Every sampled view point lies in a planned tile.
                for (int y = 0; y <= 8; ++y) {
                    for (int x = 0; x <= 8; ++x) {
                        const QPointF doc = CanvasTransform::mapToDocument(
                            pose, view, QPointF(view.width() * x / 8, view.height() * y / 8));
                        const bool covered = std::any_of(keys.cbegin(), keys.cend(), [&](const CanvasTileKey &k) {
                            return CanvasTiles::tileRect(k, 256).adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(doc);
                        });
                        QVERIFY2(covered, qPrintable(QStringLiteral("z%1 r%2 dpr%3").arg(zoom).arg(rotation).arg(dpr)));
                    }
                }
            }
        }
    }
    qInfo("bound at 1280x800: dpr1 %d, dpr2 %d tiles", CanvasTiles::maxVisible(view, 1, 256),
          CanvasTiles::maxVisible(view, 2, 256));
}

void TstCanvasRender::tenThousandObjectsBoundedWork_data()
{
    QTest::addColumn<bool>("sceneGraph");
    QTest::addColumn<qreal>("zoom");
    QTest::addColumn<qreal>("rotation");
    QTest::addColumn<qreal>("dpr");
    for (bool sg : {false, true}) {
        const char *b = sg ? "scenegraph" : "software";
        QTest::addRow("%s-fit", b) << sg << 0.16 << 0.0 << 1.0;
        QTest::addRow("%s-1x-rot30", b) << sg << 1.0 << 30.0 << 1.0;
        QTest::addRow("%s-8x-dpr2", b) << sg << 8.0 << -60.0 << 2.0;
    }
}

void TstCanvasRender::tenThousandObjectsBoundedWork()
{
    QFETCH(bool, sceneGraph);
    QFETCH(qreal, zoom);
    QFETCH(qreal, rotation);
    QFETCH(qreal, dpr);
    CanvasDocument doc;
    doc.populateFixture(10000, 7, true);
    RenderSurfaceAdapter adapter;
    adapter.setDocument(&doc);
    adapter.setSceneGraphAvailable(sceneGraph, QStringLiteral("test"));
    QCOMPARE(adapter.backend(), sceneGraph ? CanvasBackend::SceneGraph : CanvasBackend::Software);
    const int budget = 24;
    adapter.setMaxTilesPerFrame(budget);
    CanvasRenderRequest request;
    request.pose = {doc.pageRect().center(), zoom, rotation};
    request.viewSize = QSizeF(1280, 800);
    request.devicePixelRatio = dpr;
    QElapsedTimer timer;
    timer.start();
    std::shared_ptr<const CanvasRenderFrame> frame;
    const int frames = settle(adapter, request, budget, &frame);
    const qint64 ms = timer.elapsed();
    QVERIFY(frames > 0);
    const int visible = adapter.lastVisible();
    QCOMPARE(frame->items.size(), visible);
    QVERIFY(visible <= CanvasTiles::maxVisible(request.viewSize, dpr, adapter.tilePixels()));
    QVERIFY(frames <= visible / budget + 1);
    QVERIFY(adapter.cacheBytes() <= qsizetype(adapter.cacheLimitMiB()) * 1024 * 1024);
    const QVariantMap stats = adapter.stats();
    QCOMPARE(stats.value("renderedRevision").toULongLong(), doc.revision());
    qInfo("%s zoom %.2f rot %.0f dpr %.1f: %d visible tiles, %d frames, %lld cache bytes, %lld ms, lod %d",
          sceneGraph ? "scenegraph" : "software", zoom, rotation, dpr, visible, frames,
          qlonglong(adapter.cacheBytes()), qlonglong(ms), stats.value("lod").toInt());
    // A second frame at the same pose renders nothing.
    adapter.prepare(request);
    QCOMPARE(adapter.lastRendered(), 0);
}

void TstCanvasRender::damageInvalidatesOnlyTouchedTiles()
{
    CanvasDocument doc;
    doc.populateFixture(2500, 3, false);
    const CanvasObjectId target = doc.objectIds().at(1234);
    const QRectF oldBounds = doc.objectInfo(target).value("bounds").toRectF();
    RenderSurfaceAdapter adapter;
    adapter.setDocument(&doc);
    adapter.setMaxTilesPerFrame(1000);
    CanvasRenderRequest request;
    request.pose = {oldBounds.center(), 1.0, 0.0};
    request.viewSize = QSizeF(1024, 1024);
    std::shared_ptr<const CanvasRenderFrame> before;
    settle(adapter, request, 1000, &before);
    QVERIFY(doc.moveObjects({qulonglong(target)}, 30, 0));
    const QRectF newBounds = doc.objectInfo(target).value("bounds").toRectF();
    std::shared_ptr<const CanvasRenderFrame> after;
    request.documentRevision = doc.revision();
    after = adapter.prepare(request);
    int expected = 0;
    for (const CanvasRenderItem &item : before->items) {
        const qreal pad = 2.0 * item.documentRect.width() / adapter.tilePixels();
        const bool touched = item.documentRect.intersects(oldBounds.adjusted(-pad, -pad, pad, pad))
            || item.documentRect.intersects(newBounds.adjusted(-pad, -pad, pad, pad));
        expected += touched ? 1 : 0;
        const auto match = std::find_if(after->items.cbegin(), after->items.cend(),
                                        [&](const CanvasRenderItem &i) { return i.key == item.key; });
        QVERIFY(match != after->items.cend());
        QCOMPARE(match->serial != item.serial, touched);
    }
    QVERIFY(expected >= 1 && expected <= 4);
    QCOMPARE(adapter.lastRendered(), expected);
    qInfo("moving one of 2500 objects re-rendered %d of %d visible tiles", expected, adapter.lastVisible());
}

void TstCanvasRender::revisionAndColorContext()
{
    CanvasDocument doc;
    box(doc, QRectF(0, 0, 50, 50), Qt::red);
    RenderSurfaceAdapter adapter;
    adapter.setDocument(&doc);
    CanvasRenderRequest request;
    request.pose = {QPointF(0, 0), 1.0, 0.0};
    request.viewSize = QSizeF(512, 512);
    settle(adapter, request, 24);
    QCOMPARE(adapter.stats().value("renderedRevision").toULongLong(), doc.revision());
    CanvasColorContext context;
    context.working = QColorSpace(QColorSpace::SRgbLinear);
    adapter.setColorContext(context);
    adapter.prepare(request);
    QCOMPARE(adapter.lastRendered(), adapter.lastVisible()); // every tile is stale after a context change
    QVERIFY(adapter.colorContextDescription().contains(QStringLiteral("->")));
    doc.clear(); // a reset invalidates everything too
    adapter.prepare(request);
    QCOMPARE(adapter.lastRendered(), adapter.lastVisible());
}

void TstCanvasRender::cacheIsByteBounded()
{
    CanvasDocument doc;
    doc.populateFixture(400, 5, false);
    RenderSurfaceAdapter adapter;
    adapter.setDocument(&doc);
    adapter.setCacheLimitMiB(1); // four 256x256 ARGB tiles
    adapter.setMaxTilesPerFrame(64);
    CanvasRenderRequest request;
    request.viewSize = QSizeF(300, 300);
    const qsizetype tileBytes = 256 * 256 * 4;
    for (int step = 0; step < 20; ++step) {
        request.pose = {QPointF(step * 97.0, step * 41.0), 1.0, 0.0};
        adapter.prepare(request);
        // Tiles used by the current frame are never evicted; everything else is.
        QVERIFY(adapter.cacheBytes() <= std::max<qsizetype>(1024 * 1024, adapter.lastVisible() * tileBytes));
    }
    QVERIFY(adapter.stats().value("totalEvicted").toULongLong() > 0);
}

void TstCanvasRender::softwarePixelsKnownAnswer()
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
    QCOMPARE(tile.pixelColor(5, 5), QColor(Qt::white));   // page colour
    QCOMPARE(tile.pixelColor(60, 60), QColor(Qt::white)); // hidden object
    QCOMPARE(tile.pixelColor(51, 11), QColor(Qt::white)); // degenerate object
    QCOMPARE(tile.pixelColor(110, 110).alpha(), 0);       // outside the page
    // A tile at 4 px per document unit maps pixel (16,16) to document (20,20).
    const QImage fine = CanvasPainter::renderTile(doc, QRectF(16, 16, 8, 8), 32, {});
    QCOMPARE(fine.pixelColor(16, 16), QColor(Qt::red));
    // Sub-pixel objects become single device pixels, not nothing.
    CanvasPaintStats stats;
    const QImage coarse = CanvasPainter::renderTile(doc, QRectF(0, 0, 4096, 4096), 16, {}, &stats);
    QCOMPARE(coarse.width(), 16);
    QCOMPARE(stats.dots, 1);
    QCOMPARE(stats.skipped, 1); // the hidden one; degenerate objects are not even indexed
}

void TstCanvasRender::tessellationCoversShapes()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 1, 1));
    doc.setPageColor(QColor(Qt::white));
    box(doc, QRectF(10, 10, 20, 30), Qt::red);
    CanvasObject e;
    e.shape = CanvasShape::Ellipse;
    e.rect = QRectF(0, 0, 200, 100);
    e.transform = QTransform::fromTranslate(100, 0);
    e.fill = QColor(Qt::blue);
    doc.insert(e, false);
    const CanvasTileContent content = CanvasTessellator::build(doc, QRectF(0, 0, 512, 512), 1.0);
    QCOMPARE(content.runs.size(), 1);
    qreal red = 0;
    qreal blue = 0;
    const auto &v = content.runs.first().vertices;
    for (int i = 0; i + 2 < v.size(); i += 3) {
        const qreal area = std::abs((v[i + 1].x - v[i].x) * (v[i + 2].y - v[i].y)
                                    - (v[i + 2].x - v[i].x) * (v[i + 1].y - v[i].y)) / 2.0;
        if (v[i].r == 255 && v[i].g == 0) {
            red += area;
        } else if (v[i].b == 255 && v[i].r == 0) {
            blue += area;
        }
    }
    QVERIFY(std::abs(red - 600.0) < 1e-3);
    QVERIFY(std::abs(blue - M_PI * 100 * 50) / (M_PI * 100 * 50) < 0.01);
    QVERIFY(content.bytes == v.size() * qsizetype(sizeof(CanvasVertex)));
}

void TstCanvasRender::synchronizedViews()
{
    CanvasDocument doc;
    const CanvasObjectId id = box(doc, QRectF(100, 100, 40, 40), Qt::red);
    const QRectF bounds = doc.objectInfo(id).value("bounds").toRectF();
    QQuickWindow window;
    window.resize(600, 300);
    CanvasViewState shared;
    CanvasSurface a(window.contentItem());
    CanvasSurface b(window.contentItem());
    a.setSize(QSizeF(400, 300));
    b.setPosition(QPointF(400, 0));
    b.setSize(QSizeF(200, 300));
    for (CanvasSurface *s : {&a, &b}) {
        s->setDocument(&doc);
        s->setViewState(&shared);
    }
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    a.fitRect(QRectF(0, 0, 300, 300));
    a.rotateBy(30);
    a.zoomAbout(10, 10, 1.7);
    b.panBy(-15, 22);
    QCOMPARE(a.pose(), b.pose());
    QVERIFY((a.mapToDocument(QPointF(200, 150)) - b.mapToDocument(QPointF(100, 150))).manhattanLength() < 1e-9);
    QCOMPARE(doc.objectInfo(id).value("bounds").toRectF(), bounds); // navigation never edits
    QTRY_VERIFY(!a.renderSurface()->hasPendingWork() && !b.renderSurface()->hasPendingWork());
    // Centre the shared pose on the object: each view shows it at ITS centre.
    shared.setCenterX(120);
    shared.setCenterY(120);
    const QPointF inA = a.mapFromDocument(QPointF(120, 120));
    const QPointF inB = b.mapFromDocument(QPointF(120, 120)) + b.position();
    QCOMPARE(inA, QPointF(200, 150));
    QCOMPARE(inB, QPointF(500, 150));
    QTRY_VERIFY(close(pixelAt(&window, inA), Qt::red));
    QTRY_VERIFY(close(pixelAt(&window, inB), Qt::red));
}

void TstCanvasRender::contextLossAndBackendFallback()
{
    CanvasDocument doc;
    box(doc, QRectF(0, 0, 100, 100), Qt::red);
    QQuickWindow window;
    window.resize(300, 300);
    CanvasSurface view(window.contentItem());
    view.setSize(QSizeF(300, 300));
    view.setDocument(&doc);
    view.setFitMode(QStringLiteral("page"));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const QPointF probe = view.mapFromDocument(QPointF(50, 50));
    QTRY_VERIFY(close(pixelAt(&window, probe), Qt::red));
    const quint64 revision = doc.revision();
    const quint64 generation = view.renderSurface()->resourceGeneration();
    const int textures = CanvasSceneGraph::liveTextures();
    QVERIFY(textures > 0);
    view.simulateContextLoss(false);
    QCOMPARE(view.renderSurface()->resourceGeneration(), generation + 1);
    QTRY_VERIFY(close(pixelAt(&window, probe), Qt::red));
    QCOMPARE(CanvasSceneGraph::liveTextures(), textures); // old ones released, same set rebuilt
    QCOMPARE(CanvasSceneGraph::liveRootNodes(), 1);
    view.simulateContextLoss(true); // CPU cache dropped too: re-render from the document
    QCOMPARE(view.renderSurface()->cachedTiles(), 0);
    QTRY_VERIFY(close(pixelAt(&window, probe), Qt::red));
    QCOMPARE(doc.revision(), revision);
    // The software adaptation cannot draw custom geometry: the request is
    // recorded, the effective backend stays software, and the reason is exposed.
    view.setBackend(QStringLiteral("scenegraph"));
    QTRY_COMPARE(view.renderSurface()->fallbackReason(), QStringLiteral("software-adaptation"));
    QCOMPARE(view.renderSurface()->effectiveBackend(), QStringLiteral("software"));
    QTRY_VERIFY(close(pixelAt(&window, probe), Qt::red));
}

void TstCanvasRender::windowChangeReleasesResources()
{
    CanvasDocument doc;
    box(doc, QRectF(0, 0, 100, 100), Qt::red);
    auto first = std::make_unique<QQuickWindow>();
    first->resize(200, 200);
    CanvasSurface view; // no QObject parent: it must outlive the first window
    view.setParentItem(first->contentItem());
    view.setSize(QSizeF(200, 200));
    view.setDocument(&doc);
    view.fitRect(QRectF(0, 0, 100, 100));
    first->show();
    QVERIFY(QTest::qWaitForWindowExposed(first.get()));
    QTRY_VERIFY(close(pixelAt(first.get(), QPointF(100, 100)), Qt::red));
    QQuickWindow second;
    second.resize(200, 200);
    second.show();
    QVERIFY(QTest::qWaitForWindowExposed(&second));
    view.setParentItem(second.contentItem());
    first.reset(); // the old scene graph (and every node/texture in it) is gone
    QTRY_VERIFY(close(pixelAt(&second, QPointF(100, 100)), Qt::red));
    QTRY_COMPARE(CanvasSceneGraph::liveRootNodes(), 1);
}

void TstCanvasRender::pointerCancellationInView()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    tools.setActiveTool(QStringLiteral("rectangle"));
    QQuickWindow window;
    window.resize(400, 400);
    auto *view = new CanvasSurface(window.contentItem());
    view->setSize(QSizeF(400, 400));
    view->setDocument(&doc);
    view->setToolController(&tools);
    view->setViewId(QStringLiteral("main"));
    window.show();
    window.requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    const quint64 revision = doc.revision();
    // Escape mid-drag, then the late release commits nothing.
    QTest::mousePress(&window, Qt::LeftButton, {}, QPoint(50, 50));
    QTest::mouseMove(&window, QPoint(120, 140));
    QCOMPARE(tools.gestureOwner(), QStringLiteral("main"));
    QTest::keyClick(&window, Qt::Key_Escape);
    QCOMPARE(tools.lastOutcome(), QStringLiteral("canceled:escape"));
    QTest::mouseRelease(&window, Qt::LeftButton, {}, QPoint(130, 150));
    QCOMPARE(doc.revision(), revision);
    // A normal drag commits exactly once, in document coordinates.
    view->rotateBy(90, 200, 200);
    QTest::mousePress(&window, Qt::LeftButton, {}, QPoint(100, 100));
    QTest::mouseMove(&window, QPoint(150, 120));
    QTest::mouseRelease(&window, Qt::LeftButton, {}, QPoint(200, 140));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("committed"));
    QCOMPARE(doc.revision(), revision + 1);
    const QRectF drawn = doc.objectInfo(doc.objectIds().first()).value("bounds").toRectF();
    const QRectF expected = QRectF(view->mapToDocument(QPointF(100, 100)), view->mapToDocument(QPointF(200, 140))).normalized();
    QVERIFY((drawn.adjusted(1, 1, -1, -1).topLeft() - expected.topLeft()).manhattanLength() < 1e-6);
    // Lost grab: another item takes the mouse mid-gesture.
    QTest::mousePress(&window, Qt::LeftButton, {}, QPoint(50, 300));
    QTest::mouseMove(&window, QPoint(90, 330));
    view->ungrabMouse();
    QCOMPARE(tools.lastOutcome(), QStringLiteral("canceled:lost-grab"));
    QTest::mouseRelease(&window, Qt::LeftButton, {}, QPoint(90, 330));
    // The view is destroyed mid-gesture: the controller is left idle.
    QTest::mousePress(&window, Qt::LeftButton, {}, QPoint(60, 60));
    QTest::mouseMove(&window, QPoint(90, 90));
    QVERIFY(tools.gestureActive());
    delete view;
    QVERIFY(!tools.gestureActive());
    QCOMPARE(tools.lastOutcome(), QStringLiteral("canceled:view-detached"));
    QCOMPARE(doc.revision(), revision + 1);
    QTest::mouseRelease(&window, Qt::LeftButton, {}, QPoint(90, 90));
}

void TstCanvasRender::resizeAndDevicePixelRatio()
{
    CanvasDocument doc;
    doc.populateFixture(900, 9, false);
    QQuickWindow window;
    window.resize(300, 200);
    CanvasSurface view(window.contentItem());
    view.setSize(QSizeF(300, 200));
    view.setDocument(&doc);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(!view.renderSurface()->hasPendingWork());
    const int small = view.renderStats().value("visibleTiles").toInt();
    const int lod = view.renderStats().value("lod").toInt();
    window.resize(900, 700);
    view.setSize(QSizeF(900, 700));
    QTRY_VERIFY(!view.renderSurface()->hasPendingWork() && view.renderStats().value("visibleTiles").toInt() > small);
    view.setProperty("devicePixelRatioOverride", 2.0);
    view.panBy(1, 0); // schedule a frame
    QTRY_COMPARE(view.renderStats().value("lod").toInt(), lod + 1);
    QTRY_VERIFY(!view.renderSurface()->hasPendingWork());
    QVERIFY(view.renderStats().value("visibleTiles").toInt() <= CanvasTiles::maxVisible(QSizeF(900, 700), 2.0, 256));
}

void TstCanvasRender::sceneGraphBackend()
{
    // AGENT-NOTE: registered twice in CMake; the software run skips here and
    // the tst_canvas_render_rhi run (QT_QUICK_BACKEND=rhi) exercises the GPU
    // path when the machine can create an offscreen RHI context.
    if (qEnvironmentVariable("QT_QUICK_BACKEND") != QLatin1String("rhi")) {
        QSKIP("scene-graph provider runs only in the tst_canvas_render_rhi registration");
    }
    CanvasDocument doc;
    box(doc, QRectF(0, 0, 100, 100), Qt::red);
    QQuickWindow window;
    window.resize(300, 300);
    CanvasSurface view(window.contentItem());
    view.setSize(QSizeF(300, 300));
    view.setDocument(&doc);
    view.setFitMode(QStringLiteral("page"));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(window.rendererInterface() != nullptr);
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software
        || window.rendererInterface()->graphicsApi() == QSGRendererInterface::Null) {
        QSKIP("no hardware-backed RHI available for this offscreen window");
    }
    const QPointF probe = view.mapFromDocument(QPointF(50, 50));
    QTRY_COMPARE(view.renderSurface()->effectiveBackend(), QStringLiteral("scenegraph"));
    QTRY_VERIFY(close(pixelAt(&window, probe), Qt::red));
    const QColor gpu = pixelAt(&window, probe);
    const quint64 generation = view.renderSurface()->resourceGeneration();
    view.setBackend(QStringLiteral("software"));
    QCOMPARE(view.renderSurface()->resourceGeneration(), generation + 1);
    QTRY_VERIFY(close(pixelAt(&window, probe), gpu, 2)); // both providers agree
    view.setBackend(QStringLiteral("auto"));
    view.simulateContextLoss(true);
    QTRY_VERIFY(close(pixelAt(&window, probe), gpu, 2));
    QCOMPARE(CanvasSceneGraph::liveRootNodes(), 1);
    qInfo("scene-graph provider verified on %d", int(window.rendererInterface()->graphicsApi()));
}

QTEST_MAIN(TstCanvasRender)
#include "tst_canvas_render.moc"
