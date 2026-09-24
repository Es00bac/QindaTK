// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/tiled_raster_document.h"

#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
QVariantMap hardBrush(qreal diameter, qreal pressureSize = 0)
{
    return {{QStringLiteral("diameter"), diameter}, {QStringLiteral("hardness"), 1.0},
            {QStringLiteral("spacing"), 0.1}, {QStringLiteral("opacity"), 1.0},
            {QStringLiteral("pressureSize"), pressureSize}, {QStringLiteral("pressureOpacity"), 0.0}};
}

bool dab(TiledRasterDocument &document, bool mask, const QColor &color, qreal diameter, qreal x, qreal y)
{
    return document.beginStroke(QStringLiteral("paint"), mask, color, hardBrush(diameter))
        && document.sampleStroke(x, y) && document.endStroke();
}

int coverage(const QImage &image, int x) // opaque rows in one column
{
    int rows = 0;
    for (int y = 0; y < image.height(); ++y) rows += image.pixelColor(x, y).alpha() >= 128;
    return rows;
}

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

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
        QCOMPARE(composite.colorSpace(), QColorSpace(QColorSpace::SRgb));
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
        QCOMPARE(reopened.colorSpace(), QColorSpace(QColorSpace::SRgb));
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

    void tileAndUndoWorkAreBounded()
    {
        // A stroke touching a 65th tile cancels atomically: no pixels, no step.
        TiledRasterDocument wide;
        QVERIFY(wide.resizeCanvas(5120, 1024));
        QVERIFY(wide.createLayer(QStringLiteral("paint"), QStringLiteral("Paint")));
        QVariantMap huge = hardBrush(512);
        huge.insert(QStringLiteral("spacing"), 1.0);
        QVERIFY(wide.beginStroke(QStringLiteral("paint"), false, Qt::red, huge));
        QVERIFY(wide.sampleStroke(300, 512));
        QVERIFY(!wide.sampleStroke(5000, 512));
        QVERIFY2(wide.error().contains(QStringLiteral("64-tile")), qPrintable(wide.error()));
        QVERIFY(!wide.canUndo());
        QCOMPARE(wide.composite().pixelColor(300, 512).alpha(), 0);

        // Undo keeps at most 64 MiB: 128 before/after pairs of one 256 KiB tile.
        TiledRasterDocument single;
        QVERIFY(single.resizeCanvas(256, 256));
        QVERIFY(single.createLayer(QStringLiteral("paint"), QStringLiteral("Paint")));
        for (int i = 0; i < 140; ++i)
            QVERIFY(dab(single, false, i % 2 ? QColor(Qt::red) : QColor(Qt::blue), 8, 128, 128));
        int undone = 0;
        while (single.undo()) ++undone;
        QCOMPARE(undone, 128);
    }

    void maskToggleIsReversibleAndHiddenDataIsNeverPainted()
    {
        TiledRasterDocument document;
        QVERIFY(document.resizeCanvas(64, 64));
        QVERIFY(document.createLayer(QStringLiteral("paint"), QStringLiteral("Paint")));
        QVERIFY(document.addMask(QStringLiteral("paint")));
        QVERIFY(dab(document, false, Qt::red, 20, 32, 32));
        QVERIFY(dab(document, true, Qt::black, 8, 32, 32));
        const QImage masked = document.composite();
        QCOMPARE(masked.pixelColor(32, 32).alpha(), 0);

        QVERIFY(document.setMaskEnabled(QStringLiteral("paint"), false));
        QCOMPARE(document.composite().pixelColor(32, 32), QColor(Qt::red)); // coverage kept, only hidden
        QVERIFY(!document.beginStroke(QStringLiteral("paint"), true, Qt::white, hardBrush(8)));
        QVERIFY2(document.error().contains(QStringLiteral("hidden")), qPrintable(document.error()));
        QVERIFY(document.undo()); // the toggle is one undo step
        QVERIFY(document.composite() == masked);
        QVERIFY(document.layerInfo(QStringLiteral("paint")).value(QStringLiteral("maskEnabled")).toBool());
        QVERIFY(document.redo());
        QVERIFY(!document.layerInfo(QStringLiteral("paint")).value(QStringLiteral("maskEnabled")).toBool());
        QVERIFY(document.canUndo());
    }

    void presetsAreCapturedPreviewedAndRestored()
    {
        // CAN-15: a preset switch during a stroke leaves that stroke's pixels unchanged.
        const QVariantMap first = hardBrush(16);
        QVariantMap second = hardBrush(40);
        second.insert(QStringLiteral("hardness"), 0.2);
        TiledRasterDocument switched, steady;
        for (TiledRasterDocument *document : {&switched, &steady}) {
            QVERIFY(document->resizeCanvas(128, 64));
            QVERIFY(document->createLayer(QStringLiteral("paint"), QStringLiteral("Paint")));
            QVERIFY(document->beginStroke(QStringLiteral("paint"), false, Qt::darkCyan, first));
            QVERIFY(document->sampleStroke(20, 32, 0.4));
        }
        QVERIFY(switched.updateBrushPreset(second));
        for (TiledRasterDocument *document : {&switched, &steady}) {
            QVERIFY(document->sampleStroke(108, 32, 1.0));
            QVERIFY(document->endStroke());
        }
        QVERIFY(switched.composite() == steady.composite()); // also: same samples, same pixels
        QCOMPARE(switched.brushPreset().value(QStringLiteral("diameter")).toDouble(), 40.0);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(steady.saveToFile(directory.filePath(QStringLiteral("a.json"))));
        QVERIFY(steady.saveToFile(directory.filePath(QStringLiteral("b.json"))));
        QCOMPARE(fileBytes(directory.filePath(QStringLiteral("a.json"))),
                 fileBytes(directory.filePath(QStringLiteral("b.json"))));

        // CAN-16: the sample stroke follows diameter x (1 - s + s x pressure)
        // and restores after undo, save and reload.
        TiledRasterDocument document;
        QVERIFY(document.createLayer(QStringLiteral("paint"), QStringLiteral("Paint")));
        QVERIFY(document.updateBrushPreset(hardBrush(24)));
        const QImage flat = document.brushPreview(document.brushPreset(), Qt::black, 160, 40);
        QVERIFY(!flat.isNull());
        QVERIFY(std::abs(coverage(flat, 80) - 24) <= 1);   // pressure 0.55 ignored
        QVERIFY(document.updateBrushPreset({{QStringLiteral("pressureSize"), 1.0}}));
        const QImage responsive = document.brushPreview(document.brushPreset(), Qt::black, 160, 40);
        QVERIFY2(std::abs(coverage(responsive, 80) - 13.2) <= 1.5, qPrintable(QString::number(coverage(responsive, 80))));
        QVERIFY(coverage(responsive, 20) < coverage(responsive, 140));
        QVERIFY(document.brushPreviewDataUrl(document.brushPreset(), Qt::black, 160, 40)
                    .startsWith(QStringLiteral("data:image/png;base64,")));
        QVERIFY(document.brushPreview(document.brushPreset(), Qt::black, 4096, 40).isNull());
        QVERIFY(document.undo());
        QCOMPARE(document.brushPreset().value(QStringLiteral("pressureSize")).toDouble(), 0.0);
        QVERIFY(document.brushPreview(document.brushPreset(), Qt::black, 160, 40) == flat);
        QVERIFY(document.redo());
        const QString path = directory.filePath(QStringLiteral("preset.json"));
        QVERIFY(document.saveToFile(path));
        TiledRasterDocument reopened;
        QVERIFY(reopened.loadFromFile(path));
        QVERIFY(reopened.brushPreview(reopened.brushPreset(), Qt::black, 160, 40) == responsive);
    }
};

QTEST_MAIN(TiledRasterDocumentTest)
#include "tst_tiled_raster_document.moc"
