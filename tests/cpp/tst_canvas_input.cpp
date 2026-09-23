// SPDX-License-Identifier: LGPL-3.0-or-later
// CanvasView navigation input: wheel, trackpad, touch pinch/rotate, native
// gestures. Every path must move only the view pose.
#include "canvas_surface.h"

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QTest>
#include <QWheelEvent>
#include <memory>

using namespace QindaTK;

namespace {

bool near(QPointF a, QPointF b, qreal eps = 1e-6)
{
    return std::abs(a.x() - b.x()) <= eps && std::abs(a.y() - b.y()) <= eps;
}

struct Fixture {
    CanvasDocument doc;
    ToolController tools;
    QQuickWindow window;
    CanvasSurface *view = nullptr;
    CanvasObjectId box = 0;
    QRectF bounds;

    Fixture()
    {
        box = doc.addRect(100, 100, 50, 50, QColor(Qt::red));
        bounds = doc.objectInfo(box).value("bounds").toRectF();
        tools.setDocument(&doc);
        window.resize(400, 300);
        view = new CanvasSurface(window.contentItem());
        view->setSize(QSizeF(400, 300));
        view->setDocument(&doc);
        view->setToolController(&tools);
        view->setViewId(QStringLiteral("input"));
        view->fitRect(QRectF(0, 0, 300, 300));
    }
    bool documentUntouched() const
    {
        return doc.objectInfo(box).value("bounds").toRectF() == bounds && doc.objectCount() == 1;
    }
    void wheel(QPointF at, QPoint angle, QPoint pixels, Qt::KeyboardModifiers mods)
    {
        QWheelEvent event(at, window.mapToGlobal(at), pixels, angle, Qt::NoButton, mods,
                          pixels.isNull() ? Qt::NoScrollPhase : Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(&window, &event);
    }
};

} // namespace

class TstCanvasInput : public QObject {
    Q_OBJECT
private slots:
    void wheelZoomIsCursorAnchored();
    void trackpadAndWheelPan();
    void altWheelRotates();
    void touchPinchRotateAboutCentroid();
    void secondFingerCancelsToolGesture();
    void nativeGestures();
    void nonInteractiveIgnoresInput();
    void secondaryButtonReleaseKeepsPrimaryGesture();
};

void TstCanvasInput::wheelZoomIsCursorAnchored()
{
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    const QPointF anchor(73, 211);
    const QPointF under = f.view->mapToDocument(anchor);
    const qreal zoom = f.view->zoom();
    f.wheel(anchor, QPoint(0, 240), {}, Qt::ControlModifier); // two notches
    QVERIFY(std::abs(f.view->zoom() - zoom * 1.25 * 1.25) < 1e-9);
    QVERIFY(near(f.view->mapToDocument(anchor), under, 1e-9));
    QVERIFY(f.documentUntouched());
}

void TstCanvasInput::trackpadAndWheelPan()
{
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    const QPointF docPoint(120, 120);
    QPointF before = f.view->mapFromDocument(docPoint);
    f.wheel(QPointF(200, 150), QPoint(0, 0), QPoint(12, -7), Qt::NoModifier); // trackpad
    QVERIFY(near(f.view->mapFromDocument(docPoint), before + QPointF(12, -7)));
    before = f.view->mapFromDocument(docPoint);
    f.wheel(QPointF(200, 150), QPoint(0, 120), {}, Qt::ShiftModifier); // horizontal
    QVERIFY(near(f.view->mapFromDocument(docPoint), before + QPointF(f.view->property("keyboardPanStep").toReal(), 0)));
    QVERIFY(f.documentUntouched());
}

void TstCanvasInput::altWheelRotates()
{
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    const QPointF anchor(300, 40);
    const QPointF under = f.view->mapToDocument(anchor);
    // Some platforms move Alt+wheel to the horizontal axis; both are accepted.
    f.wheel(anchor, QPoint(120, 0), {}, Qt::AltModifier);
    QCOMPARE(f.view->viewRotation(), 15.0);
    QVERIFY(near(f.view->mapToDocument(anchor), under, 1e-9));
    QVERIFY(f.documentUntouched());
}

void TstCanvasInput::touchPinchRotateAboutCentroid()
{
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QPointingDevice *touch = QTest::createTouchDevice();
    const qreal zoom = f.view->zoom();
    const QPoint a(150, 150);
    const QPoint b(250, 150);
    QTest::touchEvent(&f.window, touch).press(0, a).press(1, b);
    // Spread to twice the distance and turn the pair 90 degrees about the
    // midpoint (200,150): the midpoint's document point must not move.
    const QPointF centre(200, 150);
    const QPointF under = f.view->mapToDocument(centre);
    QTest::touchEvent(&f.window, touch).move(0, QPoint(200, 50)).move(1, QPoint(200, 250));
    QTest::touchEvent(&f.window, touch).release(0, QPoint(200, 50)).release(1, QPoint(200, 250));
    QVERIFY(std::abs(f.view->zoom() - zoom * 2.0) < 1e-9);
    QCOMPARE(f.view->viewRotation(), 90.0);
    QVERIFY(near(f.view->mapToDocument(centre), under, 1e-6));
    QVERIFY(f.documentUntouched());
    QVERIFY(!f.tools.gestureActive());
}

void TstCanvasInput::secondFingerCancelsToolGesture()
{
    Fixture f;
    f.tools.setActiveTool(QStringLiteral("pen"));
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QPointingDevice *touch = QTest::createTouchDevice();
    const quint64 revision = f.doc.revision();
    QTest::touchEvent(&f.window, touch).press(0, QPoint(40, 40));
    QTest::touchEvent(&f.window, touch).move(0, QPoint(80, 60));
    QTest::touchEvent(&f.window, touch).move(0, QPoint(120, 90));
    QCOMPARE(f.tools.gestureOwner(), QStringLiteral("input"));
    QCOMPARE(f.tools.previewMap().value("kind").toString(), QStringLiteral("polyline"));
    QTest::touchEvent(&f.window, touch).stationary(0).press(1, QPoint(300, 200));
    QCOMPARE(f.tools.lastOutcome(), QStringLiteral("canceled:navigation-override"));
    QTest::touchEvent(&f.window, touch).release(0, QPoint(120, 90)).release(1, QPoint(300, 200));
    QCOMPARE(f.doc.revision(), revision); // the partial stroke never reached the document
    // A plain one-finger stroke afterwards commits normally.
    QTest::touchEvent(&f.window, touch).press(0, QPoint(40, 40));
    QTest::touchEvent(&f.window, touch).move(0, QPoint(90, 70));
    QTest::touchEvent(&f.window, touch).release(0, QPoint(140, 90));
    QCOMPARE(f.tools.lastOutcome(), QStringLiteral("committed"));
    QCOMPARE(f.doc.revision(), revision + 1);
}

void TstCanvasInput::nativeGestures()
{
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    // AGENT-NOTE: delivered straight to the item: this checks the view's
    // gesture semantics, not the platform plugin that synthesizes them.
    const QPointingDevice *pad = QPointingDevice::primaryPointingDevice();
    const QPointF at(90, 70);
    const QPointF under = f.view->mapToDocument(at);
    const qreal zoom = f.view->zoom();
    QNativeGestureEvent zoomEvent(Qt::ZoomNativeGesture, pad, 2, at, at, at, 0.5, QPointF());
    QCoreApplication::sendEvent(f.view, &zoomEvent);
    QVERIFY(std::abs(f.view->zoom() - zoom * 1.5) < 1e-9);
    QVERIFY(near(f.view->mapToDocument(at), under, 1e-9));
    QNativeGestureEvent rotate(Qt::RotateNativeGesture, pad, 2, at, at, at, 30, QPointF());
    QCoreApplication::sendEvent(f.view, &rotate);
    QCOMPARE(f.view->viewRotation(), -30.0); // platform positive = counter-clockwise
    QVERIFY(near(f.view->mapToDocument(at), under, 1e-9));
    const QPointF docPoint(120, 120);
    const QPointF before = f.view->mapFromDocument(docPoint);
    QNativeGestureEvent pan(Qt::PanNativeGesture, pad, 2, at, at, at, 0, QPointF(5, 9));
    QCoreApplication::sendEvent(f.view, &pan);
    QVERIFY(near(f.view->mapFromDocument(docPoint), before + QPointF(5, 9)));
    QVERIFY(f.documentUntouched());
}

void TstCanvasInput::nonInteractiveIgnoresInput()
{
    Fixture f;
    f.view->setProperty("interactive", false);
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    const CanvasViewPose pose = f.view->pose();
    f.wheel(QPointF(100, 100), QPoint(0, 120), {}, Qt::ControlModifier);
    f.wheel(QPointF(100, 100), QPoint(0, 0), QPoint(30, 30), Qt::NoModifier);
    QTest::mousePress(&f.window, Qt::LeftButton, {}, QPoint(120, 120));
    QTest::mouseRelease(&f.window, Qt::LeftButton, {}, QPoint(160, 160));
    QCOMPARE(f.view->pose(), pose);
    QVERIFY(!f.tools.gestureActive());
    QVERIFY(f.documentUntouched());
}

void TstCanvasInput::secondaryButtonReleaseKeepsPrimaryGesture()
{
    Fixture f;
    f.tools.setActiveTool(QStringLiteral("rectangle"));
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    const quint64 revision = f.doc.revision();
    auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons down) {
        QMouseEvent event(type, at, at, f.window.mapToGlobal(at.toPoint()),
                          button, down, Qt::NoModifier);
        QCoreApplication::sendEvent(f.view, &event);
    };
    send(QEvent::MouseButtonPress, {40, 40}, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, {120, 100}, Qt::NoButton, Qt::LeftButton);
    QVERIFY(f.tools.gestureActive());
    send(QEvent::MouseButtonPress, {120, 100}, Qt::RightButton,
         Qt::LeftButton | Qt::RightButton);
    send(QEvent::MouseButtonRelease, {120, 100}, Qt::RightButton, Qt::LeftButton);
    QVERIFY(f.tools.gestureActive());
    QCOMPARE(f.doc.revision(), revision);
    send(QEvent::MouseMove, {150, 130}, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, {150, 130}, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(f.tools.lastOutcome(), QStringLiteral("committed"));
    QCOMPARE(f.doc.revision(), revision + 1);
    QCOMPARE(f.doc.objectCount(), 2);

    // A middle-button pan must likewise survive an unrelated release.
    const CanvasViewPose before = f.view->pose();
    send(QEvent::MouseButtonPress, {100, 100}, Qt::MiddleButton, Qt::MiddleButton);
    send(QEvent::MouseButtonPress, {100, 100}, Qt::RightButton,
         Qt::MiddleButton | Qt::RightButton);
    send(QEvent::MouseButtonRelease, {100, 100}, Qt::RightButton, Qt::MiddleButton);
    send(QEvent::MouseMove, {125, 110}, Qt::NoButton, Qt::MiddleButton);
    send(QEvent::MouseButtonRelease, {125, 110}, Qt::MiddleButton, Qt::NoButton);
    QVERIFY(f.view->pose() != before);
    QCOMPARE(f.doc.revision(), revision + 1);
}

QTEST_MAIN(TstCanvasInput)
#include "tst_canvas_input.moc"
