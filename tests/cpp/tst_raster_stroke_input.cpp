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
};

QTEST_MAIN(TstRasterStrokeInput)
#include "tst_raster_stroke_input.moc"
