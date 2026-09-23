// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_snap_model.h"
#include <QTest>

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
