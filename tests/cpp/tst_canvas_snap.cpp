// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_snap_model.h"
#include <QTest>
#include <cmath>

using namespace QindaTK;

namespace {
CanvasObjectId addRect(CanvasDocument &document)
{
    CanvasObject object;
    object.shape = CanvasShape::Rect;
    object.rect = QRectF(0, 0, 100, 100);
    object.transform = QTransform::fromTranslate(100, 100);
    object.fill = QColor(Qt::red);
    return document.insert(object, false);
}

QVariantMap axis(const QVariantMap &result, const char *name)
{ return result.value(QLatin1String(name)).toMap(); }
}

class TstCanvasSnap final : public QObject {
    Q_OBJECT
private slots:
    void sourcePriorityAndExclusion()
    {
        CanvasDocument document;
        const CanvasObjectId objectId = addRect(document);
        CanvasViewState view;
        CanvasSnapModel snap;
        snap.setDocument(&document);
        snap.setViewState(&view);
        snap.setGridSpacing(10);
        snap.setGuides({QVariantMap{{"id", "guide-1"}, {"orientation", "vertical"},
                                    {"position", 103.0}}});

        auto result = snap.snapPoint(QPointF(102, 104));
        QCOMPARE(result.value("point").toPointF(), QPointF(103, 100));
        QCOMPARE(axis(result, "x").value("source").toString(), QStringLiteral("guide"));
        QCOMPARE(axis(result, "y").value("source").toString(), QStringLiteral("object"));
        QCOMPARE(axis(result, "y").value("targetId").toString(), QString::number(objectId));

        result = snap.snapPoint(QPointF(102, 104), {QString::number(objectId)});
        QCOMPARE(result.value("point").toPointF(), QPointF(103, 100));
        QCOMPARE(axis(result, "y").value("source").toString(), QStringLiteral("grid"));
        snap.setEnabled(false);
        result = snap.snapPoint(QPointF(102, 104));
        QCOMPARE(result.value("point").toPointF(), QPointF(102, 104));
        QVERIFY(!result.value("snapped").toBool());
    }

    void pixelToleranceTracksZoomAndVisibility()
    {
        CanvasDocument document;
        const CanvasObjectId id = addRect(document);
        CanvasViewState view;
        view.setZoom(4);
        CanvasSnapModel snap;
        snap.setDocument(&document);
        snap.setViewState(&view);
        QCOMPARE(snap.snapPoint(QPointF(101.9, 101.9)).value("point").toPointF(),
                 QPointF(100, 100));
        QVERIFY(!snap.snapPoint(QPointF(102.1, 102.1)).value("snapped").toBool());
        QVERIFY(document.setObjectVisible(id, false));
        QVERIFY(!snap.snapPoint(QPointF(101.9, 101.9)).value("snapped").toBool());
    }

    void projectsContinuouslyOntoLongOutlineEdge()
    {
        CanvasDocument document;
        CanvasObject object;
        object.shape = CanvasShape::Rect;
        object.rect = QRectF(0, 0, 1000, 20);
        object.transform = QTransform::fromTranslate(100, 100);
        object.fill = QColor(Qt::red);
        const CanvasObjectId id = document.insert(object, false);
        QVERIFY(document.candidates(QRectF(670, 99, 6, 6)).contains(id));

        CanvasViewState view;
        CanvasSnapModel snap;
        snap.setDocument(&document);
        snap.setViewState(&view);
        snap.setTolerancePixels(3);

        // This point is far from both endpoints and the edge midpoint.
        const QVariantMap result = snap.snapPoint(QPointF(673, 102));
        QCOMPARE(result.value("point").toPointF(), QPointF(673, 100));
        QCOMPARE(axis(result, "y").value("source").toString(), QStringLiteral("object"));
        QCOMPARE(axis(result, "y").value("targetId").toString(), QString::number(id));
        QCOMPARE(axis(result, "y").value("feature").toString(), QStringLiteral("edge:0"));
    }

    void projectsOntoRotatedOutlineEdge()
    {
        CanvasDocument document;
        CanvasObject object;
        object.shape = CanvasShape::Rect;
        object.rect = QRectF(0, 0, 300, 10);
        object.transform.rotate(37);
        object.transform.translate(500, 300);
        object.fill = QColor(Qt::red);
        const CanvasObjectId id = document.insert(object, false);
        const QPointF start = object.transform.map(object.rect.topLeft());
        const QPointF end = object.transform.map(object.rect.topRight());
        const QPointF edge = end - start;
        const QPointF midpoint = (start + end) / 2;
        const QPointF offset(-edge.y() / std::hypot(edge.x(), edge.y()) * 2,
                             edge.x() / std::hypot(edge.x(), edge.y()) * 2);

        CanvasViewState view;
        CanvasSnapModel snap;
        snap.setDocument(&document);
        snap.setViewState(&view);
        snap.setTolerancePixels(3);
        const QVariantMap result = snap.snapPoint(midpoint + offset);
        const QPointF snapped = result.value("point").toPointF();
        QVERIFY(std::abs(snapped.x() - midpoint.x()) < 1e-8);
        QVERIFY(std::abs(snapped.y() - midpoint.y()) < 1e-8);
        QCOMPARE(axis(result, "x").value("targetId").toString(), QString::number(id));
        QCOMPARE(axis(result, "x").value("feature").toString(), QStringLiteral("edge:0"));
        QCOMPARE(axis(result, "y").value("feature").toString(), QStringLiteral("edge:0"));
    }

    void equalDistanceUsesStableGuideIdentity()
    {
        CanvasViewState view;
        CanvasSnapModel snap;
        snap.setViewState(&view);
        const QVariantMap a{{"id", "a"}, {"orientation", "vertical"}, {"position", 99.0}};
        const QVariantMap b{{"id", "b"}, {"orientation", "vertical"}, {"position", 101.0}};
        snap.setGuides({b, a});
        QCOMPARE(snap.snapPoint(QPointF(100, 0)).value("point").toPointF(), QPointF(99, 0));
        snap.setGuides({a, b});
        QCOMPARE(snap.snapPoint(QPointF(100, 0)).value("point").toPointF(), QPointF(99, 0));
    }
};

QTEST_GUILESS_MAIN(TstCanvasSnap)
#include "tst_canvas_snap.moc"
