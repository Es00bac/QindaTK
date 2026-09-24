// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/tiled_raster_document.h"

#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class TiledRasterDocumentTest : public QObject {
    Q_OBJECT
private slots:
    void strokeCrossesTileSeamMaskCompositesAndUndoes()
    {
        TiledRasterDocument document;
        QVERIFY(document.resizeCanvas(520, 120));
        QVERIFY(document.createLayer(QStringLiteral("paint-main"), QStringLiteral("Main paint")));
        QVERIFY(document.addMask(QStringLiteral("paint-main")));
        QVariantMap brush{{QStringLiteral("diameter"), 24}, {QStringLiteral("hardness"), 0.9},
                          {QStringLiteral("spacing"), 0.18}, {QStringLiteral("opacity"), 1},
                          {QStringLiteral("pressureSize"), 0.4}, {QStringLiteral("pressureOpacity"), 0.25}};
        QVERIFY(document.beginStroke(QStringLiteral("paint-main"), false, QColor(40, 210, 190), brush));
        QVERIFY(document.sampleStroke(238, 60, 0.55, 0, 0));
        QVERIFY(document.sampleStroke(278, 60, 1, 38, -22));
        QVERIFY(document.endStroke());
        QVERIFY(document.canUndo());
        QImage composite = document.composite();
        QVERIFY(composite.pixelColor(255, 60).alpha() > 0);
        QVERIFY(composite.pixelColor(256, 60).alpha() > 0);

        brush.insert(QStringLiteral("diameter"), 12);
        brush.insert(QStringLiteral("hardness"), 1.0);
        brush.insert(QStringLiteral("pressureSize"), 0.0);
        brush.insert(QStringLiteral("pressureOpacity"), 0.0);
        QVERIFY(document.beginStroke(QStringLiteral("paint-main"), true, Qt::black, brush));
        QVERIFY(document.sampleStroke(256, 60));
        QVERIFY(document.endStroke());
        composite = document.composite();
        QCOMPARE(composite.pixelColor(256, 60).alpha(), 0);
        QVERIFY(composite.pixelColor(244, 60).alpha() > 0);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString pngPath = directory.filePath(QStringLiteral("masked-stroke.png"));
        QVERIFY(document.exportPng(pngPath));
        QImageReader reader(pngPath);
        const QImage reopened = reader.read();
        QVERIFY2(!reopened.isNull(), qPrintable(reader.errorString()));
        QCOMPARE(reopened.size(), QSize(520, 120));
        QCOMPARE(reopened.pixelColor(256, 60).alpha(), 0);
        QVERIFY(reopened.pixelColor(244, 60).alpha() > 0);

        QVERIFY(document.undo()); // mask stroke
        QVERIFY(document.composite().pixelColor(256, 60).alpha() > 0);
        QVERIFY(document.undo()); // paint stroke
        QCOMPARE(document.composite().pixelColor(256, 60).alpha(), 0);
        QVERIFY(document.canRedo());
        QVERIFY(document.redo());
        QVERIFY(document.composite().pixelColor(256, 60).alpha() > 0);
    }
};

QTEST_MAIN(TiledRasterDocumentTest)
#include "tst_tiled_raster_document.moc"
