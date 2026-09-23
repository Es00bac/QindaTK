// SPDX-License-Identifier: LGPL-3.0-or-later
// Known-answer tests for the canvas transform, object model and picking.
#include "canvas_document.h"
#include "canvas_transform.h"
#include "hit_test_service.h"

#include <QSignalSpy>
#include <QTest>
#include <cmath>

using namespace QindaTK;
namespace CT = QindaTK::CanvasTransform;

namespace {

bool near(QPointF a, QPointF b, qreal eps)
{
    return std::abs(a.x() - b.x()) <= eps && std::abs(a.y() - b.y()) <= eps;
}

CanvasObjectId rect(CanvasDocument &doc, QRectF r, CanvasLayerId layer = 0, bool locked = false)
{
    CanvasObject o;
    o.shape = CanvasShape::Rect;
    o.rect = QRectF(QPointF(0, 0), r.size());
    o.transform = QTransform::fromTranslate(r.x(), r.y());
    o.fill = QColor(Qt::red);
    o.layer = layer;
    o.locked = locked;
    return doc.insert(o);
}

} // namespace

class TstCanvasCore : public QObject {
    Q_OBJECT
private slots:
    void knownAnswerMapping();
    void roundTrip_data();
    void roundTrip();
    void singularMatrices();
    void anchoredNavigation();
    void panMovesContentByDelta();
    void fitRotated();
    void spatialIndexMatchesBruteForce();
    void spatialIndexHugeBoundsStayBounded();
    void documentCommitUndoDamage();
    void staleEditRejected();
    void overlapAndLayerOrder();
    void lockPolicies();
    void hiddenObjectsAndLayers();
    void transformedShapes();
    void degenerateObjectsAreSkipped();
    void toleranceScalesWithZoom();
    void subobjectParts();
    void pickingAcrossViews_data();
    void pickingAcrossViews();
    void regionPicking();
    void fixtureDeterminism();
};

void TstCanvasCore::knownAnswerMapping()
{
    // center (100,50), zoom 2, turned 90 degrees clockwise, view 200x100.
    const CanvasViewPose pose{QPointF(100, 50), 2.0, 90.0};
    const QSizeF view(200, 100);
    QVERIFY(near(CT::mapToView(pose, view, QPointF(100, 50)), QPointF(100, 50), 1e-12));
    // +x in the document points down on screen after a clockwise quarter turn.
    QVERIFY(near(CT::mapToView(pose, view, QPointF(110, 50)), QPointF(100, 70), 1e-12));
    QVERIFY(near(CT::mapToView(pose, view, QPointF(100, 60)), QPointF(80, 50), 1e-12));
    QVERIFY(near(CT::mapToDocument(pose, view, QPointF(100, 70)), QPointF(110, 50), 1e-12));
    // Device space is view * dpr.
    QCOMPARE(CT::viewToDevice(2.0).map(QPointF(3, 4)), QPointF(6, 8));
}

void TstCanvasCore::roundTrip_data()
{
    QTest::addColumn<qreal>("zoom");
    QTest::addColumn<qreal>("rotation");
    QTest::addColumn<QPointF>("center");
    for (qreal zoom : {0.03, 0.5, 1.0, 3.7, 64.0}) {
        for (qreal rotation : {0.0, 30.0, -135.0, 90.0, 180.0}) {
            QTest::addRow("z%g-r%g", zoom, rotation) << zoom << rotation << QPointF(12345.5, -678.25);
        }
    }
}

void TstCanvasCore::roundTrip()
{
    QFETCH(qreal, zoom);
    QFETCH(qreal, rotation);
    QFETCH(QPointF, center);
    const CanvasViewPose pose{center, zoom, rotation};
    const QSizeF view(1280, 720);
    const QTransform forward = CT::documentToView(pose, view);
    const QTransform inverse = CT::viewToDocument(pose, view);
    for (QPointF p : {QPointF(0, 0), QPointF(1e5, -3e4), center, QPointF(-0.001, 7.5)}) {
        const QPointF back = inverse.map(forward.map(p));
        // Error bound: a few ulps of the largest coordinate involved.
        const qreal scale = std::max({1.0, std::abs(p.x()), std::abs(p.y()), std::abs(center.x())});
        QVERIFY2(near(back, p, 1e-9 * scale), qPrintable(QStringLiteral("%1,%2").arg(back.x()).arg(back.y())));
    }
    QVERIFY(CT::isInvertible(forward));
}

void TstCanvasCore::singularMatrices()
{
    QVERIFY(!CT::invert(QTransform(0, 0, 0, 0, 5, 5)).has_value());
    QVERIFY(!CT::invert(QTransform::fromScale(1, 0)).has_value());
    QVERIFY(!CT::invert(QTransform(1, 2, 2, 4, 0, 0)).has_value()); // rank 1
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    QVERIFY(!CT::isFinite(QTransform(nan, 0, 0, 1, 0, 0)));
    QVERIFY(!CT::invert(QTransform(1, 0, 0, std::numeric_limits<qreal>::infinity(), 0, 0)).has_value());
    // Tiny but valid scales stay invertible (the threshold is relative).
    const auto tiny = CT::invert(QTransform::fromScale(1e-5, 1e-5));
    QVERIFY(tiny.has_value());
    QCOMPARE(tiny->map(QPointF(1e-5, 2e-5)), QPointF(1, 2));
    // A sanitized pose never produces a singular view transform.
    const CanvasViewPose bad = CT::sanitized({QPointF(nan, 1), 0.0, nan}, {});
    QVERIFY(CT::isInvertible(CT::documentToView(bad, QSizeF(10, 10))));
    QCOMPARE(bad.center, QPointF());
    QCOMPARE(bad.rotation, 0.0);
}

void TstCanvasCore::anchoredNavigation()
{
    const QSizeF view(800, 600);
    const CanvasZoomLimits limits{0.05, 20};
    CanvasViewPose pose{QPointF(300, 200), 1.3, 17};
    for (QPointF anchor : {QPointF(0, 0), QPointF(799, 12), QPointF(400, 300), QPointF(123, 555)}) {
        const QPointF under = CT::mapToDocument(pose, view, anchor);
        for (qreal factor : {1.25, 0.8, 100.0, 1e-4}) { // the last two hit the limits
            const CanvasViewPose zoomed = CT::zoomAbout(pose, view, anchor, factor, limits);
            QVERIFY(zoomed.zoom >= limits.minimum && zoomed.zoom <= limits.maximum);
            QVERIFY(near(CT::mapToDocument(zoomed, view, anchor), under, 1e-9));
        }
        const CanvasViewPose turned = CT::rotateAbout(pose, view, anchor, 33);
        QVERIFY(near(CT::mapToDocument(turned, view, anchor), under, 1e-9));
        QCOMPARE(turned.rotation, 50.0);
    }
    QCOMPARE(CT::normalizeRotation(540), 180.0);
    QCOMPARE(CT::normalizeRotation(-180), 180.0);
    QCOMPARE(CT::normalizeRotation(-190), 170.0);
}

void TstCanvasCore::panMovesContentByDelta()
{
    const QSizeF view(640, 480);
    const CanvasViewPose pose{QPointF(10, 20), 2.5, -60};
    const QPointF doc(33, -7);
    const QPointF before = CT::mapToView(pose, view, doc);
    const CanvasViewPose panned = CT::panBy(pose, QPointF(15, -4));
    QVERIFY(near(CT::mapToView(panned, view, doc), before + QPointF(15, -4), 1e-9));
}

void TstCanvasCore::fitRotated()
{
    const QSizeF view(400, 400);
    CanvasViewPose pose;
    pose = CT::fit(pose, view, QRectF(0, 0, 1000, 500), 0, {});
    QCOMPARE(pose.zoom, 0.4);
    QCOMPARE(pose.center, QPointF(500, 250));
    pose.rotation = 90;
    pose = CT::fit(pose, view, QRectF(0, 0, 1000, 500), 0, {});
    QCOMPARE(pose.zoom, 0.4);
    pose.rotation = 45;
    pose = CT::fit(pose, view, QRectF(0, 0, 1000, 500), 10, {});
    // Every page corner is inside the view after fitting.
    for (QPointF c : {QPointF(0, 0), QPointF(1000, 0), QPointF(1000, 500), QPointF(0, 500)}) {
        const QPointF v = CT::mapToView(pose, view, c);
        QVERIFY(v.x() >= 9.999 && v.x() <= 390.001 && v.y() >= 9.999 && v.y() <= 390.001);
    }
}

void TstCanvasCore::spatialIndexMatchesBruteForce()
{
    CanvasGeometry::SpatialIndex index(64);
    QHash<CanvasObjectId, QRectF> truth;
    quint32 s = 12345;
    auto next = [&] { s = s * 1664525u + 1013904223u; return (s >> 8) / qreal(1 << 24); };
    for (CanvasObjectId id = 1; id <= 2000; ++id) {
        const QRectF r(next() * 4000 - 500, next() * 4000 - 500, next() * 300, next() * 300);
        index.insert(id, r);
        truth.insert(id, r);
    }
    for (CanvasObjectId id = 1; id <= 2000; id += 7) {
        index.remove(id);
        truth.remove(id);
    }
    index.insert(5000, QRectF(-1e6, -1e6, 2e6, 2e6)); // oversize path
    truth.insert(5000, QRectF(-1e6, -1e6, 2e6, 2e6));
    for (int q = 0; q < 200; ++q) {
        const QRectF region(next() * 4000 - 500, next() * 4000 - 500, next() * 600, next() * 600);
        QVector<CanvasObjectId> expected;
        for (auto it = truth.cbegin(); it != truth.cend(); ++it) {
            const QRectF &b = it.value();
            if (b.left() <= region.right() && region.left() <= b.right() && b.top() <= region.bottom()
                && region.top() <= b.bottom()) {
                expected << it.key();
            }
        }
        std::sort(expected.begin(), expected.end());
        QCOMPARE(index.query(region), expected);
    }
}

void TstCanvasCore::spatialIndexHugeBoundsStayBounded()
{
    CanvasGeometry::SpatialIndex index(64);
    const QRectF huge(-1e13, -1e13, 2e13, 2e13);
    index.insert(7, huge);
    QCOMPARE(index.size(), 1);
    QCOMPARE(index.query(QRectF(0, 0, 10, 10)), QVector<CanvasObjectId>{7});
    QCOMPARE(index.lastVisited(), 1);
    QCOMPARE(index.query(huge), QVector<CanvasObjectId>{7});
    QCOMPARE(index.lastVisited(), 1);
    index.remove(7);
    QVERIFY(index.query(QRectF(0, 0, 10, 10)).isEmpty());
}

void TstCanvasCore::documentCommitUndoDamage()
{
    CanvasDocument doc;
    QSignalSpy damaged(&doc, &CanvasDocumentAdapter::damaged);
    const quint64 r0 = doc.revision();
    const CanvasObjectId id = rect(doc, QRectF(10, 10, 20, 20));
    QCOMPARE(doc.revision(), r0 + 1);
    QCOMPARE(damaged.count(), 1);
    QVERIFY(doc.moveObjects({qulonglong(id)}, 100, 0));
    // Damage covers the old AND the new bounds.
    const auto regions = damaged.last().at(0).value<QVector<QRectF>>();
    QRectF all;
    for (const QRectF &r : regions) {
        all |= r;
    }
    QCOMPARE(all, QRectF(10, 10, 120, 20));
    QCOMPARE(doc.objectInfo(id).value("bounds").toRectF(), QRectF(110, 10, 20, 20));
    QVERIFY(doc.undo());
    QCOMPARE(doc.objectInfo(id).value("bounds").toRectF(), QRectF(10, 10, 20, 20));
    QVERIFY(doc.undo());
    QCOMPARE(doc.objectCount(), 0);
    QVERIFY(doc.redo());
    QCOMPARE(doc.objectCount(), 1);
    QCOMPARE(doc.revision(), r0 + 5);
}

void TstCanvasCore::staleEditRejected()
{
    CanvasDocument doc;
    const CanvasObjectId id = rect(doc, QRectF(0, 0, 10, 10));
    CanvasObject before = *doc.object(id);
    CanvasObject after = before;
    after.transform.translate(5, 0);
    QVERIFY(doc.moveObjects({qulonglong(id)}, 1, 1)); // someone else edits first
    const quint64 revision = doc.revision();
    CanvasEdit stale;
    stale.changes.append({id, before, after});
    QVERIFY(!doc.commit(stale));
    QCOMPARE(doc.revision(), revision);
    CanvasEdit orphan; // removes an object that no longer exists
    orphan.changes.append({999, before, std::nullopt});
    QVERIFY(!doc.commit(orphan));
}

void TstCanvasCore::overlapAndLayerOrder()
{
    CanvasDocument doc;
    const CanvasLayerId base = doc.activeLayer();
    const CanvasObjectId a = rect(doc, QRectF(0, 0, 100, 100));
    const CanvasObjectId b = rect(doc, QRectF(50, 50, 100, 100));
    auto hits = HitTestService::hitPoint(doc, QPointF(75, 75), 0);
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits[0].object, b); // later z first
    QCOMPARE(hits[1].object, a);
    const CanvasLayerId top = doc.addLayer(QStringLiteral("Top"));
    CanvasObject moved = *doc.object(a);
    moved.layer = top;
    CanvasEdit edit;
    edit.changes.append({a, *doc.object(a), moved});
    QVERIFY(doc.commit(edit));
    hits = HitTestService::hitPoint(doc, QPointF(75, 75), 0);
    QCOMPARE(hits[0].object, a); // a higher layer beats a higher z
    QCOMPARE(hits[0].layer, top);
    QVERIFY(base != top);
}

void TstCanvasCore::lockPolicies()
{
    CanvasDocument doc;
    const CanvasObjectId below = rect(doc, QRectF(0, 0, 100, 100));
    const CanvasObjectId locked = rect(doc, QRectF(0, 0, 100, 100), 0, true);
    CanvasHitPolicy policy;
    auto hits = HitTestService::hitPoint(doc, QPointF(50, 50), 0, policy);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].object, below);
    policy.locked = CanvasLockPolicy::Report;
    hits = HitTestService::hitPoint(doc, QPointF(50, 50), 0, policy);
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits[0].object, locked);
    QVERIFY(hits[0].locked);
    policy.locked = CanvasLockPolicy::Block;
    hits = HitTestService::hitPoint(doc, QPointF(50, 50), 0, policy);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].object, locked);
    // A locked LAYER locks its objects.
    QVERIFY(doc.setObjectLocked(locked, false));
    QVERIFY(doc.setLayerLocked(doc.activeLayer(), true));
    QVERIFY(HitTestService::hitPoint(doc, QPointF(50, 50), 0).isEmpty());
}

void TstCanvasCore::hiddenObjectsAndLayers()
{
    CanvasDocument doc;
    const CanvasObjectId a = rect(doc, QRectF(0, 0, 10, 10));
    QVERIFY(doc.setObjectVisible(a, false));
    QVERIFY(HitTestService::hitPoint(doc, QPointF(5, 5), 0).isEmpty());
    CanvasHitPolicy all;
    all.includeHidden = true;
    QCOMPARE(HitTestService::hitPoint(doc, QPointF(5, 5), 0, all).size(), 1);
    QVERIFY(doc.setObjectVisible(a, true));
    QVERIFY(doc.setLayerVisible(doc.activeLayer(), false));
    QVERIFY(HitTestService::hitPoint(doc, QPointF(5, 5), 0).isEmpty());
}

void TstCanvasCore::transformedShapes()
{
    CanvasDocument doc;
    // 100x100 square rotated 45 degrees about its centre at (200, 200).
    CanvasObject diamond;
    diamond.shape = CanvasShape::Rect;
    diamond.rect = QRectF(-50, -50, 100, 100);
    diamond.transform = QTransform().rotate(45) * QTransform::fromTranslate(200, 200);
    diamond.fill = QColor(Qt::blue);
    const CanvasObjectId d = doc.insert(diamond);
    const qreal half = 50 * std::sqrt(2.0); // centre to tip
    QCOMPARE(HitTestService::hitPoint(doc, QPointF(200 + half - 1, 200), 0).value(0).object, d);
    QVERIFY(HitTestService::hitPoint(doc, QPointF(200 + 40, 200 + 40), 0).isEmpty()); // outside the diamond, inside its bounds
    // Ellipse scaled 3x horizontally: exact containment, not bounds.
    CanvasObject ellipse;
    ellipse.shape = CanvasShape::Ellipse;
    ellipse.rect = QRectF(-10, -10, 20, 20);
    ellipse.transform = QTransform::fromScale(3, 1) * QTransform::fromTranslate(500, 100);
    ellipse.fill = QColor(Qt::green);
    const CanvasObjectId e = doc.insert(ellipse);
    QCOMPARE(HitTestService::hitPoint(doc, QPointF(500 + 29, 100), 0).value(0).object, e);
    QVERIFY(HitTestService::hitPoint(doc, QPointF(500 + 25, 100 + 8), 0).isEmpty()); // (25/30)^2+(8/10)^2 > 1
    // Shear.
    CanvasObject sheared = diamond;
    sheared.id = 0;
    sheared.z = 0;
    sheared.rect = QRectF(0, 0, 100, 100);
    sheared.transform = QTransform(1, 0, 1, 1, 1000, 0); // x' = x + y
    const CanvasObjectId s = doc.insert(sheared);
    QCOMPARE(HitTestService::hitPoint(doc, QPointF(1000 + 150, 90), 0).value(0).object, s);
    QVERIFY(HitTestService::hitPoint(doc, QPointF(1000 + 20, 90), 0).isEmpty());
}

void TstCanvasCore::degenerateObjectsAreSkipped()
{
    CanvasDocument doc;
    const CanvasObjectId below = rect(doc, QRectF(0, 0, 100, 100));
    const CanvasObjectId flat = rect(doc, QRectF(0, 0, 100, 100));
    QVERIFY(doc.setObjectTransform(flat, {1.0, 0.0, 0.0, 0.0, 0.0, 0.0})); // singular: y collapses
    QCOMPARE(doc.degenerateObjects(), QVector<CanvasObjectId>{flat});
    QVERIFY(doc.objectInfo(flat).value("degenerate").toBool());
    const auto hits = HitTestService::hitPoint(doc, QPointF(50, 0), 2);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].object, below);
    // Non-finite input is refused outright; the object keeps its matrix.
    const qreal inf = std::numeric_limits<qreal>::infinity();
    QVERIFY(!doc.setObjectTransform(below, {1.0, 0.0, 0.0, inf, 0.0, 0.0}));
    QVERIFY(!doc.objectInfo(below).value("degenerate").toBool());
    // Undo restores a usable matrix and the object is pickable again.
    QVERIFY(doc.undo());
    QVERIFY(doc.degenerateObjects().isEmpty());
    QCOMPARE(HitTestService::hitPoint(doc, QPointF(50, 50), 0).value(0).object, flat);
}

void TstCanvasCore::toleranceScalesWithZoom()
{
    CanvasDocument doc;
    const CanvasObjectId id = rect(doc, QRectF(0, 0, 100, 100));
    const QPointF probe(103, 50); // 3 document units right of the edge
    const qreal pixels = 4;
    QCOMPARE(HitTestService::hitPoint(doc, probe, HitTestService::documentTolerance(pixels, 1.0)).value(0).object, id);
    QVERIFY(HitTestService::hitPoint(doc, probe, HitTestService::documentTolerance(pixels, 2.0)).isEmpty()); // 6 px away
    QCOMPARE(HitTestService::hitPoint(doc, probe, HitTestService::documentTolerance(pixels, 0.5)).value(0).part,
             CanvasPart::Segment);
    QCOMPARE(HitTestService::documentTolerance(4, 0), 0.0);
}

void TstCanvasCore::subobjectParts()
{
    CanvasDocument doc;
    const CanvasObjectId r = rect(doc, QRectF(0, 0, 100, 100));
    auto hit = HitTestService::hitPoint(doc, QPointF(99, 1), 3).value(0);
    QCOMPARE(hit.object, r);
    QCOMPARE(hit.part, CanvasPart::Vertex);
    QCOMPARE(hit.index, 1); // top-right corner
    hit = HitTestService::hitPoint(doc, QPointF(50, 101), 3).value(0);
    QCOMPARE(hit.part, CanvasPart::Segment);
    QCOMPARE(hit.index, 2); // bottom edge
    hit = HitTestService::hitPoint(doc, QPointF(50, 50), 3).value(0);
    QCOMPARE(hit.part, CanvasPart::Fill);
    const CanvasObjectId line = doc.addPolyline({QPointF(200, 0), QPointF(300, 0), QPointF(300, 100)},
                                                QColor(Qt::black), 4);
    hit = HitTestService::hitPoint(doc, QPointF(301, 60), 1).value(0);
    QCOMPARE(hit.object, line);
    QCOMPARE(hit.part, CanvasPart::Segment);
    QCOMPARE(hit.index, 1);
    // Image objects resolve to the tile under the probe.
    const qulonglong imageId = doc.populateFixture(0, 3, true);
    QVERIFY(imageId != 0);
    const CanvasObject *image = nullptr;
    for (CanvasObjectId id : doc.objectIds()) {
        if (doc.object(id)->shape == CanvasShape::Image) {
            image = doc.object(id);
        }
    }
    QVERIFY(image);
    // The fixture maps 2048 px onto the page width; probe pixel (300, 600).
    const QPointF docPoint = image->transform.map(QPointF(300, 600));
    hit = HitTestService::hitPoint(doc, docPoint, 0).value(0);
    QCOMPARE(hit.part, CanvasPart::ImageTile);
    QCOMPARE(hit.index, 2 * 8 + 1); // row 2, column 1 of an 8-column grid
}

void TstCanvasCore::pickingAcrossViews_data()
{
    QTest::addColumn<qreal>("zoom");
    QTest::addColumn<qreal>("rotation");
    QTest::addColumn<qreal>("dpr");
    for (qreal zoom : {0.25, 1.0, 6.0}) {
        for (qreal rotation : {0.0, 37.0, -90.0}) {
            for (qreal dpr : {1.0, 1.5, 2.0}) {
                QTest::addRow("z%g-r%g-dpr%g", zoom, rotation, dpr) << zoom << rotation << dpr;
            }
        }
    }
}

void TstCanvasCore::pickingAcrossViews()
{
    QFETCH(qreal, zoom);
    QFETCH(qreal, rotation);
    QFETCH(qreal, dpr);
    CanvasDocument doc;
    doc.populateFixture(400, 11, false);
    const CanvasViewPose pose{QPointF(480, 480), zoom, rotation};
    const QSizeF view(900, 700);
    const QTransform toDevice = CT::documentToView(pose, view) * CT::viewToDevice(dpr);
    const QTransform fromDevice = CT::viewToDevice(1.0 / dpr) * CT::viewToDocument(pose, view);
    const qreal tolerance = HitTestService::documentTolerance(4, zoom);
    // Probe object centres and a grid; the device-space round trip must pick
    // exactly what a direct document-space pick does.
    for (int i = 0; i < 400; ++i) {
        const QPointF docPoint(i % 20 * 48.0 + 17.0, i / 20 * 48.0 + 13.0);
        const QPointF device = toDevice.map(docPoint);
        const QPointF back = fromDevice.map(device);
        QVERIFY(near(back, docPoint, 1e-9 * 1000));
        const auto direct = HitTestService::hitPoint(doc, docPoint, tolerance);
        const auto viaDevice = HitTestService::hitPoint(doc, back, tolerance);
        QCOMPARE(viaDevice.size(), direct.size());
        for (int k = 0; k < direct.size(); ++k) {
            QCOMPARE(viaDevice[k].object, direct[k].object);
            QCOMPARE(viaDevice[k].part, direct[k].part);
            QCOMPARE(viaDevice[k].index, direct[k].index);
        }
    }
}

void TstCanvasCore::regionPicking()
{
    CanvasDocument doc;
    const CanvasObjectId a = rect(doc, QRectF(0, 0, 10, 10));
    const CanvasObjectId b = rect(doc, QRectF(20, 0, 10, 10));
    const CanvasObjectId c = doc.addPolyline({QPointF(0, 40), QPointF(100, 40)}, QColor(Qt::black), 0);
    const QPolygonF region(QRectF(-5, -5, 30, 50)); // covers a, half of b, crosses c
    QCOMPARE(HitTestService::hitRegion(doc, region, CanvasRegionMode::Intersect),
             (QVector<CanvasObjectId>{c, b, a}));
    QCOMPARE(HitTestService::hitRegion(doc, region, CanvasRegionMode::Contain), QVector<CanvasObjectId>{a});
    // A rotated region (as from a turned view) that only touches b's corner.
    QPolygonF diamond;
    diamond << QPointF(35, 5) << QPointF(40, 0) << QPointF(45, 5) << QPointF(40, 10);
    QVERIFY(HitTestService::hitRegion(doc, diamond, CanvasRegionMode::Intersect).isEmpty());
    diamond.translate(-9, 0); // left tip now at x = 26, inside b
    QCOMPARE(HitTestService::hitRegion(doc, diamond, CanvasRegionMode::Intersect), QVector<CanvasObjectId>{b});
}

void TstCanvasCore::fixtureDeterminism()
{
    CanvasDocument one;
    CanvasDocument two;
    one.populateFixture(10000, 7, true);
    two.populateFixture(10000, 7, true);
    QCOMPARE(one.objectCount(), 10001);
    QCOMPARE(one.contentBounds(), two.contentBounds());
    CanvasHitStats stats;
    for (int i = 0; i < 200; ++i) {
        const QPointF p(i * 23.5, i * 21.25);
        const auto a = HitTestService::hitPoint(one, p, 2, {}, &stats);
        const auto b = HitTestService::hitPoint(two, p, 2);
        QCOMPARE(a.size(), b.size());
        for (int k = 0; k < a.size(); ++k) {
            QCOMPARE(a[k].object, b[k].object);
        }
    }
    // Bounded work: each probe examines only nearby index entries.
    QVERIFY2(stats.candidates < 200 * 16, qPrintable(QString::number(stats.candidates)));
    qInfo("10k fixture: 200 probes examined %lld candidates, tested %lld",
          qlonglong(stats.candidates), qlonglong(stats.tested));
}

QTEST_GUILESS_MAIN(TstCanvasCore)
#include "tst_canvas_core.moc"
