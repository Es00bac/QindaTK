// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/geographic_feature_model.h"
#include "../../src/core/geographic_map_canvas.h"
#include "../../src/core/geographic_tile_source.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace QindaTK;
class TestGeographicMap : public QObject {
    Q_OBJECT
private slots:
    void projection()
    {
        GeographicFeatureModel model;
        auto origin = model.project(0,0);
        QVERIFY(origin.value("valid").toBool());
        QCOMPARE(origin.value("x").toDouble(), 0.5);
        QCOMPARE(origin.value("y").toDouble(), 0.5);
        const auto known = model.project(180, 85.0511287798066);
        QVERIFY(std::abs(known.value("x").toDouble()-1.0) < 1e-12);
        QVERIFY(std::abs(known.value("y").toDouble()) < 1e-12);
        QVERIFY(model.project(0,90).value("latitudeClamped").toBool());
        QVERIFY(!model.project(181,0).value("valid").toBool());
        const auto reverse = model.unproject(1.0027777777777778, 0.5);
        QVERIFY(std::abs(reverse.value("longitude").toDouble()+179.0) < 1e-9);
        GeographicMapCanvas canvas;
        canvas.setWidth(400); canvas.setHeight(300);
        canvas.setCenterLatitude(90);
        QVERIFY(std::abs(canvas.centerLatitude()-GeographicFeatureModel::latitudeLimit) < 1e-8);
        QVERIFY(canvas.screenPosition(0,90).value("latitudeClamped").toBool());
        QVERIFY(canvas.coordinateAt(200,-100).value("latitudeClamped").toBool());
    }
    void localGeoJsonAndInteraction()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath("in.geojson"), output = dir.filePath("out.geojson");
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"type":"FeatureCollection","attribution":"Local survey","features":[
          {"type":"Feature","id":"marker","properties":{"name":"Station A","score":7},"geometry":{"type":"Point","coordinates":[179.5,0]}},
          {"type":"Feature","properties":{"name":"crossing"},"geometry":{"type":"LineString","coordinates":[[179,1],[-179,1]]}}
        ]})");
        file.close();
        GeographicFeatureModel model;
        QVERIFY(model.loadGeoJson(input));
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.attribution(), QStringLiteral("Local survey"));
        QCOMPARE(model.featureById("marker").value("properties").toMap().value("name").toString(), QStringLiteral("Station A"));
        const auto generatedId = model.featureAt(1).value("id").toString();
        QVERIFY(generatedId.startsWith("geo-"));
        QVERIFY(model.saveGeoJson(output));
        GeographicFeatureModel restored;
        QVERIFY(restored.loadGeoJson(output));
        QCOMPARE(restored.featureAt(1).value("id").toString(), generatedId);
        QCOMPARE(restored.featureById("marker").value("properties").toMap().value("score").toInt(), 7);
        GeographicMapCanvas canvas;
        canvas.setWidth(400); canvas.setHeight(300);
        canvas.setModel(&restored);
        canvas.setProperty("pointRadius", 5);
        canvas.setProperty("lineWidth", 2);
        canvas.setProperty("hitRadius", 4);
        canvas.setCenterLongitude(-180);
        canvas.setCenterLatitude(0);
        canvas.setZoomLevel(4);
        const auto marker = canvas.screenPosition(179.5,0);
        QVERIFY(std::abs(marker.value("x").toDouble()-200) < 10);
        QCOMPARE(canvas.featureIdAt(marker.value("x").toDouble(), marker.value("y").toDouble()), QStringLiteral("marker"));
        const QPointF click(marker.value("x").toDouble(), marker.value("y").toDouble());
        QMouseEvent press(QEvent::MouseButtonPress, click, click, click, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, click, click, click, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&canvas, &press);
        QCoreApplication::sendEvent(&canvas, &release);
        QCOMPARE(canvas.selectedFeatureId(), QStringLiteral("marker"));
        const auto crossing = canvas.screenPosition(-179,1);
        QCOMPARE(canvas.featureIdAt(crossing.value("x").toDouble(), crossing.value("y").toDouble()), generatedId);
        QCOMPARE(canvas.featureIdAt(200, crossing.value("y").toDouble()), generatedId);
        const auto before = canvas.coordinateAt(200,150);
        canvas.panPixels(50,0);
        const auto after = canvas.coordinateAt(200,150);
        QVERIFY(std::abs(before.value("longitude").toDouble()-after.value("longitude").toDouble()) > 1);
        QWheelEvent wheel(QPointF(100,100), QPointF(100,100), QPoint(), QPoint(0,480),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&canvas, &wheel);
        QCOMPARE(canvas.zoomLevel(), 5.0);
        QCOMPARE(canvas.tileStatus(), QStringLiteral("unavailable"));
        QVERIFY(!restored.loadGeoJson("https://example.org/features.geojson"));
        QCOMPARE(restored.count(), 2); // failed replacement is atomic
        QFile rejected(input);
        QVERIFY(rejected.open(QIODevice::WriteOnly | QIODevice::Truncate));
        rejected.write(R"({"type":"FeatureCollection","crs":{"type":"name","properties":{"name":"EPSG:3857"}},"features":[]})");
        rejected.close();
        QVERIFY(!restored.loadGeoJson(input));
        QVERIFY(restored.lastError().contains("CRS"));
        QCOMPARE(restored.count(), 2);
    }
    void editSaveExportAndReopen()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString saved = dir.filePath("edited.geojson"), exported = dir.filePath("copy.geojson");
        GeographicFeatureModel model;
        model.setAttribution(QStringLiteral("Field survey"));
        const QString point = model.addFeature("Point", QVariantList{-105.0, 40.0},
                                               {{"name", "station"}});
        QVERIFY(!point.isEmpty());
        QVERIFY(model.movePoint(point, -104.8, 39.8));
        const QVariantList line{QVariantList{-105.0,39.7}, QVariantList{-104.8,39.9}};
        QCOMPARE(model.addFeature("LineString", line, {{"name", "route"}}, "route-1"),
                 QStringLiteral("route-1"));
        QVERIFY(model.translateFeature("route-1", 0.1, -0.1));
        const QVariantList ring{QVariantList{-105.1,39.6}, QVariantList{-104.9,39.6},
                                QVariantList{-104.9,39.8}, QVariantList{-105.1,39.6}};
        QCOMPARE(model.addFeature("Polygon", QVariantList{ring}, {{"name", "area"}}, "area-1"),
                 QStringLiteral("area-1"));
        const auto polygonBefore = model.featureById("area-1").value("geometry").toMap();
        QVERIFY(!model.replaceGeometry("area-1", QVariantList{QVariantList{QVariantList{181.0,0.0}}}));
        QCOMPARE(model.featureById("area-1").value("geometry").toMap(), polygonBefore);
        GeographicMapCanvas canvas;
        canvas.setWidth(500); canvas.setHeight(400);
        canvas.setModel(&model);
        canvas.setProperty("pointRadius", 6);
        canvas.setProperty("hitRadius", 6);
        canvas.setCenterLongitude(-104.8);
        canvas.setCenterLatitude(39.8);
        canvas.setZoomLevel(9);
        canvas.setEditMode(true);
        const auto start = canvas.screenPosition(-104.8,39.8);
        const QPointF a(start.value("x").toDouble(), start.value("y").toDouble());
        const QPointF b = a + QPointF(25, 12);
        QMouseEvent press(QEvent::MouseButtonPress,a,a,a,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove,b,b,b,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,b,b,b,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(&canvas,&press);
        QCoreApplication::sendEvent(&canvas,&move);
        QCoreApplication::sendEvent(&canvas,&release);
        QCOMPARE(canvas.selectedFeatureId(), point);
        const auto moved = model.featureById(point).value("geometry").toMap()
                               .value("coordinates").toList();
        QVERIFY(moved[0].toDouble() > -104.8);
        QCOMPARE(model.featureById(point).value("properties").toMap().value("name").toString(),
                 QStringLiteral("station"));
        QCOMPARE(canvas.selectedFeature().value("id").toString(), point);
        QCOMPARE(model.addFeature("Point", QVariantList{-104.7,39.7}, {}, "temporary"),
                 QStringLiteral("temporary"));
        QVERIFY(canvas.selectFeature("temporary"));
        QVERIFY(model.removeFeature("temporary"));
        QCOMPARE(canvas.selectedFeatureId(), QString());
        QVERIFY(model.saveGeoJson(saved));
        QCOMPARE(model.sourcePath(), saved);
        QVERIFY(model.exportGeoJson(exported));
        QCOMPARE(model.sourcePath(), saved); // export is a copy, not the active document
        GeographicFeatureModel reopened;
        QVERIFY(reopened.loadGeoJson(exported));
        QCOMPARE(reopened.count(), 3);
        QCOMPARE(reopened.attribution(), QStringLiteral("Field survey"));
        QCOMPARE(reopened.featureById(point).value("geometry").toMap().value("coordinates").toList(), moved);
        QCOMPARE(reopened.featureById("route-1").value("properties").toMap().value("name").toString(),
                 QStringLiteral("route"));
        QCOMPARE(reopened.featureById("area-1").value("geometry").toMap(), polygonBefore);
    }
    void antimeridianTilesAndComposedPng()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonArray tiles;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                const QString name = QStringLiteral("%1-%2.png").arg(x).arg(y);
                QImage tile(256, 256, QImage::Format_RGB32);
                tile.fill(x == 0 ? QColor("#0000ff") : QColor("#ff0000"));
                QVERIFY(tile.save(dir.filePath(name)));
                tiles.append(QJsonObject{{"z",1},{"x",x},{"y",y},{"file",name}});
            }
        QFile manifest(dir.filePath("tiles.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(QJsonDocument(QJsonObject{{"tileSize",256},{"minZoom",1},{"maxZoom",1},
                                             {"attribution","Tile author"},{"tiles",tiles}}).toJson());
        manifest.close();
        GeographicTileSource source;
        QVERIFY(source.loadManifest(manifest.fileName()));
        GeographicFeatureModel model;
        model.setAttribution(QStringLiteral("Vector author"));
        QCOMPARE(model.addFeature("Point", QVariantList{-180.0,0.0}, {}, "seam"), QStringLiteral("seam"));
        GeographicMapCanvas canvas;
        canvas.setWidth(400); canvas.setHeight(300);
        canvas.setModel(&model);
        canvas.setTileSource(&source);
        canvas.setProperty("backgroundColor", QColor("#202020"));
        canvas.setProperty("featureColor", QColor("#00ffff"));
        canvas.setProperty("gridColor", QColor("#000000"));
        canvas.setProperty("pointRadius", 10);
        canvas.setProperty("lineWidth", 2);
        canvas.setProperty("fillOpacity", 1);
        canvas.setCenterLongitude(0); // Export declares a different camera.
        canvas.setZoomLevel(1);
        const QString output = dir.filePath("composed.png");
        QVERIFY2(canvas.exportPng(output,400,300,-180,0,1), qPrintable(canvas.lastExportError()));
        QCOMPARE(canvas.centerLongitude(), 0.0);
        const QImage composed(output);
        QCOMPARE(composed.size(), QSize(400,300));
        QCOMPARE(composed.pixelColor(50,100), QColor("#ff0000"));
        QCOMPARE(composed.pixelColor(350,100), QColor("#0000ff"));
        QCOMPARE(composed.pixelColor(200,150), QColor("#00ffff"));
        QCOMPARE(composed.text("QindaTK/Attribution"), QStringLiteral("Vector author · Tile author"));
        QVERIFY(composed.text("QindaTK/CRS").contains("EPSG:3857"));
        QVERIFY(!canvas.exportPng(dir.filePath("bad.png"), 400,300,0,91,1));
        QVERIFY(!canvas.lastExportError().isEmpty());
    }
};
QTEST_MAIN(TestGeographicMap)
#include "tst_geographic_map.moc"
