// SPDX-License-Identifier: LGPL-3.0-or-later
#include "stylus_handler.h"

#include <QPointingDevice>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QtGui/private/qpointingdevice_p.h>
#include <QtTest>
#include <qpa/qwindowsysteminterface.h>

using namespace QindaTK;

// The handler is verified the way a tablet driver reaches Qt: through
// QWindowSystemInterface, so the delivery agent, mouse synthesis and grab
// rules are all in play (a QCoreApplication::sendEvent never reaches a
// handler).
class TstStylusHandler final : public QObject {
    Q_OBJECT

    QPointingDevice *m_stylus = nullptr;
    QPointingDevice *m_eraser = nullptr;
    QPointingDevice *m_finger = nullptr;

    struct Scene {
        QQuickView view;
        QQuickItem *root = nullptr;
        StylusHandler *handler = nullptr;
    };

    std::unique_ptr<Scene> makeScene(const QString &extra = {})
    {
        auto scene = std::make_unique<Scene>();
        scene->view.engine()->addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        scene->view.setResizeMode(QQuickView::SizeRootObjectToView);
        scene->view.resize(400, 300);
        QQmlComponent component(scene->view.engine());
        component.setData(("import QtQuick\nimport QindaTK as Tk\n"
                           "Rectangle { objectName: \"canvas\"; width: 400; height: 300\n"
                           "  Tk.StylusHandler { objectName: \"stylus\" " + extra + " } }")
                              .toUtf8(),
                          QUrl(QStringLiteral("inline.qml")));
        if (component.isError()) {
            for (const QQmlError &error : component.errors()) {
                qWarning("%s", qPrintable(error.toString()));
            }
        }
        scene->root = qobject_cast<QQuickItem *>(component.create());
        if (scene->root != nullptr) {
            scene->root->setParentItem(scene->view.contentItem());
            scene->handler = scene->root->findChild<StylusHandler *>(QStringLiteral("stylus"));
        }
        scene->view.show();
        return scene;
    }

    void tabletSequence(QWindow *window, const QPointingDevice *device, const QPointF &from,
                        const QPointF &to, qreal pressure, qreal tilt)
    {
        const auto send = [&](const QPointF &pt, Qt::MouseButtons buttons, qreal p) {
            QWindowSystemInterface::handleTabletEvent(window, device, pt,
                                                      QPointF(window->mapToGlobal(pt.toPoint())),
                                                      buttons, p, tilt, 0.0, 0.0, 0.0, 0,
                                                      Qt::NoModifier);
            QWindowSystemInterface::flushWindowSystemEvents();
            QCoreApplication::processEvents();
        };
        send(from, Qt::LeftButton, pressure);
        send(to, Qt::LeftButton, pressure * 0.75);
        send(to, Qt::NoButton, 0.0);
    }

private slots:
    void initTestCase()
    {
        m_stylus = new QPointingDevice(QStringLiteral("test-stylus"), 1,
                                       QInputDevice::DeviceType::Stylus,
                                       QPointingDevice::PointerType::Pen,
                                       QInputDevice::Capability::Position
                                           | QInputDevice::Capability::Pressure
                                           | QInputDevice::Capability::XTilt
                                           | QInputDevice::Capability::YTilt,
                                       1, 1);
        m_eraser = new QPointingDevice(QStringLiteral("test-eraser"), 2,
                                       QInputDevice::DeviceType::Stylus,
                                       QPointingDevice::PointerType::Eraser,
                                       QInputDevice::Capability::Position
                                           | QInputDevice::Capability::Pressure,
                                       1, 1);
        m_finger = new QPointingDevice(QStringLiteral("test-finger"), 3,
                                       QInputDevice::DeviceType::TouchScreen,
                                       QPointingDevice::PointerType::Finger,
                                       QInputDevice::Capability::Position, 1, 1);
        QPointingDevicePrivate::registerDevice(m_stylus);
        QPointingDevicePrivate::registerDevice(m_eraser);
        QPointingDevicePrivate::registerDevice(m_finger);
    }

    void penGestureCarriesPressureTiltAndType()
    {
        auto scene = makeScene();
        QVERIFY(scene->handler != nullptr);
        QVERIFY(QTest::qWaitForWindowExposed(&scene->view));
        QSignalSpy began(scene->handler, &StylusHandler::began);
        QSignalSpy moved(scene->handler, &StylusHandler::moved);
        QSignalSpy ended(scene->handler, &StylusHandler::ended);

        tabletSequence(&scene->view, m_stylus, QPointF(100, 100), QPointF(130, 110), 0.8, 12.0);

        QCOMPARE(began.count(), 1);
        QCOMPARE(moved.count(), 1);
        QCOMPARE(ended.count(), 1);
        const auto first = began.first().first().value<StylusSample>();
        QCOMPARE(first.position, QPointF(100, 100));
        QCOMPARE(first.pressure, 0.8);
        QCOMPARE(first.tiltX, 12.0);
        QCOMPARE(first.pointerTypeName(), QStringLiteral("pen"));
        QVERIFY(!first.synthesizedFromMouse);
        const auto mid = moved.first().first().value<StylusSample>();
        QCOMPARE(mid.position, QPointF(130, 110));
        QCOMPARE(mid.pressure, 0.6);
        QVERIFY(!scene->handler->active());
    }

    void eraserEndReportsEraser()
    {
        auto scene = makeScene();
        QVERIFY(QTest::qWaitForWindowExposed(&scene->view));
        QSignalSpy began(scene->handler, &StylusHandler::began);
        tabletSequence(&scene->view, m_eraser, QPointF(50, 50), QPointF(60, 60), 0.5, 0.0);
        QCOMPARE(began.count(), 1);
        QCOMPARE(began.first().first().value<StylusSample>().pointerTypeName(),
                 QStringLiteral("eraser"));
    }

    void touchNeverInks()
    {
        auto scene = makeScene();
        QVERIFY(QTest::qWaitForWindowExposed(&scene->view));
        QSignalSpy began(scene->handler, &StylusHandler::began);
        QWindowSystemInterface::TouchPoint finger;
        finger.id = 1;
        finger.area = QRectF(95, 95, 10, 10);
        finger.state = QEventPoint::Pressed;
        QWindowSystemInterface::handleTouchEvent(&scene->view, m_finger, {finger});
        QWindowSystemInterface::flushWindowSystemEvents();
        QCoreApplication::processEvents();
        QCOMPARE(began.count(), 0);
    }

    void mouseFallbackSamplesOnceWithMousePressure()
    {
        auto scene = makeScene(QStringLiteral("; mousePressure: 0.4"));
        QVERIFY(QTest::qWaitForWindowExposed(&scene->view));
        QSignalSpy began(scene->handler, &StylusHandler::began);
        QSignalSpy moved(scene->handler, &StylusHandler::moved);
        QSignalSpy ended(scene->handler, &StylusHandler::ended);
        QTest::mousePress(&scene->view, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QTest::mouseMove(&scene->view, QPoint(40, 30));
        QTest::mouseRelease(&scene->view, Qt::LeftButton, Qt::NoModifier, QPoint(40, 30));
        QCOMPARE(began.count(), 1);
        QVERIFY(moved.count() >= 1);
        QCOMPARE(ended.count(), 1);
        const auto first = began.first().first().value<StylusSample>();
        QCOMPARE(first.pressure, 0.4);
        QVERIFY(first.synthesizedFromMouse);
        QCOMPARE(first.pointerTypeName(), QStringLiteral("mouse"));
    }

    void aPenStrokeIsNotAlsoAMouseStroke()
    {
        // Qt synthesises a mouse event for every tablet event a handler's
        // localised copy could not mark accepted; those must be dropped.
        auto scene = makeScene();
        QVERIFY(QTest::qWaitForWindowExposed(&scene->view));
        QSignalSpy began(scene->handler, &StylusHandler::began);
        tabletSequence(&scene->view, m_stylus, QPointF(100, 100), QPointF(130, 110), 0.8, 0.0);
        QCOMPARE(began.count(), 1);
    }

    void strokeThatStartsOutsideIsIgnored()
    {
        auto scene = makeScene();
        QVERIFY(QTest::qWaitForWindowExposed(&scene->view));
        scene->root->setSize(QSizeF(200, 150));
        QSignalSpy began(scene->handler, &StylusHandler::began);
        tabletSequence(&scene->view, m_stylus, QPointF(300, 250), QPointF(310, 260), 0.8, 0.0);
        QCOMPARE(began.count(), 0);
    }
};

QTEST_MAIN(TstStylusHandler)
#include "tst_stylus_handler.moc"
