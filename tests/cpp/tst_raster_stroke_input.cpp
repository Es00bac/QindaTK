// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/stylus_handler.h"
#include "../../src/core/tiled_raster_document.h"

#include <QPointingDevice>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QtGui/private/qpointingdevice_p.h>
#include <QtTest>
#include <qpa/qwindowsysteminterface.h>

using namespace QindaTK;

class TstRasterStrokeInput final : public QObject {
    Q_OBJECT
private slots:
    void mouseAndTabletSamplesCommitSingleDocumentEdits()
    {
        QPointingDevice stylus(QStringLiteral("raster-test-stylus"), 91,
                               QInputDevice::DeviceType::Stylus,
                               QPointingDevice::PointerType::Pen,
                               QInputDevice::Capability::Position
                                   | QInputDevice::Capability::Pressure
                                   | QInputDevice::Capability::XTilt
                                   | QInputDevice::Capability::YTilt,
                               1, 1);
        QPointingDevicePrivate::registerDevice(&stylus);

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(400, 300);
        QQmlComponent component(view.engine());
        const QByteArray qml =
            "import QtQuick\nimport QindaTK as Tk\n"
            "Rectangle { objectName: 'hostCanvas'; width: 400; height: 300; color: '#20242a'\n"
            "  Tk.TiledRasterDocument { id: document; objectName: 'rasterDocument';"
            " Component.onCompleted: { resizeCanvas(400, 300); createLayer('ink', 'Ink') } }\n"
            "  Tk.RasterStrokeInput { objectName: 'inputSurface'; anchors.fill: parent;"
            " document: document; layerId: 'ink'; color: '#40c9ca';"
            " brushPreset: ({diameter: 32, hardness: 1, spacing: 0.2, opacity: 1,"
            " pressureSize: 0.8, pressureOpacity: 0.4}) }\n} ";
        component.setData(qml, QUrl(QStringLiteral("inline:raster-test.qml")));
        if (component.isLoading()) QTRY_VERIFY(component.isReady() || component.isError());
        if (component.isError()) {
            for (const QQmlError &error : component.errors()) qWarning("%s", qPrintable(error.toString()));
        }
        QQuickItem *root = qobject_cast<QQuickItem *>(component.create());
        QVERIFY(root);
        root->setParentItem(view.contentItem());
        TiledRasterDocument *document = root->findChild<TiledRasterDocument *>(QStringLiteral("rasterDocument"));
        QVERIFY(document);
        const qulonglong startingRevision = document->revision();
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        const auto tabletSample = [&](const QPointF &point, Qt::MouseButtons buttons, qreal pressure,
                                      qreal xTilt, qreal yTilt) {
            QWindowSystemInterface::handleTabletEvent(&view, &stylus, point,
                QPointF(view.mapToGlobal(point.toPoint())), buttons, pressure,
                xTilt, yTilt, 0.0, 0.0, 0, Qt::NoModifier);
            QWindowSystemInterface::flushWindowSystemEvents();
            QCoreApplication::processEvents();
        };
        tabletSample(QPointF(90, 90), Qt::LeftButton, 0.82, 26, -14);
        tabletSample(QPointF(160, 112), Qt::LeftButton, 0.56, 18, -8);
        tabletSample(QPointF(160, 112), Qt::NoButton, 0.0, 18, -8);
        QCOMPARE(document->revision(), startingRevision + 1);
        QVERIFY(document->composite().pixelColor(90, 90).alpha() > 0);

        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(230, 160));
        QTest::mouseMove(&view, QPoint(280, 174));
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(280, 174));
        QCOMPARE(document->revision(), startingRevision + 2);
        QVERIFY(document->composite().pixelColor(230, 160).alpha() > 0);

        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(320, 230));
        QVERIFY(document->composite().pixelColor(320, 230).alpha() > 0);
        QTest::keyClick(&view, Qt::Key_Escape);
        QCOMPARE(document->revision(), startingRevision + 2);
        QCOMPARE(document->composite().pixelColor(320, 230).alpha(), 0);
        delete root;
    }

    void pointMapperAndPresetSwitchKeepTheStroke()
    {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        view.resize(400, 300);
        QQmlComponent component(view.engine());
        const QByteArray qml =
            "import QtQuick\nimport QindaTK as Tk\n"
            "Item { width: 400; height: 300\n"
            "  Tk.TiledRasterDocument { id: document; objectName: 'rasterDocument';"
            " Component.onCompleted: { resizeCanvas(200, 150); createLayer('ink', 'Ink') } }\n"
            "  Tk.RasterStrokeInput { objectName: 'inputSurface'; anchors.fill: parent;"
            " document: document; layerId: 'ink'; color: '#ff0000';"
            " pointMapper: function(p) { return Qt.point(p.x / 2 + 10, p.y / 2) };"
            " brushPreset: ({diameter: 8, hardness: 1, spacing: 0.2, opacity: 1,"
            " pressureSize: 0, pressureOpacity: 0}) }\n} ";
        component.setData(qml, QUrl(QStringLiteral("inline:raster-mapper-test.qml")));
        if (component.isLoading()) QTRY_VERIFY(component.isReady() || component.isError());
        QQuickItem *root = qobject_cast<QQuickItem *>(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setParentItem(view.contentItem());
        auto *document = root->findChild<TiledRasterDocument *>(QStringLiteral("rasterDocument"));
        auto *input = root->findChild<QQuickItem *>(QStringLiteral("inputSurface"));
        QVERIFY(document && input);
        const qulonglong startingRevision = document->revision();
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
        QTest::mouseMove(&view, QPoint(140, 100));
        // A preset switch mid-gesture neither cancels nor alters the stroke.
        QVERIFY(input->setProperty("brushPreset", QVariantMap{{QStringLiteral("diameter"), 30.0}}));
        QVERIFY(input->property("strokeActive").toBool());
        QTest::mouseMove(&view, QPoint(180, 100));
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(180, 100));
        QCOMPARE(document->revision(), startingRevision + 1);
        const QImage image = document->composite();
        QCOMPARE(image.pixelColor(60, 50), QColor(Qt::red));  // view (100, 100) mapped
        QCOMPARE(image.pixelColor(100, 50), QColor(Qt::red)); // view (180, 100) mapped
        QCOMPARE(image.pixelColor(100, 58).alpha(), 0);       // still 8 px wide, not 30
        delete root;
    }
};

QTEST_MAIN(TstRasterStrokeInput)
#include "tst_raster_stroke_input.moc"
