// SPDX-License-Identifier: LGPL-3.0-or-later
#include "polar_data_model.h"
#include "polar_renderer.h"
#include "polar_transform.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
#include <numbers>

using namespace QindaTK;

namespace {
QVariantMap numeric(const QString &id, double angle, double radius,
                    int kind = PolarDataModel::Points,
                    const QString &series = QStringLiteral("wind"))
{
    return {{QStringLiteral("seriesId"), series},
            {QStringLiteral("seriesName"), QStringLiteral("Wind, measured")},
            {QStringLiteral("pointId"), id}, {QStringLiteral("angle"), angle},
            {QStringLiteral("radius"), radius}, {QStringLiteral("kind"), kind}};
}
QVariantMap category(const QString &id, const QString &label, double radius,
                     int kind = PolarDataModel::Polygon)
{
    return {{QStringLiteral("seriesId"), QStringLiteral("scores")},
            {QStringLiteral("seriesName"), QStringLiteral("Scores")},
            {QStringLiteral("pointId"), id}, {QStringLiteral("category"), label},
            {QStringLiteral("radius"), radius}, {QStringLiteral("kind"), kind}};
}
void near(double actual, double expected)
{ QVERIFY2(std::abs(actual - expected) < 1e-8, "Unexpected polar coordinate"); }
}

class TestPolar : public QObject {
    Q_OBJECT
private slots:
    void cardinalProjectionAndInverse();
    void exactAngleUnitAndDirection();
    void atomicSourceValidation();
    void categoryOrderAndTransformedHit();
    void radialBarInteriorHit();
    void tableCsvAndImageExport();
};

void TestPolar::cardinalProjectionAndInverse()
{
    const QRectF square(0, 0, 200, 200);
    const QPointF north = PolarTransform::project(0, 10, square, 0, 10, false, 0, true);
    const QPointF east = PolarTransform::project(90, 10, square, 0, 10, false, 0, true);
    const QPointF south = PolarTransform::project(180, 10, square, 0, 10, false, 0, true);
    const QPointF west = PolarTransform::project(270, 10, square, 0, 10, false, 0, true);
    near(north.x(), 100); near(north.y(), 0);
    near(east.x(), 200); near(east.y(), 100);
    near(south.x(), 100); near(south.y(), 200);
    near(west.x(), 0); near(west.y(), 100);
    const auto inverse = PolarTransform::unproject(east, square, 0, 10, false, 0, true);
    near(inverse.angle, 90); near(inverse.radius, 10);
    const auto inner = PolarTransform::project(90, 5, square, 0, 10, false, 0, true);
    near(inner.x(), 150); near(inner.y(), 100);
}

void TestPolar::exactAngleUnitAndDirection()
{
    const QRectF square(0, 0, 200, 200);
    const QPointF radians = PolarTransform::project(std::numbers::pi / 2, 10,
        square, 0, 10, true, 0, true);
    near(radians.x(), 200); near(radians.y(), 100);
    const QPointF ccw = PolarTransform::project(90, 10, square, 0, 10, false, 0, false);
    near(ccw.x(), 0); near(ccw.y(), 100);
    const QPointF rotated = PolarTransform::project(90, 10, square, 0, 10, false, 90, false);
    near(rotated.x(), 100); near(rotated.y(), 0);
    const auto inverse = PolarTransform::unproject(radians, square, 0, 10, true, 0, true);
    near(inverse.angle, std::numbers::pi / 2);
    near(PolarTransform::angularDistanceDegrees(359, 1), 2);
}

void TestPolar::atomicSourceValidation()
{
    PolarDataModel model;
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QVERIFY(model.replacePoints({numeric(QStringLiteral("p1"), 0, 4),
                                 numeric(QStringLiteral("p2"), 90, 5)}));
    QCOMPARE(model.count(), 2);
    auto bad = numeric(QStringLiteral("bad"), 45,
                       std::numeric_limits<double>::infinity());
    QVERIFY(!model.appendPoint(bad));
    QCOMPARE(model.count(), 2);
    QVERIFY(!model.replacePoints({numeric(QStringLiteral("p1"), 0, 4),
                                  numeric(QStringLiteral("p1"), 90, 5)}));
    QCOMPARE(model.count(), 2);
    QCOMPARE(reset.size(), 1);
    bad = numeric(QStringLiteral("bad"), 45, 3);
    bad.insert(QStringLiteral("category"), QStringLiteral("North"));
    QVERIFY(!model.appendPoint(bad));
    QVERIFY(model.lastError().contains(QStringLiteral("exactly one")));
}

void TestPolar::categoryOrderAndTransformedHit()
{
    PolarDataModel model;
    // Ingest out of category order; the polygon follows declared axis order.
    QVERIFY(model.replacePoints({category(QStringLiteral("s"), QStringLiteral("South"), 7),
                                 category(QStringLiteral("n"), QStringLiteral("North"), 6),
                                 category(QStringLiteral("w"), QStringLiteral("West"), 8),
                                 category(QStringLiteral("e"), QStringLiteral("East"), 5)}));
    PolarRenderer renderer;
    renderer.setWidth(300); renderer.setHeight(300);
    renderer.setModel(&model);
    renderer.setAngleMode(PolarRenderer::Category);
    renderer.setCategories({QStringLiteral("North"), QStringLiteral("East"),
                            QStringLiteral("South"), QStringLiteral("West")});
    renderer.setRadialMaximum(10);
    renderer.setStartAngleDegrees(90);
    renderer.setClockwise(false);
    renderer.setMetrics({{QStringLiteral("margin"), 20}, {QStringLiteral("hitRadius"), 9},
                         {QStringLiteral("pointRadius"), 4}});
    QVERIFY(renderer.valid());
    const QPointF east = renderer.projectCategory(QStringLiteral("East"), 5);
    near(east.x(), 150); near(east.y(), 85); // Start angle + counterclockwise East = north.
    QCOMPARE(renderer.unproject(east.x(), east.y()).value(QStringLiteral("category")).toString(),
             QStringLiteral("East"));
    QCOMPARE(renderer.hitTest(east.x(), east.y()).value(QStringLiteral("pointId")).toString(),
             QStringLiteral("e"));
    QSignalSpy selected(&renderer, &PolarRenderer::pointSelected);
    QVERIFY(renderer.selectAt(east.x(), east.y()));
    QCOMPARE(renderer.selectedPointId(), QStringLiteral("e"));
    QCOMPARE(selected.size(), 1);
    QVERIFY(renderer.csv().contains(QStringLiteral("\"East\",\"1\"")));
    renderer.setCategories({QStringLiteral("North"), QStringLiteral("East")});
    QVERIFY(!renderer.valid());
    QVERIFY(renderer.error().contains(QStringLiteral("absent"))
            || renderer.error().contains(QStringLiteral("every")));
}

void TestPolar::tableCsvAndImageExport()
{
    PolarDataModel model;
    QVERIFY(model.replacePoints({numeric(QStringLiteral("a"), 0, 4),
                                 numeric(QStringLiteral("b"), 90, 6),
                                 numeric(QStringLiteral("c"), 180, 3)}));
    PolarRenderer renderer;
    renderer.setWidth(240); renderer.setHeight(240);
    renderer.setModel(&model);
    renderer.setRadialMaximum(10);
    renderer.setRadialUnit(QStringLiteral("m/s"));
    renderer.setBackgroundColor(QColor(QStringLiteral("#101010")));
    renderer.setGridColor(QColor(QStringLiteral("#404040")));
    renderer.setTextColor(QColor(QStringLiteral("#ffffff")));
    renderer.setDefaultSeriesColor(QColor(QStringLiteral("#00ffff")));
    renderer.setMetrics({{QStringLiteral("margin"), 24}, {QStringLiteral("gridWidth"), 1},
                         {QStringLiteral("lineWidth"), 2}, {QStringLiteral("pointRadius"), 4},
                         {QStringLiteral("fontSize"), 10}, {QStringLiteral("labelGap"), 5},
                         {QStringLiteral("labelWidth"), 70}});
    QCOMPARE(model.get(1).value(QStringLiteral("pointId")).toString(), QStringLiteral("b"));
    QVERIFY(renderer.csv().contains(QStringLiteral("\"Wind, measured\"")));
    QVERIFY(renderer.csv().contains(QStringLiteral("\"m/s\"")));
    const QImage image = renderer.renderImage(QSize(240, 240));
    QCOMPARE(image.size(), QSize(240, 240));
    QVERIFY(image.pixelColor(120, 120) != QColor(QStringLiteral("#101010")));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath(QStringLiteral("polar.png"));
    const QString svg = dir.filePath(QStringLiteral("polar.svg"));
    QVERIFY(renderer.exportPng(png, QSize(240, 240)));
    QVERIFY(renderer.exportSvg(svg, QSize(240, 240)));
    QVERIFY(QFile(png).size() > 100);
    QVERIFY(QFile(svg).size() > 100);
    QVERIFY(!renderer.exportPng(png, QSize(9000, 1)));
}

void TestPolar::radialBarInteriorHit()
{
    PolarDataModel model;
    QVERIFY(model.replacePoints({category(QStringLiteral("east"), QStringLiteral("East"),
                                          0.8, PolarDataModel::RadialBar)}));
    PolarRenderer renderer;
    renderer.setWidth(200); renderer.setHeight(200);
    renderer.setModel(&model);
    renderer.setAngleMode(PolarRenderer::Category);
    renderer.setCategories({QStringLiteral("North"), QStringLiteral("East"),
                            QStringLiteral("South"), QStringLiteral("West")});
    renderer.setMetrics({{QStringLiteral("margin"), 0}, {QStringLiteral("hitRadius"), 0}});
    QVERIFY(renderer.valid());
    QCOMPARE(renderer.hitTest(140, 100).value(QStringLiteral("pointId")).toString(),
             QStringLiteral("east"));
    QVERIFY(renderer.hitTest(190, 100).isEmpty());
    QVERIFY(renderer.unproject(100, 100).isEmpty()); // No bearing at the origin.
}

QTEST_MAIN(TestPolar)
#include "tst_polar.moc"
