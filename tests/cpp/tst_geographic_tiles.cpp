// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/geographic_map_canvas.h"
#include "../../src/core/geographic_tile_source.h"
#include <QFile>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;
class TestGeographicTiles : public QObject {
    Q_OBJECT
private slots:
    void localTilesAndStaleRequests()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage red(256,256,QImage::Format_ARGB32_Premultiplied);
        red.fill(Qt::red);
        QImage blue(256,256,QImage::Format_ARGB32_Premultiplied);
        blue.fill(Qt::blue);
        QVERIFY(red.save(dir.filePath("red.png")));
        QVERIFY(blue.save(dir.filePath("blue.png")));
        QFile manifest(dir.filePath("tiles.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(R"({"tileSize":256,"minZoom":0,"maxZoom":1,"attribution":"Fixture tiles",
          "tiles":[{"z":0,"x":0,"y":0,"file":"red.png"},
                   {"z":1,"x":0,"y":0,"file":"red.png"},
                   {"z":1,"x":1,"y":0,"file":"blue.png"}]})");
        manifest.close();
        GeographicTileSource source;
        QVERIFY(source.loadManifest(manifest.fileName()));
        QCOMPARE(source.attribution(), QStringLiteral("Fixture tiles"));
        source.setCacheLimitBytes(1024 * 1024);
        source.setVisibleTiles({GeographicTileSource::key(1,0,0)});
        source.setVisibleTiles({GeographicTileSource::key(1,1,0)});
        QTRY_COMPARE(source.tileState(GeographicTileSource::key(1,1,0)), 2);
        QCOMPARE(source.tileState(GeographicTileSource::key(1,0,0)), 1);
        QCOMPARE(source.cachedTileCount(), 1);
        QVERIFY(source.cacheBytes() <= source.cacheLimitBytes());
        GeographicMapCanvas canvas;
        canvas.setWidth(256); canvas.setHeight(256);
        canvas.setTileSource(&source);
        canvas.setZoomLevel(0);
        QTRY_COMPARE(canvas.tileStatus(), QStringLiteral("1/1 ready, 0 missing, 0 failed"));
        QImage composited(256,256,QImage::Format_ARGB32_Premultiplied);
        composited.fill(Qt::transparent);
        QPainter painter(&composited);
        canvas.paint(&painter);
        painter.end();
        QCOMPARE(composited.pixelColor(100,100), QColor(Qt::red));
        canvas.setZoomLevel(1);
        QVERIFY(canvas.tileStatus().contains(QStringLiteral("missing")));
        QVERIFY(!source.loadManifest("https://example.org/tiles.json"));
        QCOMPARE(source.attribution(), QStringLiteral("Fixture tiles"));
    }
};
QTEST_MAIN(TestGeographicTiles)
#include "tst_geographic_tiles.moc"
