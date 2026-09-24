// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_figure_composer.h"
#include "scientific_plot_renderer.h"
#include "scientific_sampling.h"

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
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }
// Plot rect without a surface: (64, 24) 560 x 320 in a 640 x 400 item.
void style(ScientificPlotRenderer &plot, ChartAxis &x, ChartAxis &y)
{
    plot.setXAxis(&x); plot.setYAxis(&y);
    plot.setWidth(640); plot.setHeight(400);
    plot.setProperty("backgroundColor", QColor(Qt::white));
    plot.setProperty("gridColor", QColor(Qt::gray));
    plot.setProperty("textColor", QColor(Qt::black));
    plot.setProperty("missingColor", QColor(0, 255, 0));
    plot.setProperty("selectionColor", QColor(Qt::magenta));
    plot.setProperty("annotationColor", QColor(Qt::darkRed));
    plot.setProperty("seriesColors", QVariant::fromValue(QList<QColor>{QColor(Qt::blue), QColor(Qt::darkGreen)}));
    plot.setProperty("surfaceColors", QVariant::fromValue(QList<QColor>{QColor(0, 0, 255), QColor(255, 0, 0)}));
    plot.setProperty("metrics", QVariantMap{{"leftMargin", 64}, {"rightMargin", 16}, {"topMargin", 24},
        {"bottomMargin", 56}, {"fontSize", 11}, {"fontFamily", "Sans Serif"}, {"lineWidth", 1},
        {"pointRadius", 3}, {"capHalfWidth", 4}, {"labelGap", 3}, {"tickLabelWidth", 48},
        {"hitRadius", 8}, {"colorBarWidth", 12}});
}
void axes(ChartAxis &x, ChartAxis &y, double yMax = 100)
{
    x.setUnit("s"); x.setMinimum(0); x.setMaximum(10);
    y.setUnit("V"); y.setMinimum(0); y.setMaximum(yMax);
}
void squares(ScientificDataset &d)
{
    d.setSourceId("squares"); d.setXUnit("s"); d.setYUnit("V");
    QVariantList rows;
    for (int x = 0; x <= 10; ++x) {
        QVariantMap row{{"id", QStringLiteral("a%1").arg(x)}, {"x", double(x)}, {"y", double(x * x)}};
        if (x == 5) { row.insert("sigmaYLow", 1.0); row.insert("sigmaYHigh", 2.0); }
        rows.append(row);
    }
    QVERIFY(d.replaceSamples(rows));
}
}

class ScientificFigureTest : public QObject {
    Q_OBJECT
private slots:
    void multiSeriesProjectionAndSelection();
    void calibratedTicksAndLogLimits();
    void downsamplingKeepsExtremesAndGaps();
    void surfaceLayerCursorAndHoles();
    void annotationsAndReproducibleExport();
    void composedPanels();
};

void ScientificFigureTest::multiSeriesProjectionAndSelection()
{
    ChartAxis x, y; axes(x, y);
    ScientificDataset a; squares(a);
    ScientificDataset b; b.setSourceId("scatter-run"); b.setKind(ScientificDataset::Scatter);
    b.setXUnit("s"); b.setYUnit("V");
    QVERIFY(b.replaceSamples({QVariantMap{{"id", "b1"}, {"x", 5.0}, {"y", 50.0}, {"sigmaX", 1.0}}}));
    ScientificPlotRenderer plot; style(plot, x, y);
    plot.appendSeries(&a); plot.appendSeries(&b);
    QCOMPARE(plot.problem(), QString());
    const QVariantMap p5 = plot.projected(0, "a5");
    QVERIFY(near(p5.value("x").toDouble(), 344));
    QVERIFY(near(p5.value("y").toDouble(), 264));
    QVERIFY(near(p5.value("lowY").toDouble(), 267.2));
    QVERIFY(near(p5.value("highY").toDouble(), 257.6));
    const QVariantMap b1 = plot.projected(1, "b1");
    QVERIFY(near(b1.value("y").toDouble(), 184));
    QVERIFY(near(b1.value("lowX").toDouble(), 288));
    QVERIFY(near(b1.value("highX").toDouble(), 400));
    QCOMPARE(plot.hitAt(344, 184).value("id").toString(), QStringLiteral("b1"));
    QCOMPARE(plot.hitAt(344, 264).value("series").toInt(), 0);
    QSignalSpy selected(&plot, &ScientificPlotRenderer::sampleSelected);
    QVERIFY(plot.selectSample(1, "b1"));
    QCOMPARE(selected.count(), 1);
    QCOMPARE(plot.selectedSample().value("sourceId").toString(), QStringLiteral("scatter-run"));
    QVERIFY(plot.navigateSelection(Qt::Key_Up)); // previous series, nearest X
    QCOMPARE(plot.selectedSeries(), 0);
    QCOMPARE(plot.selectedSampleId(), QStringLiteral("a5"));
    QVERIFY(plot.navigateSelection(Qt::Key_Right));
    QCOMPARE(plot.selectedSampleId(), QStringLiteral("a6"));
    const QVariantMap cursor = plot.dataAt(344, 184);
    QVERIFY(near(cursor.value("x").toDouble(), 5));
    QVERIFY(near(cursor.value("y").toDouble(), 50));
    QCOMPARE(cursor.value("yUnit").toString(), QStringLiteral("V"));
    ScientificDataset other; other.setSourceId("millivolts"); other.setXUnit("s"); other.setYUnit("mV");
    plot.appendSeries(&other);
    QVERIFY(plot.problem().contains(QStringLiteral("mV")));
    QVERIFY(plot.paintedIndices(0).isEmpty());
    QVERIFY(plot.textSummary().contains(QStringLiteral("mV")));
    plot.clearSeries();
    QCOMPARE(plot.selectedSeries(), -1);
}

void ScientificFigureTest::calibratedTicksAndLogLimits()
{
    using ScientificSampling::ticks;
    QCOMPARE(ticks(false, 0, 10, 6), (QVector<double>{0, 2, 4, 6, 8, 10}));
    QCOMPARE(ticks(false, -1, 1, 5), (QVector<double>{-1, -0.5, 0, 0.5, 1}));
    QCOMPARE(ticks(true, 1, 100, 6), (QVector<double>{1, 2, 5, 10, 20, 50, 100}));
    QCOMPARE(ticks(true, 0.1, 1000, 6), (QVector<double>{0.1, 1, 10, 100, 1000}));
    QVERIFY(ticks(true, 0, 10, 5).isEmpty());
    QCOMPARE(ScientificSampling::tickLabel(0.1), QStringLiteral("0.1"));
    ChartAxis x, y; axes(x, y);
    y.setKind(ChartAxis::Logarithmic); y.setMinimum(1); y.setMaximum(100);
    ScientificDataset d; d.setSourceId("log"); d.setXUnit("s"); d.setYUnit("V");
    QVERIFY(d.replaceSamples({QVariantMap{{"id", "zero"}, {"x", 1.0}, {"y", 0.0}},
        QVariantMap{{"id", "five"}, {"x", 2.0}, {"y", 5.0}}, QVariantMap{{"id", "negative"}, {"x", 3.0}, {"y", -3.0}}}));
    ScientificPlotRenderer plot; style(plot, x, y); plot.appendSeries(&d);
    QCOMPARE(plot.ticks("x"), (QVariantList{0.0, 2.0, 4.0, 6.0, 8.0, 10.0}));
    QCOMPARE(plot.ticks("y"), (QVariantList{1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0}));
    QCOMPARE(plot.omittedCount(), 2);
    QVERIFY(plot.textSummary().contains(QStringLiteral("2 samples lie outside")));
    QVERIFY(plot.projected(0, "zero").isEmpty());
    QVERIFY(!plot.projected(0, "five").isEmpty());
    y.setMinimum(0); // log domain must be positive
    QVERIFY(!plot.problem().isEmpty());
}

void ScientificFigureTest::downsamplingKeepsExtremesAndGaps()
{
    QVector<ScientificSample> small(10);
    const double ys[10] = {0, 5, 1, 9, 2, 2, 8, 3, 4, 1};
    for (int i = 0; i < 10; ++i) { small[i].x = i; small[i].y = ys[i]; }
    QCOMPARE(ScientificSampling::traceEnvelope(small, 0, 10, 2), (QVector<int>{0, 3, 4, 5, 6, 9}));
    QVector<int> candidates;
    QVector<ScientificSample> cloud(100);
    for (int i = 0; i < 100; ++i) { cloud[i].x = i; cloud[i].y = i == 57 ? 1000 : i % 7; candidates.append(i); }
    const QVector<int> thinned = ScientificSampling::scatterStride(cloud, candidates, 12);
    QVERIFY(thinned.size() <= 12);
    QVERIFY(thinned.contains(57));
    QVERIFY(thinned.contains(99));

    ChartAxis x, y;
    x.setUnit("s"); x.setMinimum(0); x.setMaximum(99999); y.setUnit("V"); y.setMinimum(-2); y.setMaximum(1001);
    ScientificDataset d; d.setSourceId("long"); d.setXUnit("s"); d.setYUnit("V");
    QVariantList rows; rows.reserve(ScientificDataset::SampleLimit);
    for (int i = 0; i < ScientificDataset::SampleLimit; ++i) {
        QVariantMap row{{"id", QStringLiteral("s%1").arg(i)}, {"x", double(i)}};
        if (i == 77777) row.insert("missingY", true);
        else row.insert("y", i == 54321 ? 1000.0 : std::sin(i * 0.001));
        rows.append(row);
    }
    QVERIFY(d.replaceSamples(rows));
    ScientificPlotRenderer plot; style(plot, x, y); plot.appendSeries(&d);
    plot.setPaintPointLimit(2000);
    const QVariantList painted = plot.paintedIndices(0);
    QVERIFY(painted.size() <= 2000);
    QVERIFY(painted.size() > 400);
    QVERIFY(painted.contains(54321)); // the spike survives
    QVERIFY(painted.contains(77777)); // the gap survives
    QCOMPARE(painted.first().toInt(), 0);
    QCOMPARE(painted.last().toInt(), 99999);
    for (int i = 1; i < painted.size(); ++i) QVERIFY(painted[i].toInt() > painted[i - 1].toInt());
    plot.zoom(100, 0.5, 0.5); // zoomed in: the visible range is painted exactly
    const QVariantList zoomed = plot.paintedIndices(0);
    QVERIFY(zoomed.size() < 2000);
    QCOMPARE(zoomed.last().toInt() - zoomed.first().toInt() + 1, zoomed.size());
}

void ScientificFigureTest::surfaceLayerCursorAndHoles()
{
    ChartAxis x, y; axes(x, y);
    ScientificGrid grid; grid.setSourceId("sum"); grid.setXUnit("s"); grid.setYUnit("V"); grid.setZUnit("K");
    QVERIFY(grid.replaceGrid({0.0, 5.0, 10.0}, {0.0, 50.0, 100.0}, {0.0, 5.0, 10.0, 50.0, 55.0, 60.0, 100.0, 105.0, 110.0}));
    ScientificPlotRenderer plot; style(plot, x, y); plot.setSurface(&grid);
    // A colour bar now takes 12 + 48 + 2 * 3 px: the plot is (64, 24) 494 x 320.
    const QVariantMap centre = plot.dataAt(311, 184);
    QCOMPARE(centre.value("x").toDouble(), 5.0);
    QCOMPARE(centre.value("y").toDouble(), 50.0);
    QCOMPARE(centre.value("z").toDouble(), 55.0);
    QCOMPARE(centre.value("zUnit").toString(), QStringLiteral("K"));
    plot.setSurfaceInterpolation(ScientificGrid::Nearest);
    QCOMPARE(plot.dataAt(440, 184).value("z").toDouble(), 60.0);
    plot.setSurfaceInterpolation(ScientificGrid::Bilinear);
    const QImage image = plot.renderImage(QSize(640, 400));
    const QColor mid = image.pixelColor(311, 184);
    QVERIFY(std::abs(mid.red() - 127) <= 4 && mid.green() <= 3 && std::abs(mid.blue() - 128) <= 4);
    QVERIFY(grid.replaceGrid({0.0, 5.0, 10.0}, {0.0, 50.0, 100.0}, {QVariant(), 5.0, 10.0, 50.0, 55.0, 60.0, 100.0, 105.0, 110.0}));
    const QImage holed = plot.renderImage(QSize(640, 400));
    QCOMPARE(holed.pixelColor(113, 312), QColor(0, 255, 0)); // missing, never a ramp colour
    QVERIFY(plot.dataAt(113, 312).value("surfaceMissing").toBool());
    const QJsonObject meta = QJsonDocument::fromJson(plot.metadataJson(640, 400).toUtf8()).object();
    QCOMPARE(meta.value("surface").toObject().value("valuesSha256").toString().size(), 64);
    QCOMPARE(meta.value("render").toObject().value("surface").toObject().value("interpolation").toString(), QStringLiteral("bilinear"));
    QCOMPARE(meta.value("render").toObject().value("surface").toObject().value("rasterWidth").toInt(), 494);
    QVERIFY(plot.textSummary().contains(QStringLiteral("3 by 3 nodes")));
    grid.setZUnit("K"); grid.setXUnit("m");
    QVERIFY(!plot.problem().isEmpty());
}

void ScientificFigureTest::annotationsAndReproducibleExport()
{
    ChartAxis x, y; axes(x, y);
    ScientificDataset a; squares(a);
    ScientificDataset b; b.setSourceId("line"); b.setLabel("Linear"); b.setXUnit("s"); b.setYUnit("V");
    QVERIFY(b.replaceSamples({QVariantMap{{"id", "l0"}, {"x", 0.0}, {"y", 0.0}}, QVariantMap{{"id", "l1"}, {"x", 10.0}, {"y", 100.0}}}));
    ChartAnnotationModel notes;
    QVERIFY(notes.replaceAnnotations({QVariantMap{{"id", "peak"}, {"label", "Peak"}, {"kind", 0}, {"x", 5.0}, {"y", 25.0},
        {"xUnit", "s"}, {"yUnit", "V"}}, QVariantMap{{"id", "limit"}, {"label", "Limit"}, {"kind", 2}, {"y", 80.0}}}));
    ScientificPlotRenderer plot; style(plot, x, y);
    plot.appendSeries(&a); plot.appendSeries(&b); plot.setAnnotations(&notes);
    plot.setProperty("title", QStringLiteral("Known response"));
    plot.setProperty("xLabel", QStringLiteral("Time"));
    QCOMPARE(plot.problem(), QString());
    QVERIFY(plot.textSummary().contains(QStringLiteral("Note Peak: X = 5, Y = 25.")));
    QVERIFY(plot.textSummary().contains(QStringLiteral("Time axis linear from 0 to 10 s.")));
    const QStringList csv = plot.csvData().split('\n', Qt::SkipEmptyParts);
    QCOMPARE(csv.size(), 1 + 11 + 2);
    QVERIFY(csv.first().startsWith(QStringLiteral("series,sourceId,sampleId")));
    QVERIFY(csv.last().startsWith(QStringLiteral("\"1\",\"line\",\"l1\"")));
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QSize size(800, 500);
    QVERIFY(plot.exportPng(dir.filePath("one.png"), size));
    QVERIFY(plot.exportPng(dir.filePath("two.png"), size));
    QVERIFY(plot.exportSvg(dir.filePath("figure.svg"), size));
    QFile one(dir.filePath("one.png")), two(dir.filePath("two.png")), svg(dir.filePath("figure.svg"));
    QVERIFY(one.open(QIODevice::ReadOnly) && two.open(QIODevice::ReadOnly) && svg.open(QIODevice::ReadOnly));
    QCOMPARE(one.readAll(), two.readAll()); // identical inputs -> identical bytes
    QVERIFY(svg.readAll().contains("qindatk-scientific-figure"));
    const QImage image(dir.filePath("one.png"));
    QCOMPARE(image.size(), size);
    const QJsonObject meta = QJsonDocument::fromJson(image.text("QindaTK.ScientificFigure").toUtf8()).object();
    QCOMPARE(meta.value("schema").toString(), QStringLiteral("QindaTK.scientificFigure.v1"));
    QCOMPARE(meta.value("series").toArray().size(), 2);
    QCOMPARE(meta.value("series").toArray().at(0).toObject().value("samplesSha256").toString().size(), 64);
    QCOMPARE(meta.value("series").toArray().at(1).toObject().value("label").toString(), QStringLiteral("Linear"));
    QCOMPARE(meta.value("annotations").toArray().size(), 2);
    const QJsonObject render = meta.value("render").toObject();
    QCOMPARE(render.value("title").toString(), QStringLiteral("Known response"));
    QCOMPARE(render.value("sampling").toObject().value("paintedPerSeries").toArray().at(0).toInt(), 11);
    QCOMPARE(render.value("width").toInt(), 800);
    QVERIFY(notes.replaceAnnotations({QVariantMap{{"id", "bad"}, {"kind", 1}, {"x", 1.0}, {"xUnit", "ms"}}}));
    QVERIFY(plot.problem().contains(QStringLiteral("bad")));
}

void ScientificFigureTest::composedPanels()
{
    ChartAxis x, y; axes(x, y);
    ScientificDataset a; squares(a);
    ScientificPlotRenderer left, right;
    style(left, x, y); style(right, x, y);
    left.appendSeries(&a); right.appendSeries(&a);
    right.setMode(ScientificPlotRenderer::Band);
    ScientificFigureComposer figure;
    figure.appendPanel(&left); figure.appendPanel(&right);
    figure.setProperty("title", QStringLiteral("Figure 1"));
    figure.setProperty("backgroundColor", QColor(Qt::white));
    figure.setProperty("textColor", QColor(Qt::black));
    figure.setProperty("metrics", QVariantMap{{"gap", 8}, {"fontSize", 12}, {"fontFamily", "Sans Serif"}});
    QCOMPARE(figure.panelRect(0, QSize(1000, 500)), QRectF(8, 40, 488, 452));
    QCOMPARE(figure.panelRect(1, QSize(1000, 500)), QRectF(504, 40, 488, 452));
    const QJsonObject meta = QJsonDocument::fromJson(figure.metadataJson(1000, 500).toUtf8()).object();
    const QJsonArray panels = meta.value("panels").toArray();
    QCOMPARE(panels.size(), 2);
    QCOMPARE(panels.at(1).toObject().value("label").toString(), QStringLiteral("b"));
    QCOMPARE(panels.at(1).toObject().value("figure").toObject().value("render").toObject().value("mode").toString(),
             QStringLiteral("band"));
    QCOMPARE(panels.at(0).toObject().value("figure").toObject().value("render").toObject().value("width").toInt(), 488);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QVERIFY(figure.exportPng(dir.filePath("composite.png"), QSize(1000, 500)));
    QVERIFY(figure.exportSvg(dir.filePath("composite.svg"), QSize(1000, 500)));
    const QImage image(dir.filePath("composite.png"));
    QCOMPARE(image.size(), QSize(1000, 500));
    QVERIFY(image.text("QindaTK.ScientificComposite").contains(QStringLiteral("scientificComposite.v1")));
}

QTEST_MAIN(ScientificFigureTest)
#include "tst_scientific_figure.moc"
