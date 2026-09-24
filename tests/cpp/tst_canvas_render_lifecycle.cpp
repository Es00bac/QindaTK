// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_surface.h"
#include "canvas_surface_nodes.h"

#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <cmath>

using namespace QindaTK;

namespace {

QColor itemPixelAt(QQuickItem *item, QPointF point)
{
    // Item capture also works when Qt 6.8's offscreen GLX drawable is stale.
    const auto grab = item->grabToImage();
    if (!grab) return {};
    QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
    if (grab->image().isNull() && !ready.wait(5000)) return {};
    const QImage image = grab->image();
    const qreal dpr = image.devicePixelRatio();
    return image.pixelColor(int(point.x() * dpr), int(point.y() * dpr));
}

bool red(QColor color)
{
    return std::abs(color.red() - 255) <= 12 && color.green() <= 12 && color.blue() <= 12;
}

} // namespace

class TstCanvasRenderLifecycle : public QObject {
    Q_OBJECT
private slots:
    void twoWindowResourceIsolation();
};

void TstCanvasRenderLifecycle::twoWindowResourceIsolation()
{
    CanvasDocument doc;
    CanvasObject object;
    object.shape = CanvasShape::Rect;
    object.rect = QRectF(0, 0, 100, 100);
    object.fill = Qt::red;
    doc.insert(object, false);

    QQuickWindow first;
    QQuickWindow second;
    first.resize(200, 200);
    second.resize(400, 200);
    CanvasSurface a(first.contentItem());
    CanvasSurface b(second.contentItem());
    a.setSize(QSizeF(200, 200));
    b.setSize(QSizeF(200, 200));
    a.setDocument(&doc);
    b.setDocument(&doc);
    a.fitRect(QRectF(0, 0, 100, 100));
    b.fitRect(QRectF(0, 0, 100, 100));
    first.show();
    second.show();
    QVERIFY(QTest::qWaitForWindowExposed(&first));
    QVERIFY(QTest::qWaitForWindowExposed(&second));
    QTRY_VERIFY(red(itemPixelAt(&a, QPointF(100, 100))));
    QTRY_VERIFY(red(itemPixelAt(&b, QPointF(100, 100))));
    if (qEnvironmentVariable("QT_QUICK_BACKEND") == QLatin1String("rhi")) {
        QTRY_COMPARE(a.renderSurface()->effectiveBackend(), QStringLiteral("scenegraph"));
        QTRY_COMPARE(b.renderSurface()->effectiveBackend(), QStringLiteral("scenegraph"));
    }
    QTRY_COMPARE(CanvasSceneGraph::liveRootNodes(), 2);

    const quint64 bGeneration = b.renderSurface()->resourceGeneration();
    a.simulateContextLoss(true);
    QTRY_VERIFY(red(itemPixelAt(&a, QPointF(100, 100))));
    QCOMPARE(b.renderSurface()->resourceGeneration(), bGeneration);
    QTRY_VERIFY(red(itemPixelAt(&b, QPointF(100, 100))));

    const quint64 aGeneration = a.renderSurface()->resourceGeneration();
    a.setParentItem(second.contentItem()); // both windows remain alive
    a.setPosition(QPointF(200, 0));
    QVERIFY(a.renderSurface()->resourceGeneration() > aGeneration);
    QTRY_VERIFY(red(itemPixelAt(&a, QPointF(100, 100))));
    QTRY_VERIFY(red(itemPixelAt(&b, QPointF(100, 100))));
    QCOMPARE(b.renderSurface()->resourceGeneration(), bGeneration);
    QTRY_COMPARE(CanvasSceneGraph::liveRootNodes(), 2);
}

QTEST_MAIN(TstCanvasRenderLifecycle)
#include "tst_canvas_render_lifecycle.moc"
