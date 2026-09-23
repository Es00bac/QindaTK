// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_transform_controller.h"

#include <QSignalSpy>
#include <QTest>
#include <cmath>

using namespace QindaTK;

namespace {
CanvasObjectId square(CanvasDocument &document, qreal x, qreal y)
{
    CanvasObject object;
    object.shape = CanvasShape::Rect;
    object.rect = QRectF(0, 0, 10, 10);
    object.transform = QTransform::fromTranslate(x, y);
    object.fill = Qt::red;
    return document.insert(object, false);
}
}

class TstCanvasTransform : public QObject {
    Q_OBJECT
private slots:
    void groupMoveResizeAndUndo();
    void negativeScaleAndSingularRejection();
    void snapUsesRawPointerAndExcludesSelection();
    void cancelAndExternalEdit();
    void keyboardNudgeUsesViewPose();
    void thinPolylineCanMove();
};

void TstCanvasTransform::groupMoveResizeAndUndo()
{
    CanvasDocument document;
    const CanvasObjectId a = square(document, 0, 0);
    const CanvasObjectId b = square(document, 20, 0);
    ToolController tools;
    tools.setDocument(&document);
    tools.setSelection({a, b});
    CanvasTransformController transform;
    transform.setDocument(&document);
    transform.setTools(&tools);
    QSignalSpy finished(&transform, &CanvasTransformController::gestureFinished);
    const quint64 revision = document.revision();

    QCOMPARE(transform.selectionBounds(), QRectF(0, 0, 30, 10));
    QVERIFY(transform.update(QStringLiteral("move"), {0, 0}, {10, 15}));
    QCOMPARE(transform.previewBounds(), QRectF(10, 15, 30, 10));
    QCOMPARE(document.revision(), revision); // preview never mutates canonical objects
    QVERIFY(transform.finish(QStringLiteral("move")));
    QCOMPARE(document.revision(), revision + 1);
    QCOMPARE(finished.size(), 1);
    QCOMPARE(document.objectInfo(a).value("bounds").toRectF(), QRectF(10, 15, 10, 10));
    QCOMPARE(document.objectInfo(b).value("bounds").toRectF(), QRectF(30, 15, 10, 10));
    QVERIFY(document.undo());
    QCOMPARE(document.objectInfo(a).value("bounds").toRectF(), QRectF(0, 0, 10, 10));
    QCOMPARE(document.objectInfo(b).value("bounds").toRectF(), QRectF(20, 0, 10, 10));

    QVERIFY(transform.update(QStringLiteral("right"), {30, 5}, {60, 5}));
    QCOMPARE(transform.previewBounds(), QRectF(0, 0, 60, 10));
    QVERIFY(transform.finish(QStringLiteral("right")));
    QCOMPARE(document.objectInfo(a).value("bounds").toRectF(), QRectF(0, 0, 20, 10));
    QCOMPARE(document.objectInfo(b).value("bounds").toRectF(), QRectF(40, 0, 20, 10));
    QVERIFY(document.undo());
    QCOMPARE(document.objectInfo(a).value("bounds").toRectF(), QRectF(0, 0, 10, 10));
}

void TstCanvasTransform::negativeScaleAndSingularRejection()
{
    CanvasDocument document;
    const CanvasObjectId id = square(document, 0, 0);
    ToolController tools;
    tools.setDocument(&document);
    tools.setSelection({id});
    CanvasTransformController transform;
    transform.setDocument(&document);
    transform.setTools(&tools);
    const quint64 revision = document.revision();
    QVERIFY(!transform.update(QStringLiteral("right"), {10, 5}, {0, 5}));
    QVERIFY(transform.cancel());
    QCOMPARE(document.revision(), revision);
    QVERIFY(transform.update(QStringLiteral("right"), {10, 5}, {-10, 5}));
    QCOMPARE(transform.previewBounds(), QRectF(-10, 0, 10, 10));
    QVERIFY(transform.finish(QStringLiteral("right")));
    QCOMPARE(document.objectInfo(id).value("bounds").toRectF(), QRectF(-10, 0, 10, 10));
    QVERIFY(document.undo());
    QCOMPARE(document.objectInfo(id).value("bounds").toRectF(), QRectF(0, 0, 10, 10));
}

void TstCanvasTransform::snapUsesRawPointerAndExcludesSelection()
{
    CanvasDocument document;
    const CanvasObjectId id = square(document, 0, 0);
    ToolController tools;
    tools.setDocument(&document);
    tools.setSelection({id});
    CanvasViewState view;
    CanvasSnapModel snap;
    snap.setDocument(&document);
    snap.setViewState(&view);
    snap.setGuides({QVariantMap{{QStringLiteral("id"), QStringLiteral("guide-20")},
                                {QStringLiteral("orientation"), QStringLiteral("vertical")},
                                {QStringLiteral("position"), 20.0}}});
    CanvasTransformController transform;
    transform.setDocument(&document);
    transform.setTools(&tools);
    transform.setSnapModel(&snap);
    QVERIFY(transform.update(QStringLiteral("move"), {0, 0}, {19, 0}));
    QCOMPARE(transform.previewBounds(), QRectF(20, 0, 10, 10));
    snap.setEnabled(false);
    QVERIFY(transform.update(QStringLiteral("move"), {0, 0}, {19, 0}));
    QCOMPARE(transform.previewBounds(), QRectF(19, 0, 10, 10));
    QVERIFY(transform.finish(QStringLiteral("move")));
    QCOMPARE(document.objectInfo(id).value("bounds").toRectF(), QRectF(19, 0, 10, 10));
}

void TstCanvasTransform::cancelAndExternalEdit()
{
    CanvasDocument document;
    const CanvasObjectId id = square(document, 0, 0);
    ToolController tools;
    tools.setDocument(&document);
    tools.setSelection({id});
    CanvasTransformController transform;
    transform.setDocument(&document);
    transform.setTools(&tools);
    const quint64 revision = document.revision();
    QVERIFY(transform.update(QStringLiteral("move"), {0, 0}, {10, 10}));
    QVERIFY(transform.cancel(QStringLiteral("move")));
    QCOMPARE(document.revision(), revision);
    QVERIFY(transform.update(QStringLiteral("move"), {0, 0}, {10, 10}));
    QVERIFY(document.moveObjects({QVariant::fromValue(qulonglong(id))}, 5, 0));
    QVERIFY(!transform.active());
    QVERIFY(!transform.finish(QStringLiteral("move")));
    QCOMPARE(document.objectInfo(id).value("bounds").toRectF(), QRectF(5, 0, 10, 10));
}

void TstCanvasTransform::keyboardNudgeUsesViewPose()
{
    CanvasDocument document;
    const CanvasObjectId id = square(document, 0, 0);
    ToolController tools;
    tools.setDocument(&document);
    tools.setSelection({id});
    CanvasViewState view;
    view.setZoom(2);
    view.setRotation(90);
    CanvasTransformController transform;
    transform.setDocument(&document);
    transform.setTools(&tools);
    transform.setViewState(&view);
    QVERIFY(transform.nudge(QStringLiteral("move"), 20, 0));
    const QRectF bounds = document.objectInfo(id).value("bounds").toRectF();
    QVERIFY(std::abs(bounds.x()) < 1e-8);
    QVERIFY(std::abs(bounds.y() + 10) < 1e-8);
}

void TstCanvasTransform::thinPolylineCanMove()
{
    CanvasDocument document;
    CanvasObject line;
    line.shape = CanvasShape::Polyline;
    line.points = {{0, 0}, {10, 0}};
    line.stroke = Qt::red;
    const CanvasObjectId id = document.insert(line, false);
    ToolController tools;
    tools.setDocument(&document);
    tools.setSelection({id});
    CanvasTransformController transform;
    transform.setDocument(&document);
    transform.setTools(&tools);
    QVERIFY(transform.update(QStringLiteral("move"), {0, 0}, {5, 5}));
    QVERIFY(transform.finish(QStringLiteral("move")));
    QCOMPARE(document.object(id)->transform.map(QPointF(0, 0)), QPointF(5, 5));
}

QTEST_GUILESS_MAIN(TstCanvasTransform)
#include "tst_canvas_transform.moc"
