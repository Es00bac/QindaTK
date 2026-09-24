// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/tiled_raster_document.h"

#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
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

        const QImage expectedComposite = document.composite();
        const QVariantMap expectedPreset = document.brushPreset();
        const QString statePath = directory.filePath(QStringLiteral("raster-document.qtraster.json"));
        QVERIFY(document.saveToFile(statePath));
        TiledRasterDocument restored;
        QVERIFY(restored.loadFromFile(statePath));
        QCOMPARE(restored.width(), 520);
        QCOMPARE(restored.height(), 120);
        QCOMPARE(restored.layerIds(), QStringList{QStringLiteral("paint-main")});
        QVERIFY(restored.layerInfo(QStringLiteral("paint-main")).value(QStringLiteral("hasMask")).toBool());
        QCOMPARE(restored.brushPreset(), expectedPreset);
        QVERIFY(!restored.canUndo()); // load starts a new clean history baseline
        QVERIFY(!restored.canRedo());
        QVERIFY(restored.composite() == expectedComposite);

        QFile stateFile(statePath);
        QVERIFY(stateFile.open(QIODevice::ReadOnly));
        QJsonObject invalidState = QJsonDocument::fromJson(stateFile.readAll()).object();
        stateFile.close();
        invalidState.insert(QStringLiteral("version"), 999);
        const QString invalidPath = directory.filePath(QStringLiteral("unsupported-version.json"));
        QFile invalidFile(invalidPath);
        QVERIFY(invalidFile.open(QIODevice::WriteOnly));
        QCOMPARE(invalidFile.write(QJsonDocument(invalidState).toJson()), qint64(QJsonDocument(invalidState).toJson().size()));
        invalidFile.close();
        QVERIFY(!restored.loadFromFile(invalidPath));
        QVERIFY(restored.composite() == expectedComposite); // invalid load is all-or-nothing

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
