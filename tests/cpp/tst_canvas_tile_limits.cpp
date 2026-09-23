// SPDX-License-Identifier: LGPL-3.0-or-later
#include "render_surface_adapter.h"

#include <QTest>
#include <limits>

using namespace QindaTK;

class TstCanvasTileLimits : public QObject {
    Q_OBJECT
private slots:
    void visibleTilesAtCoordinateLimit();
};

void TstCanvasTileLimits::visibleTilesAtCoordinateLimit()
{
    constexpr qreal high = std::numeric_limits<int>::max();
    constexpr qreal low = std::numeric_limits<int>::min();
    const QSizeF view(128, 128);
    const CanvasViewPose atHigh{QPointF((high + 0.5) * 256, 128), 1, 0};
    const auto highKeys = CanvasTiles::visible(atHigh, view, 1, 256);
    QCOMPARE(highKeys.size(), 1);
    QCOMPARE(highKeys.first().lod, 0);
    QCOMPARE(highKeys.first().x, std::numeric_limits<int>::max());
    QCOMPARE(highKeys.first().y, 0);
    const CanvasViewPose atLow{QPointF((low + 0.5) * 256, 128), 1, 0};
    const auto lowKeys = CanvasTiles::visible(atLow, view, 1, 256);
    QCOMPARE(lowKeys.size(), 1);
    QCOMPARE(lowKeys.first().lod, 0);
    QCOMPARE(lowKeys.first().x, std::numeric_limits<int>::min());
    QCOMPARE(lowKeys.first().y, 0);
    // A view wholly beyond the addressable tile grid has no keys. It must
    // not narrow a floating index to int or attempt a wrapped loop.
    const CanvasViewPose beyond{QPointF((high + 2.5) * 256, 128), 1, 0};
    QVERIFY(CanvasTiles::visible(beyond, view, 1, 256).isEmpty());
}

QTEST_MAIN(TstCanvasTileLimits)
#include "tst_canvas_tile_limits.moc"
