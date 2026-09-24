// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_transform_controller.h"

#include <QLineF>
#include <QSignalSpy>
#include <QTest>
#include <QtMath>
#include <cmath>
#include <functional>

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

bool sameTransform(const QTransform &a, const QTransform &b)
{
    const QPointF probes[] = {{0, 0}, {10, 0}, {0, 10}};
    for (const QPointF &p : probes)
        if (QLineF(a.map(p), b.map(p)).length() > 1e-9) return false;
    return true;
}

// One route's result. The route must commit exactly once (one revision) and
// one undo must restore the start exactly, leaving that step to redo.
struct Outcome {
    QTransform a;
    QTransform b;
    QString label;
};

// A rotated, zoomed pose so view-pixel keyboard steps are non-trivial.
struct Rig {
    CanvasDocument document;
    ToolController tools;
    CanvasViewState pose;
    CanvasTransformController transform;
    CanvasObjectId a = 0;
    CanvasObjectId b = 0;
    Rig()
    {
        a = square(document, 5, 7);
        b = square(document, 25, 7);
        tools.setDocument(&document);
        tools.setSelection({a, b});
        pose.setZoom(2);
        pose.setRotation(30);
        transform.setDocument(&document);
        transform.setTools(&tools);
        transform.setViewState(&pose);
    }
    // Maps a document delta to the view-pixel step nudge() inverts.
    QPointF viewStep(QPointF documentDelta) const
    {
        const qreal angle = qDegreesToRadians(pose.rotation());
        const qreal c = std::cos(angle);
        const qreal s = std::sin(angle);
        return {pose.zoom() * (c * documentDelta.x() - s * documentDelta.y()),
                pose.zoom() * (s * documentDelta.x() + c * documentDelta.y())};
    }
    bool run(const std::function<bool()> &operation, Outcome *outcome)
    {
        const QTransform beforeA = document.object(a)->transform;
        const QTransform beforeB = document.object(b)->transform;
        const quint64 revision = document.revision();
        if (!operation() || transform.active() || document.revision() != revision + 1) return false;
        *outcome = {document.object(a)->transform, document.object(b)->transform, document.undoText()};
        return document.undo() && document.object(a)->transform == beforeA
            && document.object(b)->transform == beforeB && document.redoText() == outcome->label;
    }
};
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
    void rotationPivotPreviewAndUndo();
    void handleKeyboardAndInspectorShareOneOperation();
    void shiftMoveConstrainsToDocumentDirections();
    void refusedNumericEntryLeavesNoGesture();
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

void TstCanvasTransform::rotationPivotPreviewAndUndo()
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
    const quint64 revision = document.revision();

    transform.setPivot({0, 0});
    QCOMPARE(transform.pivot(), QPointF(0, 0));
    QVERIFY(transform.update(QStringLiteral("rotate"), {10, 0}, {0, 10}));
    QCOMPARE(document.revision(), revision);
    const QVariantList corners = transform.previewCorners();
    QCOMPARE(corners.size(), 4);
    QVERIFY(QLineF(corners[1].toPointF(), QPointF(0, 30)).length() < 1e-8);
    QVERIFY(transform.finish(QStringLiteral("rotate")));
    QCOMPARE(document.revision(), revision + 1);
    QVERIFY(QLineF(document.object(a)->transform.map(QPointF(5, 5)),
                   QPointF(-5, 5)).length() < 1e-8);
    QVERIFY(QLineF(document.object(b)->transform.map(QPointF(5, 5)),
                   QPointF(-5, 25)).length() < 1e-8);
    QVERIFY(document.undo());
    QCOMPARE(document.revision(), revision + 2);
    QCOMPARE(document.object(a)->transform.map(QPointF(5, 5)), QPointF(5, 5));

    transform.resetPivot();
    QCOMPARE(transform.pivot(), QPointF(15, 5));
    QVERIFY(transform.rotateBy(90));
    QVERIFY(QLineF(document.object(a)->transform.map(QPointF(5, 5)),
                   QPointF(15, -5)).length() < 1e-8);
    QVERIFY(QLineF(document.object(b)->transform.map(QPointF(5, 5)),
                   QPointF(15, 15)).length() < 1e-8);
    QVERIFY(document.undo());

    const QPointF start(15, -5);
    const QPointF pointer(18.42, -4.4); // about 20 degrees from the top
    QVERIFY(transform.update(QStringLiteral("rotate"), start, pointer, Qt::ShiftModifier));
    QVERIFY(transform.finish(QStringLiteral("rotate")));
    const QPointF rotated = document.object(a)->transform.map(QPointF(5, 5));
    QVERIFY(std::abs(rotated.x() - 5.3407417) < 1e-5);
    QVERIFY(std::abs(rotated.y() - 2.4118095) < 1e-5);
}

void TstCanvasTransform::handleKeyboardAndInspectorShareOneOperation()
{
    Rig rig;
    QCOMPARE(rig.transform.selectionBounds(), QRectF(5, 7, 30, 10));
    Outcome pointer, keyboard, inspector;
    // Move by (6, -4) document units.
    QVERIFY(rig.run([&] {
        return rig.transform.update(QStringLiteral("move"), {5, 7}, {11, 3})
            && rig.transform.finish(QStringLiteral("move"));
    }, &pointer));
    const QPointF moveStep = rig.viewStep({6, -4});
    QVERIFY(rig.run([&] {
        return rig.transform.nudge(QStringLiteral("move"), moveStep.x(), moveStep.y());
    }, &keyboard));
    QVERIFY(rig.run([&] { return rig.transform.setBounds(11, 3, 30, 10); }, &inspector));
    for (const Outcome *o : {&keyboard, &inspector}) {
        QVERIFY(sameTransform(o->a, pointer.a));
        QVERIFY(sameTransform(o->b, pointer.b));
        QCOMPARE(o->label, QStringLiteral("Move selection"));
    }
    QVERIFY(sameTransform(pointer.b, QTransform::fromTranslate(31, 3)));

    // Resize the right edge from 35 to 50 (width 30 -> 45).
    QVERIFY(rig.run([&] {
        return rig.transform.update(QStringLiteral("right"), {35, 12}, {50, 12})
            && rig.transform.finish(QStringLiteral("right"));
    }, &pointer));
    const QPointF resizeStep = rig.viewStep({15, 0});
    QVERIFY(rig.run([&] {
        return rig.transform.nudge(QStringLiteral("right"), resizeStep.x(), resizeStep.y());
    }, &keyboard));
    QVERIFY(rig.run([&] { return rig.transform.setBounds(5, 7, 45, 10); }, &inspector));
    for (const Outcome *o : {&keyboard, &inspector}) {
        QVERIFY(sameTransform(o->a, pointer.a));
        QVERIFY(sameTransform(o->b, pointer.b));
        QCOMPARE(o->label, QStringLiteral("Resize selection"));
    }
    QVERIFY(QLineF(pointer.b.map(QPointF(10, 10)), QPointF(50, 17)).length() < 1e-9);

    // Rotate 90 degrees about the selection center (20, 12).
    QCOMPARE(rig.transform.rotation(), 0.0);
    QVERIFY(rig.run([&] {
        return rig.transform.update(QStringLiteral("rotate"), {30, 12}, {20, 22})
            && rig.transform.finish(QStringLiteral("rotate"));
    }, &pointer));
    QVERIFY(rig.run([&] { return rig.transform.rotateBy(90); }, &keyboard));
    QVERIFY(rig.run([&] { return rig.transform.rotateTo(90); }, &inspector));
    for (const Outcome *o : {&keyboard, &inspector}) {
        QVERIFY(sameTransform(o->a, pointer.a));
        QVERIFY(sameTransform(o->b, pointer.b));
        QCOMPARE(o->label, QStringLiteral("Rotate selection"));
    }
    QVERIFY(QLineF(pointer.a.map(QPointF(5, 5)), QPointF(20, 2)).length() < 1e-9);
    QVERIFY(rig.transform.rotateTo(90));
    QVERIFY(std::abs(rig.transform.rotation() - 90) < 1e-9);
}

void TstCanvasTransform::shiftMoveConstrainsToDocumentDirections()
{
    Rig rig;
    const QPointF start(5, 7);
    const struct {
        QPointF pointer;
        QPointF delta;
    } cases[] = {{{15, 10}, {10, 0}}, {{8, 17}, {0, 10}}, {{15, 16}, {9.5, 9.5}},
                 {{-5, 17}, {-10, 10}}, {{0, 7.5}, {-5, 0}}};
    for (const auto &c : cases) {
        QVERIFY(rig.transform.update(QStringLiteral("move"), start, c.pointer, Qt::ShiftModifier));
        QCOMPARE(rig.transform.previewBounds().topLeft(), start + c.delta);
        QVERIFY(rig.transform.cancel());
    }
    QVERIFY(rig.transform.update(QStringLiteral("move"), start, {15, 10}));
    QCOMPARE(rig.transform.previewBounds().topLeft(), QPointF(15, 10)); // unconstrained
    QVERIFY(rig.transform.cancel());
}

void TstCanvasTransform::refusedNumericEntryLeavesNoGesture()
{
    Rig rig;
    const quint64 revision = rig.document.revision();
    QVERIFY(!rig.transform.setBounds(5, 7, 0, 10)); // zero width is singular
    QVERIFY(!rig.transform.setBounds(qQNaN(), 7, 30, 10));
    QVERIFY(!rig.transform.rotateTo(qInf()));
    QVERIFY(!rig.transform.active());
    QCOMPARE(rig.document.revision(), revision);
    QVERIFY(rig.transform.setBounds(5, 7, 30, 10)); // unchanged bounds: no edit, no step
    QCOMPARE(rig.document.revision(), revision);
    QVERIFY(rig.transform.update(QStringLiteral("move"), {5, 7}, {6, 7}));
    QVERIFY(!rig.transform.setBounds(1, 1, 30, 10)); // a pointer gesture owns the edit
    QVERIFY(!rig.transform.nudge(QStringLiteral("move"), 4, 0));
    QVERIFY(rig.transform.finish(QStringLiteral("move")));
    rig.tools.setSelection({});
    QVERIFY(!rig.transform.setBounds(0, 0, 10, 10));
    QVERIFY(!rig.transform.rotateTo(10));
}

QTEST_GUILESS_MAIN(TstCanvasTransform)
#include "tst_canvas_transform.moc"
