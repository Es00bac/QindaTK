// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <QQuickWindow>
#include <QScreen>
#include <QQmlComponent>
#include <QQmlEngine>
#include "screen_work_area.h"
#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>

class TestScreen : public QPlatformScreen {
public:
    QRect bounds;
    QRect workArea;
    QList<QPlatformScreen *> siblings;
    qreal dpr = 1;
    qreal devicePixelRatio() const override { return dpr; }
    QRect geometry() const override { return bounds; }
    QRect availableGeometry() const override { return workArea; }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }
    QList<QPlatformScreen *> virtualSiblings() const override { return siblings; }
};

class ScreenWorkAreaTest : public QObject {
    Q_OBJECT
private slots:
    void differentScreensAndReservedSpace() {
        auto *left = new TestScreen;
        auto *right = new TestScreen;
        left->bounds = QRect(-1200, 0, 1200, 900);
        left->workArea = QRect(-1200, 30, 1200, 820);
        right->dpr = 2;
        right->bounds = QRect(0, 0, 1600, 1000);
        right->workArea = QRect(0, 0, 1600, 940);
        left->siblings = right->siblings = {left, right};
        QWindowSystemInterface::handleScreenAdded(left);
        QWindowSystemInterface::handleScreenAdded(right);
        {
            QQuickWindow window;
            window.setScreen(left->screen());
            QQuickItem anchor(window.contentItem());
            QindaTK::ScreenWorkArea area;
            area.setAnchor(&anchor);
            area.setAnchorPoint(anchor.mapFromGlobal(QPointF(-600, 300)));
            QCOMPARE(area.availableGeometry(), left->workArea);
            // Popup anchor crosses monitors while owning window stays on the left.
            area.setAnchorPoint(anchor.mapFromGlobal(QPointF(600, 300)));
            QCOMPARE(area.availableGeometry(), right->workArea);
            right->workArea = QRect(0, 50, 1600, 850);
            QWindowSystemInterface::handleScreenGeometryChange(right->screen(), right->bounds, right->workArea);
            QTRY_COMPARE(area.availableGeometry(), right->workArea);
            // No native-pixel conversion: the logical work-area height remains 850.
            QCOMPARE(area.availableGeometry().height(), 850);
            area.setAnchorPoint(anchor.mapFromGlobal(QPointF(-600, 300)));
            QCOMPARE(area.availableGeometry(), left->workArea);
            anchor.setX(anchor.x() + 1200);
            QCOMPARE(area.availableGeometry(), right->workArea);
        }
        QWindowSystemInterface::handleScreenRemoved(right);
        QWindowSystemInterface::handleScreenRemoved(left);
    }
    void geometryAndLifetime() {
        QindaTK::ScreenWorkArea area;
        QCOMPARE(area.availableGeometry(), QRect());
        QQuickWindow window;
        auto *ancestor = new QQuickItem(window.contentItem());
        auto *item = new QQuickItem(ancestor);
        area.setAnchor(item);
        QCOMPARE(area.availableGeometry(), window.screen()->availableGeometry());
        // A point outside every screen must retain the window-screen fallback.
        area.setAnchorPoint(QPointF(-100000, -100000));
        QCOMPARE(area.availableGeometry(), window.screen()->availableGeometry());
        item->setParentItem(nullptr);
        QCOMPARE(area.availableGeometry(), QRect());
        item->setParentItem(ancestor);
        QCOMPARE(area.availableGeometry(), window.screen()->availableGeometry());
        delete item;
        QCOMPARE(area.anchor(), nullptr);
        QCOMPARE(area.availableGeometry(), QRect());
    }
    void menuUsesWorkArea() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QindaTK as Tk
Item {
    width: 320; height: 200
    Tk.Menu {
        objectName: "menu"
        contentHeight: 100000
    }
})", QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *root = qobject_cast<QQuickItem *>(object.data());
        QQuickWindow window;
        root->setParentItem(window.contentItem());
        auto *menu = root->findChild<QObject *>("menu");
        QVERIFY(menu);
        const qreal margin = menu->property("margins").toReal();
        QCOMPARE(menu->property("implicitHeight").toReal(),
                 window.screen()->availableGeometry().height() - 2 * margin);
        root->setParentItem(nullptr);
    }
};
QTEST_MAIN(ScreenWorkAreaTest)
#include "tst_screen_work_area.moc"
