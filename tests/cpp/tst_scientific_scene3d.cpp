// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_scene3d.h"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace QindaTK;

namespace {
bool near(double a, double b, double tolerance = 1e-3) { return std::abs(a - b) <= tolerance; }
// z = x * y on x, y in {0, 1, 2}; bilinear interpolation reproduces it exactly.
void product(ScientificGrid &grid, const QVariant &centre = 1.0)
{
    grid.setSourceId("product"); grid.setXUnit("m"); grid.setYUnit("m"); grid.setZUnit("K");
    QVERIFY(grid.replaceGrid({0.0, 1.0, 2.0}, {0.0, 1.0, 2.0}, {0.0, 0.0, 0.0, 0.0, centre, 2.0, 0.0, 2.0, 4.0}));
}
void style(ScientificScene3D &scene)
{
    scene.setProperty("surfaceColors", QVariant::fromValue(QList<QColor>{QColor(0, 0, 255), QColor(255, 0, 0)}));
    scene.setProperty("pointColor", QColor(Qt::black));
    scene.setProperty("frameColor", QColor(Qt::gray));
    scene.setProperty("backgroundColor", QColor(Qt::white));
    scene.setProperty("textColor", QColor(Qt::black));
    scene.setProperty("selectionColor", QColor(Qt::magenta));
    scene.setProperty("metrics", QVariantMap{{"fontFamily", "Sans Serif"}, {"fontSize", 11}, {"pointRadius", 3},
                                             {"lineWidth", 1}, {"hitRadius", 8}});
}
QVariantList probeRows()
{
    return {QVariantMap{{"id", "p0"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}},
            QVariantMap{{"id", "p1"}, {"x", 2.0}, {"y", 2.0}, {"z", 4.0}},
            QVariantMap{{"id", "p2"}, {"x", 0.5}, {"y", 1.5}, {"z", 2.25}},
            QVariantMap{{"id", "gap"}, {"x", 1.0}, {"missingY", true}}};
}
void probes(ScientificDataset &points, const QVariantList &rows = probeRows())
{
    points.setSourceId("probes"); points.setKind(ScientificDataset::Scatter); points.setDimensions(3);
    points.setXUnit("m"); points.setYUnit("m"); points.setZUnit("K");
    QVERIFY(points.replaceSamples(rows));
}
QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
// Builds every object from scratch, exports PNG and SVG, returns their bytes.
QPair<QByteArray, QByteArray> freshExport(const QString &directory, const QString &name)
{
    ScientificGrid grid; product(grid);
    ScientificDataset points; probes(points);
    ScientificScene3D scene; style(scene);
    scene.setGrid(&grid); scene.setPoints(&points);
    scene.setProperty("title", QStringLiteral("Fresh"));
    scene.fitDomain(); scene.setResolutionX(7); scene.setResolutionY(7);
    scene.setAzimuth(40); scene.setElevation(30);
    scene.selectPoint("p2");
    const QString png = directory + '/' + name + ".png", svg = directory + '/' + name + ".svg";
    if (!scene.exportPng(png, QSize(480, 360)) || !scene.exportSvg(svg, QSize(480, 360))) return {};
    return {readAll(png), readAll(svg)};
}
}

class ScientificScene3DTest : public QObject {
    Q_OBJECT
private slots:
    void mappingAndCameraTransforms();
    void surfaceMeshAndInterpolation();
    void pointsSelectionAndReproducibleExport();
    void pointBoundsAtScale();
    void pointsTableAndSummary();
    void freshInstancesExportIdenticalBytes();
};

void ScientificScene3DTest::mappingAndCameraTransforms()
{
    ScientificScene3D scene;
    scene.setXMinimum(0); scene.setXMaximum(10);
    scene.setYMinimum(0); scene.setYMaximum(20);
    scene.setZMinimum(-1); scene.setZMaximum(1);
    QCOMPARE(scene.problem(), QString());
    QCOMPARE(scene.toScene(10, 0, 0), QVector3D(100, 0, 100)); // data Z is scene up, data Y is scene -Z
    const QVariantMap back = scene.fromScene(scene.toScene(2.5, 15, 0.5));
    QVERIFY(near(back.value("x").toDouble(), 2.5, 1e-5));
    QVERIFY(near(back.value("y").toDouble(), 15, 1e-5));
    QVERIFY(near(back.value("z").toDouble(), 0.5, 1e-5));
    scene.setAzimuth(0); scene.setElevation(0); scene.setDistance(300); scene.setFieldOfView(90);
    QVERIFY(near(scene.cameraPosition().z(), 300));
    QVariantMap p = scene.project(5, 10, 0, 400, 400); // the domain centre is the orbit target
    QVERIFY(p.value("visible").toBool());
    QVERIFY(near(p.value("x").toDouble(), 200));
    QVERIFY(near(p.value("y").toDouble(), 200));
    p = scene.project(10, 10, 0, 400, 400); // +X at distance 300, 90 degree FOV: NDC x = 1/3
    QVERIFY(near(p.value("x").toDouble(), 800.0 / 3));
    QVERIFY(near(p.value("y").toDouble(), 200));
    p = scene.project(5, 10, 1, 400, 400); // data Z up is screen up
    QVERIFY(near(p.value("y").toDouble(), 400.0 / 3));
    scene.setAzimuth(90); // camera on +X looking at the origin; scene +Z is now screen left
    QVERIFY(near(scene.project(10, 10, 0, 400, 400).value("x").toDouble(), 200));
    QVERIFY(near(scene.project(5, 0, 0, 400, 400).value("x").toDouble(), 400.0 / 3));
    const QQuaternion q = scene.cameraRotation();
    QVERIFY(near(q.scalar(), std::sqrt(0.5), 1e-6) && near(q.y(), std::sqrt(0.5), 1e-6));
    scene.setElevation(120);
    QCOMPARE(scene.elevation(), 89.0);
    scene.setDistance(1);
    QCOMPARE(scene.distance(), 50.0);
    scene.setAzimuth(-30);
    QCOMPARE(scene.azimuth(), 330.0);
    scene.setZMaximum(-1);
    QVERIFY(!scene.problem().isEmpty());
}

void ScientificScene3DTest::surfaceMeshAndInterpolation()
{
    ScientificGrid grid; product(grid);
    ScientificScene3D scene; style(scene);
    scene.setGrid(&grid);
    QVERIFY(scene.fitDomain());
    QCOMPARE(scene.zMaximum(), 4.0);
    scene.setResolutionX(3); scene.setResolutionY(3);
    QCOMPARE(scene.triangleCount(), 8);
    QCOMPARE(scene.holeCount(), 0);
    QCOMPARE(scene.latticeNode(1, 1).value("z").toDouble(), 1.0);
    QCOMPARE(scene.surfaceMesh().positions[4], QVector3D(0, -50, 0));
    for (const QVector3D &n : scene.surfaceMesh().normals) QVERIFY(n.y() > 0); // faces point up (+Z data)
    scene.setResolutionX(5); scene.setResolutionY(5);
    QCOMPARE(scene.latticeNode(1, 1).value("z").toDouble(), 0.25);  // bilinear x*y at (0.5, 0.5)
    QCOMPARE(scene.latticeNode(1, 3).value("z").toDouble(), 0.75);  // (1.5, 0.5)
    QCOMPARE(scene.latticeNode(4, 4).value("z").toDouble(), 4.0);   // last line is the grid edge exactly
    QSignalSpy mesh(&scene, &ScientificScene3D::meshChanged);
    scene.setInterpolation(ScientificGrid::Nearest);
    QVERIFY(mesh.count() >= 1);
    QCOMPARE(scene.latticeNode(1, 1).value("z").toDouble(), 0.0);   // tie -> lower node (0, 0)
    QCOMPARE(scene.latticeNode(1, 3).value("z").toDouble(), 0.0);   // node (x 1, y 0)
    QCOMPARE(scene.csvData().split('\n', Qt::SkipEmptyParts).size(), 1 + 25);
    QVERIFY(scene.csvData().contains(QStringLiteral("1,3,1.5,0.5,0,false,nearest")));
    ScientificGrid holed; product(holed, QVariant());
    scene.setGrid(&holed);
    scene.setResolutionX(3); scene.setResolutionY(3);
    QCOMPARE(scene.holeCount(), 4);    // every cell touches the missing centre
    QCOMPARE(scene.triangleCount(), 0);
    scene.setInterpolation(ScientificGrid::Bilinear);
    scene.setResolutionX(5); scene.setResolutionY(5);
    QVERIFY(scene.holeCount() > 0);
    QCOMPARE(scene.triangleCount(), 2 * (16 - scene.holeCount()));
    QVERIFY(scene.latticeNode(2, 2).value("missing").toBool());
    QVERIFY(scene.textSummary().contains(QStringLiteral("bilinear")));
}

void ScientificScene3DTest::pointsSelectionAndReproducibleExport()
{
    ScientificGrid grid; product(grid);
    ScientificDataset points;
    points.setSourceId("probes"); points.setKind(ScientificDataset::Scatter); points.setDimensions(3);
    points.setXUnit("m"); points.setYUnit("m"); points.setZUnit("K");
    QVERIFY(points.replaceSamples({QVariantMap{{"id", "p0"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}},
        QVariantMap{{"id", "p1"}, {"x", 2.0}, {"y", 2.0}, {"z", 4.0}}, QVariantMap{{"id", "gap"}, {"x", 1.0}, {"missingY", true}}}));
    ScientificScene3D scene; style(scene);
    scene.setGrid(&grid); scene.setPoints(&points);
    QVERIFY(scene.fitDomain());
    scene.setResolutionX(9); scene.setResolutionY(9);
    scene.setAzimuth(30); scene.setElevation(20); scene.setDistance(400); scene.setFieldOfView(45);
    QCOMPARE(scene.pointCount(), 2);
    const QVariantMap p1 = scene.project(2, 2, 4, 640, 480);
    QVERIFY(p1.value("visible").toBool());
    QCOMPARE(scene.hitPoint(p1.value("x").toDouble() + 2, p1.value("y").toDouble(), 640, 480), QStringLiteral("p1"));
    QSignalSpy picked(&scene, &ScientificScene3D::pointSelected);
    QVERIFY(scene.selectPoint("p1"));
    QCOMPARE(picked.count(), 1);
    QVERIFY(!scene.selectPoint("gap")); // missing samples are not drawable points
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QSize size(640, 480);
    QVERIFY(scene.exportPng(dir.filePath("a.png"), size));
    QVERIFY(scene.exportPng(dir.filePath("b.png"), size));
    QVERIFY(scene.exportSvg(dir.filePath("c.svg"), size));
    QFile a(dir.filePath("a.png")), b(dir.filePath("b.png")), c(dir.filePath("c.svg"));
    QVERIFY(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly) && c.open(QIODevice::ReadOnly));
    QCOMPARE(a.readAll(), b.readAll());
    QVERIFY(c.readAll().contains("qindatk-scientific-3d"));
    const QImage image(dir.filePath("a.png"));
    const QVariantMap onSurface = scene.project(1, 1, 1, 640, 480); // a lattice node of z = x y
    QCOMPARE(image.pixelColor(2, 2), QColor(Qt::white));
    QVERIFY(image.pixelColor(int(onSurface.value("x").toDouble()), int(onSurface.value("y").toDouble())) != QColor(Qt::white));
    const QJsonObject meta = QJsonDocument::fromJson(image.text("QindaTK.Scientific3D").toUtf8()).object();
    QCOMPARE(meta.value("schema").toString(), QStringLiteral("QindaTK.scientific3d.v1"));
    QCOMPARE(meta.value("camera").toObject().value("azimuth").toDouble(), 30.0);
    QCOMPARE(meta.value("resolution").toArray().at(0).toInt(), 9);
    QCOMPARE(meta.value("triangleCount").toInt(), scene.triangleCount());
    QCOMPARE(meta.value("surface").toObject().value("valuesSha256").toString().size(), 64);
    QCOMPARE(meta.value("points").toObject().value("samplesSha256").toString().size(), 64);
    QCOMPARE(meta.value("domain").toArray().at(5).toDouble(), 4.0);
    const QVariantList labels = scene.axisLabels(640, 480);
    QCOMPARE(labels.size(), 4);
    QCOMPARE(labels.at(1).toMap().value("text").toString(), QStringLiteral("x 2 m"));
    QCOMPARE(labels.at(3).toMap().value("text").toString(), QStringLiteral("z 4 K"));
    const QVariantMap corner = scene.project(2, 0, 0, 640, 480);
    QCOMPARE(labels.at(1).toMap().value("x").toDouble(), corner.value("x").toDouble());
    points.setZUnit("mK");
    QVERIFY(!scene.problem().isEmpty());
    QCOMPARE(scene.triangleCount(), 0);
}

void ScientificScene3DTest::pointBoundsAtScale()
{
    // 100,000 present samples on a helix; z extremes sit mid-series.
    QVariantList rows;
    rows.reserve(ScientificDataset::SampleLimit);
    for (int i = 0; i < ScientificDataset::SampleLimit; ++i) {
        const double t = i * 0.001;
        rows.append(QVariantMap{{"id", QStringLiteral("s%1").arg(i)}, {"x", std::cos(t)}, {"y", std::sin(t)},
                                {"z", i == 31337 ? 9.0 : i == 77777 ? -9.0 : t / 100}});
    }
    ScientificDataset points; probes(points, rows);
    ScientificScene3D scene; style(scene);
    scene.setPoints(&points);
    QVERIFY(scene.fitDomain());
    QCOMPARE(scene.pointLimit(), int(ScientificScene3D::DefaultPointLimit));
    QCOMPARE(scene.sourcePointCount(), 100000);
    QVERIFY(scene.pointCount() <= scene.pointLimit());
    QVERIFY(scene.pointCount() > scene.pointLimit() / 2);
    for (const char *extreme : {"s31337", "s77777"}) QVERIFY(scene.pointIds().contains(QString::fromLatin1(extreme)));
    scene.setPointLimit(1 << 30);
    QCOMPARE(scene.pointLimit(), int(ScientificScene3D::PointLimitMaximum));
    QVERIFY(scene.pointCount() <= ScientificScene3D::PointLimitMaximum);
    scene.setPointLimit(1000);
    QVERIFY(scene.pointCount() <= 1000);
    // Selecting a drawn point is not a mesh change; a thinned one is added once.
    QSignalSpy mesh(&scene, &ScientificScene3D::meshChanged);
    const QString drawn = scene.pointIds().at(10);
    QVERIFY(scene.selectPoint(drawn));
    QCOMPARE(mesh.count(), 0);
    QVERIFY(!scene.pointIds().contains(QStringLiteral("s12345")));
    QVERIFY(scene.selectPoint("s12345"));
    QCOMPARE(mesh.count(), 1);
    QVERIFY(scene.pointIds().contains(QStringLiteral("s12345")));
    QVERIFY(scene.pointCount() <= 1000);
    const QVariantMap at = scene.project(std::cos(12.345), std::sin(12.345), 0.12345, 640, 480);
    QCOMPARE(scene.hitPoint(at.value("x").toDouble(), at.value("y").toDouble(), 640, 480), QStringLiteral("s12345"));
    const QJsonObject meta = QJsonDocument::fromJson(scene.metadataJson(640, 480).toUtf8()).object();
    const QJsonObject sampling = meta.value("pointSampling").toObject();
    QCOMPARE(sampling.value("present").toInt(), 100000);
    QCOMPARE(sampling.value("drawn").toInt(), scene.pointCount());
    QCOMPARE(sampling.value("limit").toInt(), 1000);
    QVERIFY(sampling.value("method").toString().startsWith(QStringLiteral("stride")));
    QVERIFY(scene.textSummary().contains(QStringLiteral(" of 100000 points")));
    QVERIFY(!scene.renderImage(QSize(320, 240)).isNull());
}

void ScientificScene3DTest::pointsTableAndSummary()
{
    ScientificGrid grid; product(grid);
    ScientificDataset points; probes(points);
    ScientificScene3D scene; style(scene);
    scene.setGrid(&grid); scene.setPoints(&points);
    QVERIFY(scene.fitDomain());
    scene.setResolutionX(3); scene.setResolutionY(3);
    // The lattice CSV carries units; the points CSV has every source row.
    const QStringList lattice = scene.csvData().split('\n', Qt::SkipEmptyParts);
    QCOMPARE(lattice.first(), QStringLiteral("row,column,x,y,z,missing,interpolation,xUnit,yUnit,zUnit"));
    QCOMPARE(lattice.at(1 + 4), QStringLiteral("1,1,1,1,1,false,bilinear,m,m,K"));
    const QStringList rows = scene.pointsCsvData().split('\n', Qt::SkipEmptyParts);
    QCOMPARE(rows.size(), 1 + 4);
    QVERIFY(rows.at(3).startsWith(QStringLiteral("\"probes\",\"p2\",\"0.5\",\"1.5\"")));
    QVERIFY(rows.at(3).contains(QStringLiteral("\"2.25\",\"m\",\"m\",\"K\"")));
    QVERIFY(rows.at(4).contains(QStringLiteral("\"gap\"")));
    // The summary names the points with coordinates and units, and the selection.
    QSignalSpy summary(&scene, &ScientificScene3D::summaryChanged);
    QVERIFY(scene.textSummary().contains(QStringLiteral("p2 at X 0.5 m, Y 1.5 m, Z 2.25 K")));
    QVERIFY(scene.selectPoint("p1"));
    QVERIFY(summary.count() >= 1);
    QVERIFY(scene.textSummary().contains(QStringLiteral("Selected point p1 at X 2 m, Y 2 m, Z 4 K.")));
}

void ScientificScene3DTest::freshInstancesExportIdenticalBytes()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto first = freshExport(dir.path(), "first"), second = freshExport(dir.path(), "second");
    QVERIFY(!first.first.isEmpty() && !first.second.isEmpty());
    QCOMPARE(first.first, second.first);   // PNG bytes from independent instances
    QCOMPARE(first.second, second.second); // SVG bytes too
    // The embedded provenance holds the input values and every render input.
    const QImage image = QImage::fromData(first.first, "PNG");
    const QJsonObject meta = QJsonDocument::fromJson(image.text("QindaTK.Scientific3D").toUtf8()).object();
    const QJsonArray samples = meta.value("points").toObject().value("samples").toArray();
    const QVariantList input = probeRows();
    QCOMPARE(samples.size(), input.size());
    for (int i = 0; i < input.size(); ++i) {
        const QVariantMap row = input.at(i).toMap();
        const QJsonObject out = samples.at(i).toObject();
        QCOMPARE(out.value("id").toString(), row.value("id").toString());
        QCOMPARE(out.value("x").toDouble(), row.value("x").toDouble());
        QCOMPARE(out.value("missingY").toBool(), row.value("missingY").toBool());
        if (!row.value("missingY").toBool()) {
            QCOMPARE(out.value("y").toDouble(), row.value("y").toDouble());
            QCOMPARE(out.value("z").toDouble(), row.value("z").toDouble());
        }
    }
    QCOMPARE(meta.value("selectedPoint").toString(), QStringLiteral("p2"));
    const QJsonObject styleMeta = meta.value("style").toObject();
    QCOMPARE(styleMeta.value("selectionColor").toString(), QStringLiteral("#ffff00ff"));
    QCOMPARE(styleMeta.value("surfaceColors").toArray().at(1).toString(), QStringLiteral("#ffff0000"));
    QCOMPARE(styleMeta.value("metrics").toObject().value("fontSize").toInt(), 11);
    QVERIFY(!styleMeta.value("resolvedFontFamily").toString().isEmpty());
    // Anything that changes the pixels changes the metadata.
    ScientificGrid grid; product(grid);
    ScientificDataset points; probes(points);
    ScientificScene3D scene; style(scene);
    scene.setGrid(&grid); scene.setPoints(&points); scene.fitDomain();
    const QString plain = scene.metadataJson(480, 360);
    QVERIFY(scene.selectPoint("p0"));
    const QString selected = scene.metadataJson(480, 360);
    QVERIFY(selected != plain);
    scene.setProperty("pointColor", QColor(Qt::darkBlue));
    QVERIFY(scene.metadataJson(480, 360) != selected);
}

QTEST_MAIN(ScientificScene3DTest)
#include "tst_scientific_scene3d.moc"
