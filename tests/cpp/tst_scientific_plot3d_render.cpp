// SPDX-License-Identifier: LGPL-3.0-or-later
// Real Qt Quick 3D drawing of ScientificPlot3D and its WP-076 render-resource
// lifecycle: the GPU renderer draws the surface and points where
// ScientificScene3D.project() puts them, and a genuine scene-graph
// invalidation (a hidden window without persistent graphics on the threaded
// render loop, the path Qt takes after a graphics device loss) replaces the
// renderer, which rebuilds every GPU resource from the scene.
// Needs a display and a Qt RHI backend, for example
//   QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=rhi tst_scientific_plot3d_render
// The headless ctest run (offscreen, software adaptation) skips it.
#include "scientific_scene3d.h"

#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <atomic>
#include <memory>

using namespace QindaTK;

namespace {

struct Host {
    std::atomic_int invalidated{0};
    std::atomic_int initialized{0};
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QQuickItem> plot;
};

std::unique_ptr<Host> makeHost(ScientificGrid *grid, ScientificDataset *points)
{
    auto host = std::make_unique<Host>();
    host->engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
    QQmlComponent component(&host->engine);
    component.setData("import QtQuick\nimport QindaTK as Tk\nTk.ScientificPlot3D { resolution: 9 }\n", QUrl());
    QObject *object = component.createWithInitialProperties(
        {{"grid", QVariant::fromValue(grid)}, {"points", QVariant::fromValue(points)}});
    if (!object) {
        qWarning() << component.errors();
        return nullptr;
    }
    host->plot.reset(qobject_cast<QQuickItem *>(object));
    Host *raw = host.get();
    QObject::connect(&host->window, &QQuickWindow::sceneGraphInvalidated, &host->window,
                     [raw] { ++raw->invalidated; }, Qt::DirectConnection);
    QObject::connect(&host->window, &QQuickWindow::sceneGraphInitialized, &host->window,
                     [raw] { ++raw->initialized; }, Qt::DirectConnection);
    host->window.setPersistentGraphics(false);
    host->window.setPersistentSceneGraph(false);
    host->window.resize(480, 360);
    host->plot->setParentItem(host->window.contentItem());
    host->plot->setSize(QSizeF(480, 360));
    host->window.show();
    return host;
}

ScientificScene3D *sceneOf(const Host &host)
{ return host.plot->property("scene").value<ScientificScene3D *>(); }

// Window pixel of data point (x, y, z), from the scene's own projection.
QPoint windowPixel(const Host &host, double x, double y, double z)
{
    auto *content = host.plot->property("contentItem").value<QQuickItem *>();
    const QVariantMap p = sceneOf(host)->project(x, y, z, content->width(), content->height());
    return content->mapToScene(QPointF(p.value("x").toDouble(), p.value("y").toDouble())).toPoint();
}

int difference(const QColor &a, const QColor &b)
{ return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()); }

// The GPU image shows the surface node (1, 1, 1) and the probe (2, 2, 4).
void verifyDrawn(Host &host, const QColor &clear)
{
    const QImage frame = host.window.grabWindow();
    QVERIFY(!frame.isNull());
    const qreal dpr = frame.devicePixelRatio();
    auto at = [&](QPoint p) { return frame.pixelColor(int(p.x() * dpr), int(p.y() * dpr)); };
    const QColor node = at(windowPixel(host, 1, 1, 1));
    const QColor probe = at(windowPixel(host, 2, 2, 4));
    QVERIFY2(difference(node, clear) > 30, qPrintable(node.name() + ' ' + clear.name()));
    QVERIFY2(difference(probe, clear) > 30, qPrintable(probe.name() + ' ' + clear.name()));
}

} // namespace

class TstScientificPlot3DRender : public QObject {
    Q_OBJECT
public:
    static void initMain()
    {
        // Only the threaded loop releases a hidden non-persistent window's scene graph.
        if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP"))
            qputenv("QSG_RENDER_LOOP", "threaded");
    }
private slots:
    void initTestCase();
    void drawsAndRecoversFromSceneGraphLoss();
};

void TstScientificPlot3DRender::initTestCase()
{
    if (QGuiApplication::platformName() == QLatin1String("offscreen")
        || QQuickWindow::sceneGraphBackend() == QLatin1String("software"))
        QSKIP("Needs a display and a Qt RHI backend (xcb or wayland with QT_QUICK_BACKEND=rhi)");
}

void TstScientificPlot3DRender::drawsAndRecoversFromSceneGraphLoss()
{
    // z = x y on a 3 x 3 grid and two probes (the second at the far top corner).
    ScientificGrid grid;
    grid.setSourceId("product"); grid.setXUnit("m"); grid.setYUnit("m"); grid.setZUnit("K");
    QVERIFY(grid.replaceGrid({0.0, 1.0, 2.0}, {0.0, 1.0, 2.0}, {0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 0.0, 2.0, 4.0}));
    ScientificDataset points;
    points.setSourceId("probes"); points.setKind(ScientificDataset::Scatter); points.setDimensions(3);
    points.setXUnit("m"); points.setYUnit("m"); points.setZUnit("K");
    QVERIFY(points.replaceSamples({QVariantMap{{"id", "p0"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}},
                                   QVariantMap{{"id", "p1"}, {"x", 2.0}, {"y", 2.0}, {"z", 4.0}}}));
    auto host = makeHost(&grid, &points);
    QVERIFY(host);
    QVERIFY(QMetaObject::invokeMethod(host->plot.get(), "fitDomain"));
    QVERIFY(QTest::qWaitForWindowExposed(&host->window));
    QTRY_VERIFY_WITH_TIMEOUT(host->plot->property("gpuRenderer").toBool(), 10000);
    QPointer<QObject> renderer = host->plot->findChild<QObject *>(QStringLiteral("scientificRenderer3D"));
    QVERIFY(renderer);
    const QColor clear = QQmlProperty::read(renderer, QStringLiteral("environment.clearColor")).value<QColor>();
    QVERIFY(clear.isValid());
    QTRY_VERIFY_WITH_TIMEOUT(difference(host->window.grabWindow().pixelColor(
        windowPixel(*host, 1, 1, 1) * host->window.devicePixelRatio()), clear) > 30, 10000);
    verifyDrawn(*host, clear);
    const int generation = host->plot->property("renderGeneration").toInt();
    QVERIFY(generation >= 1);

    // Lose the scene graph: the renderer and its GPU resources are released.
    host->window.hide();
    QTRY_VERIFY(host->invalidated.load() > 0);
    QTRY_VERIFY(renderer.isNull());
    QTRY_VERIFY(host->plot->property("renderGeneration").toInt() > generation);
    // Data changes while hidden reach the rebuilt renderer.
    QVERIFY(sceneOf(*host)->selectPoint("p1"));
    host->plot->setProperty("resolution", 5);

    // Recovery: a new device initializes and the replacement draws the scene.
    const int initialized = host->initialized.load();
    host->window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host->window));
    QTRY_VERIFY(host->initialized.load() > initialized);
    QTRY_VERIFY(host->plot->property("gpuRenderer").toBool());
    QVERIFY(host->plot->findChild<QObject *>(QStringLiteral("scientificRenderer3D")));
    QCOMPARE(sceneOf(*host)->resolutionX(), 5);
    QTRY_VERIFY_WITH_TIMEOUT(difference(host->window.grabWindow().pixelColor(
        windowPixel(*host, 1, 1, 1) * host->window.devicePixelRatio()), clear) > 30, 10000);
    verifyDrawn(*host, clear);

    // resetRenderer() on request takes the same path without losing the device.
    const int before = host->plot->property("renderGeneration").toInt();
    QVERIFY(QMetaObject::invokeMethod(host->plot.get(), "resetRenderer"));
    QTRY_VERIFY(host->plot->property("renderGeneration").toInt() > before);
    QTRY_VERIFY(host->plot->property("gpuRenderer").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(difference(host->window.grabWindow().pixelColor(
        windowPixel(*host, 1, 1, 1) * host->window.devicePixelRatio()), clear) > 30, 10000);
}

QTEST_MAIN(TstScientificPlot3DRender)
#include "tst_scientific_plot3d_render.moc"
