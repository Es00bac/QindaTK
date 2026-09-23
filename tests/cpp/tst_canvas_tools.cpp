// SPDX-License-Identifier: LGPL-3.0-or-later
// ToolController gesture state machines: commit once, cancel cleanly.
#include "tool_controller.h"

#include <QSignalSpy>
#include <QTest>

using namespace QindaTK;

namespace {

CanvasObjectId square(CanvasDocument &doc, QRectF r, bool locked = false)
{
    CanvasObject o;
    o.shape = CanvasShape::Rect;
    o.rect = QRectF(QPointF(0, 0), r.size());
    o.transform = QTransform::fromTranslate(r.x(), r.y());
    o.fill = QColor(Qt::red);
    o.locked = locked;
    return doc.insert(o, false);
}

// Snapshot of everything a canceled gesture must leave untouched.
struct Snapshot {
    quint64 revision;
    int objects;
    bool canUndo;
    explicit Snapshot(const CanvasDocument &d) : revision(d.revision()), objects(d.objectCount()), canUndo(d.canUndo()) {}
    bool same(const CanvasDocument &d) const
    {
        return revision == d.revision() && objects == d.objectCount() && canUndo == d.canUndo();
    }
};

} // namespace

class TstCanvasTools : public QObject {
    Q_OBJECT
private slots:
    void rectangleCommitsOnce();
    void clickIsNotAnEdit();
    void escapeCancels();
    void lostGrabCancelsOnlyOwner();
    void documentSwitchCancels();
    void providerDestroyedCancels();
    void externalEditCancels();
    void toolSwitchAndTemporaryTools();
    void secondViewIsRejected();
    void selectMoveMarquee();
    void lockedObjectsAreNotMoved();
    void penStrokeAndEraser();
    void lockedLayerDeclines();
    void controllerDestroyedMidGesture();
};

void TstCanvasTools::rectangleCommitsOnce()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    tools.setActiveTool(QStringLiteral("rectangle"));
    QSignalSpy committed(&tools, &ToolController::gestureCommitted);
    QSignalSpy edited(&doc, &CanvasDocumentAdapter::edited);
    const quint64 r0 = doc.revision();
    QVERIFY(tools.begin(QStringLiteral("v"), 10, 20));
    QCOMPARE(tools.state(), QStringLiteral("active"));
    for (int i = 1; i <= 50; ++i) {
        QVERIFY(tools.update(QStringLiteral("v"), 10 + i, 20 + i));
    }
    QCOMPARE(tools.previewMap().value("kind").toString(), QStringLiteral("rect"));
    QCOMPARE(doc.revision(), r0); // previews never reach the document
    QVERIFY(tools.end(QStringLiteral("v"), 110, 70));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("committed"));
    QCOMPARE(committed.count(), 1);
    QCOMPARE(edited.count(), 1);
    QCOMPARE(doc.revision(), r0 + 1);
    QCOMPARE(doc.objectCount(), 1);
    const CanvasObjectId id = doc.objectIds().first();
    QCOMPARE(doc.objectInfo(id).value("bounds").toRectF().adjusted(1, 1, -1, -1), QRectF(10, 20, 100, 50));
    QCOMPARE(doc.undoText(), QStringLiteral("Draw rectangle"));
    QVERIFY(doc.undo());
    QCOMPARE(doc.objectCount(), 0);
    QCOMPARE(tools.state(), QStringLiteral("idle"));
}

void TstCanvasTools::clickIsNotAnEdit()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    tools.setActiveTool(QStringLiteral("ellipse"));
    const Snapshot before(doc);
    QVERIFY(tools.begin(QStringLiteral("v"), 5, 5));
    QVERIFY(tools.end(QStringLiteral("v"), 5.5, 5.5));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("completed"));
    QVERIFY(before.same(doc));
}

void TstCanvasTools::escapeCancels()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    tools.setActiveTool(QStringLiteral("pen"));
    QSignalSpy canceled(&tools, &ToolController::gestureCanceled);
    const Snapshot before(doc);
    QVERIFY(tools.begin(QStringLiteral("v"), 0, 0, ToolController::Pen, 0, 0.5));
    for (int i = 0; i < 30; ++i) {
        tools.update(QStringLiteral("v"), i * 3.0, i * 2.0, 0.7);
    }
    QCOMPARE(tools.previewMap().value("points").toList().size(), 30);
    tools.escape();
    QCOMPARE(tools.state(), QStringLiteral("idle"));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("canceled:escape"));
    QCOMPARE(tools.previewMap().value("kind").toString(), QStringLiteral("none"));
    QCOMPARE(canceled.count(), 1);
    QVERIFY(before.same(doc));
    // The late release from the platform is ignored, not committed.
    QVERIFY(!tools.end(QStringLiteral("v"), 90, 60));
    QVERIFY(before.same(doc));
    // And the tool is immediately usable again (nothing stuck).
    QVERIFY(tools.begin(QStringLiteral("v"), 0, 0));
    tools.update(QStringLiteral("v"), 20, 0);
    QVERIFY(tools.end(QStringLiteral("v"), 40, 0));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("committed"));
}

void TstCanvasTools::lostGrabCancelsOnlyOwner()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    tools.setActiveTool(QStringLiteral("rectangle"));
    QVERIFY(tools.begin(QStringLiteral("a"), 0, 0));
    tools.lostGrab(QStringLiteral("b")); // not the owner
    QVERIFY(tools.gestureActive());
    tools.lostGrab(QStringLiteral("a"));
    QVERIFY(!tools.gestureActive());
    QCOMPARE(tools.lastOutcome(), QStringLiteral("canceled:lost-grab"));
    QCOMPARE(doc.objectCount(), 0);
}

void TstCanvasTools::documentSwitchCancels()
{
    CanvasDocument first;
    CanvasDocument second;
    ToolController tools;
    tools.setDocument(&first);
    tools.setActiveTool(QStringLiteral("rectangle"));
    const Snapshot a(first);
    const Snapshot b(second);
    QVERIFY(tools.begin(QStringLiteral("v"), 0, 0));
    tools.update(QStringLiteral("v"), 50, 50);
    tools.setDocument(&second);
    QCOMPARE(tools.lastOutcome(), QStringLiteral("canceled:document-switch"));
    QVERIFY(!tools.end(QStringLiteral("v"), 60, 60));
    QVERIFY(a.same(first));
    QVERIFY(b.same(second));
}

void TstCanvasTools::providerDestroyedCancels()
{
    auto *doc = new CanvasDocument;
    ToolController tools;
    tools.setDocument(doc);
    square(*doc, QRectF(0, 0, 10, 10));
    QVERIFY(tools.begin(QStringLiteral("v"), 5, 5)); // select + move
    tools.update(QStringLiteral("v"), 50, 50);
    delete doc;
    QCOMPARE(tools.lastOutcome(), QStringLiteral("canceled:provider-destroyed"));
    QVERIFY(!tools.gestureActive());
    QVERIFY(tools.selection().isEmpty());
    QVERIFY(!tools.document());
    // Edit tools refuse to start without a provider; navigation still works.
    QVERIFY(!tools.begin(QStringLiteral("v"), 0, 0));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("rejected:unavailable"));
    tools.setActiveTool(QStringLiteral("pan"));
    QVERIFY(tools.begin(QStringLiteral("v"), 0, 0));
    QVERIFY(tools.end(QStringLiteral("v"), 5, 5));
}

void TstCanvasTools::externalEditCancels()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    const CanvasObjectId id = square(doc, QRectF(0, 0, 10, 10));
    QVERIFY(tools.begin(QStringLiteral("v"), 5, 5));
    tools.update(QStringLiteral("v"), 25, 5);
    QVERIFY(doc.setObjectVisible(id, false)); // e.g. an inspector or a script
    QCOMPARE(tools.lastOutcome(), QStringLiteral("canceled:document-changed"));
    const QRectF bounds = doc.objectInfo(id).value("bounds").toRectF();
    QCOMPARE(bounds, QRectF(0, 0, 10, 10)); // the move never applied
}

void TstCanvasTools::toolSwitchAndTemporaryTools()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    tools.setActiveTool(QStringLiteral("rectangle"));
    QVERIFY(tools.begin(QStringLiteral("v"), 0, 0));
    QVERIFY(tools.pushTemporaryTool(QStringLiteral("pan")));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("canceled:tool-switch"));
    QCOMPARE(tools.effectiveTool(), QStringLiteral("pan"));
    QCOMPARE(tools.activeTool(), QStringLiteral("rectangle"));
    QVERIFY(tools.popTemporaryTool());
    QCOMPARE(tools.effectiveTool(), QStringLiteral("rectangle"));
    QVERIFY(!tools.popTemporaryTool());
    QVERIFY(!tools.pushTemporaryTool(QStringLiteral("no-such-tool")));
    QVERIFY(tools.begin(QStringLiteral("v"), 0, 0));
    tools.setActiveTool(QStringLiteral("ellipse"));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("canceled:tool-switch"));
    QCOMPARE(doc.objectCount(), 0);
    const QVariantList list = tools.tools();
    QCOMPARE(list.size(), 5);
    QCOMPARE(list.first().toMap().value("iconName").toString(), QStringLiteral("mouse-pointer-2"));
}

void TstCanvasTools::secondViewIsRejected()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    tools.setActiveTool(QStringLiteral("rectangle"));
    QVERIFY(tools.begin(QStringLiteral("left"), 0, 0));
    QVERIFY(!tools.begin(QStringLiteral("right"), 5, 5));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("rejected:busy"));
    QVERIFY(!tools.update(QStringLiteral("right"), 500, 500));
    QVERIFY(!tools.end(QStringLiteral("right"), 500, 500));
    QCOMPARE(tools.gestureOwner(), QStringLiteral("left"));
    QVERIFY(tools.end(QStringLiteral("left"), 40, 40));
    QCOMPARE(doc.objectInfo(doc.objectIds().first()).value("bounds").toRectF().adjusted(1, 1, -1, -1),
             QRectF(0, 0, 40, 40));
    tools.viewDetached(QStringLiteral("left")); // no gesture: harmless
}

void TstCanvasTools::selectMoveMarquee()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    const CanvasObjectId a = square(doc, QRectF(0, 0, 10, 10));
    const CanvasObjectId b = square(doc, QRectF(100, 0, 10, 10));
    // Click selects.
    QVERIFY(tools.begin(QStringLiteral("v"), 5, 5));
    QVERIFY(tools.end(QStringLiteral("v"), 5, 5));
    QCOMPARE(tools.selection(), QVector<CanvasObjectId>{a});
    // Shift-click adds, then dragging moves both in ONE edit.
    QVERIFY(tools.begin(QStringLiteral("v"), 105, 5, ToolController::Mouse, Qt::ShiftModifier));
    tools.update(QStringLiteral("v"), 115, 25);
    QCOMPARE(tools.previewMap().value("kind").toString(), QStringLiteral("move"));
    const quint64 revision = doc.revision();
    QVERIFY(tools.end(QStringLiteral("v"), 125, 45));
    QCOMPARE(doc.revision(), revision + 1);
    QCOMPARE(doc.objectInfo(a).value("bounds").toRectF(), QRectF(20, 40, 10, 10));
    QCOMPARE(doc.objectInfo(b).value("bounds").toRectF(), QRectF(120, 40, 10, 10));
    QVERIFY(doc.undo());
    QCOMPARE(doc.objectInfo(a).value("bounds").toRectF(), QRectF(0, 0, 10, 10));
    QCOMPARE(doc.objectInfo(b).value("bounds").toRectF(), QRectF(100, 0, 10, 10));
    // Marquee on empty space replaces the selection.
    QVERIFY(tools.begin(QStringLiteral("v"), 90, -10));
    tools.update(QStringLiteral("v"), 130, 30);
    QCOMPARE(tools.previewMap().value("kind").toString(), QStringLiteral("marquee"));
    QVERIFY(tools.end(QStringLiteral("v"), 130, 30));
    QCOMPARE(tools.selection(), QVector<CanvasObjectId>{b});
    // A canceled click restores the previous selection.
    QVERIFY(tools.begin(QStringLiteral("v"), 5, 5));
    QCOMPARE(tools.selection(), QVector<CanvasObjectId>{a});
    tools.escape();
    QCOMPARE(tools.selection(), QVector<CanvasObjectId>{b});
}

void TstCanvasTools::lockedObjectsAreNotMoved()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    const CanvasObjectId below = square(doc, QRectF(0, 0, 10, 10));
    square(doc, QRectF(0, 0, 10, 10), true); // locked, on top
    QVERIFY(tools.begin(QStringLiteral("v"), 5, 5));
    QCOMPARE(tools.selection(), QVector<CanvasObjectId>{below}); // picks through the locked one
    QVERIFY(tools.end(QStringLiteral("v"), 15, 5));
    QCOMPARE(doc.objectInfo(below).value("bounds").toRectF(), QRectF(10, 0, 10, 10));
}

void TstCanvasTools::penStrokeAndEraser()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    tools.setActiveTool(QStringLiteral("pen"));
    QVERIFY(tools.begin(QStringLiteral("v"), 0, 0, ToolController::Pen, 0, 0.5));
    for (int i = 1; i <= 10; ++i) {
        tools.update(QStringLiteral("v"), i * 10.0, 0, 0.5);
    }
    QVERIFY(tools.end(QStringLiteral("v"), 100, 50));
    QCOMPARE(doc.objectCount(), 1);
    const CanvasObject *stroke = doc.object(doc.objectIds().first());
    QCOMPARE(stroke->shape, CanvasShape::Polyline);
    QCOMPARE(stroke->points.size(), 12);
    QVERIFY(stroke->strokeWidth < 2.0); // pressure 0.5 thins the default width
    // Eraser: everything crossed goes in one edit; locked objects survive.
    const CanvasObjectId box = square(doc, QRectF(200, 0, 10, 10));
    const CanvasObjectId locked = square(doc, QRectF(300, 0, 10, 10), true);
    QVERIFY(tools.begin(QStringLiteral("v"), 50, 0, ToolController::Eraser));
    tools.update(QStringLiteral("v"), 205, 5);
    tools.update(QStringLiteral("v"), 305, 5);
    QCOMPARE(tools.previewMap().value("ids").toList().size(), 2);
    const quint64 revision = doc.revision();
    QVERIFY(tools.end(QStringLiteral("v"), 305, 5));
    QCOMPARE(doc.revision(), revision + 1);
    QVERIFY(!doc.object(box));
    QVERIFY(doc.object(locked));
    QCOMPARE(doc.objectCount(), 1);
    QCOMPARE(doc.undoText(), QStringLiteral("Erase"));
}

void TstCanvasTools::lockedLayerDeclines()
{
    CanvasDocument doc;
    ToolController tools;
    tools.setDocument(&doc);
    tools.setActiveTool(QStringLiteral("rectangle"));
    QVERIFY(doc.setLayerLocked(doc.activeLayer(), true));
    QVERIFY(!tools.begin(QStringLiteral("v"), 0, 0));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("declined:layer-locked"));
    QVERIFY(doc.setLayerLocked(doc.activeLayer(), false));
    QVERIFY(doc.setLayerVisible(doc.activeLayer(), false));
    QVERIFY(!tools.begin(QStringLiteral("v"), 0, 0));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("declined:layer-hidden"));
    QCOMPARE(tools.state(), QStringLiteral("idle"));
}

void TstCanvasTools::controllerDestroyedMidGesture()
{
    CanvasDocument doc;
    const Snapshot before(doc);
    {
        ToolController tools;
        tools.setDocument(&doc);
        tools.setActiveTool(QStringLiteral("pen"));
        QVERIFY(tools.begin(QStringLiteral("v"), 0, 0));
        tools.update(QStringLiteral("v"), 30, 30);
    }
    QVERIFY(before.same(doc));
}

QTEST_GUILESS_MAIN(TstCanvasTools)
#include "tst_canvas_tools.moc"
