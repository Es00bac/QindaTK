// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QSignalSpy>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include "chart_dataset.h"
#include "chart_renderer.h"
#include "chart_annotation_model.h"

using namespace QindaTK;

class TestChartDataset : public QObject {
    Q_OBJECT
private slots:
    void boundedStablePointRows();
    void rejectedUpdatesAreAtomic();
    void exactUtcInput();
    void rasterContainsPlotPixels();
    void axisDestructionIsSafe();
    void linkedAxesShareViewport();
    void annotationIdentityAndCoordinates();
    void histogramSelectionUsesSourceEdges();
};

void TestChartDataset::boundedStablePointRows()
{
    ChartDataset data;
    data.setCapacity(3);
    data.setId(QStringLiteral("telemetry"));
    QSignalSpy inserted(&data, &QAbstractItemModel::rowsInserted);
    QVERIFY(data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("a")},
                              {QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 1.0}}));
    QVERIFY(data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("b")},
                              {QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 2.0}}));
    QVERIFY(data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("c")},
                              {QStringLiteral("x"), 2.0}, {QStringLiteral("y"), 3.0}}));
    QCOMPARE(data.rowCount(), 3);
    QCOMPARE(data.get(1).value(QStringLiteral("pointId")).toString(), QStringLiteral("b"));
    QVERIFY(data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("d")},
                              {QStringLiteral("x"), 3.0}, {QStringLiteral("y"), 4.0}}));
    QCOMPARE(data.rowCount(), 3);
    QCOMPARE(data.get(0).value(QStringLiteral("pointId")).toString(), QStringLiteral("b"));
    QVERIFY(data.removePoint(QStringLiteral("c")));
    QCOMPARE(data.rowCount(), 2);
    QVERIFY(!data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("d")},
                               {QStringLiteral("x"), 4.0}, {QStringLiteral("y"), 5.0}}));
    QVERIFY(inserted.size() >= 4);
}

void TestChartDataset::exactUtcInput()
{
    ChartDataset data;
    const QString exact = QStringLiteral("9007199254740993");
    QVERIFY(data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("t0")},
                              {QStringLiteral("utcMs"), exact}, {QStringLiteral("y"), 7.0}}));
    QCOMPARE(data.get(0).value(QStringLiteral("utcMs")).toString(), exact);
    QVERIFY(!data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("t1")},
                               {QStringLiteral("utcMs"), QVariant::fromValue<qlonglong>(9007199254740994LL)},
                               {QStringLiteral("y"), 8.0}}));
    QCOMPARE(data.rowCount(), 1);
}

void TestChartDataset::rejectedUpdatesAreAtomic()
{
    ChartDataset data;
    data.setCapacity(2);
    QVERIFY(data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("a")},
                              {QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 2.0}}));
    QVERIFY(data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("b")},
                              {QStringLiteral("x"), 2.0}, {QStringLiteral("y"), 3.0}}));
    QSignalSpy inserted(&data, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&data, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(&data, &QAbstractItemModel::modelReset);
    QVERIFY(!data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("a")},
                               {QStringLiteral("x"), 3.0}, {QStringLiteral("y"), 4.0}}));
    QCOMPARE(data.rowCount(), 2);
    QCOMPARE(data.get(0).value(QStringLiteral("pointId")).toString(), QStringLiteral("a"));
    QCOMPARE(data.get(1).value(QStringLiteral("pointId")).toString(), QStringLiteral("b"));
    QCOMPARE(inserted.size(), 0);
    QCOMPARE(removed.size(), 0);
    const QVariantList duplicateRows{
        QVariantMap{{QStringLiteral("pointId"), QStringLiteral("c")}, {QStringLiteral("x"), 3.0}, {QStringLiteral("y"), 4.0}},
        QVariantMap{{QStringLiteral("pointId"), QStringLiteral("c")}, {QStringLiteral("x"), 4.0}, {QStringLiteral("y"), 5.0}}};
    QVERIFY(!data.replacePoints(duplicateRows));
    QCOMPARE(data.rowCount(), 2);
    QCOMPARE(data.get(0).value(QStringLiteral("pointId")).toString(), QStringLiteral("a"));
    QCOMPARE(reset.size(), 0);
    QVERIFY(data.replacePoints({QVariantMap{{QStringLiteral("pointId"), QStringLiteral("c")},
        {QStringLiteral("x"), 3.0}, {QStringLiteral("y"), 4.0}}}));
    QCOMPARE(reset.size(), 1);
}

void TestChartDataset::rasterContainsPlotPixels()
{
    ChartAxis x, y;
    x.setMinimum(0); x.setMaximum(1); y.setMinimum(0); y.setMaximum(10);
    ChartDataset data;
    data.setId(QStringLiteral("raster"));
    QVERIFY(data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("a")},
                              {QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 2.0}}));
    QVERIFY(data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("b")},
                              {QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 8.0}}));
    QVERIFY(data.appendPoint({{QStringLiteral("pointId"), QStringLiteral("gap")},
                              {QStringLiteral("x"), 0.8}, {QStringLiteral("gap"), true}}));
    ChartRenderer renderer;
    renderer.setXAxis(&x); renderer.setYAxis(&y);
    renderer.setBackgroundColor(QColor(QStringLiteral("#101010")));
    renderer.setGridColor(QColor(QStringLiteral("#303030")));
    renderer.setAxisColor(QColor(QStringLiteral("#ffffff")));
    renderer.setDefaultSeriesColor(QColor(QStringLiteral("#ff0000")));
    renderer.setMetrics({{QStringLiteral("leftMargin"), 48}, {QStringLiteral("rightMargin"), 8},
        {QStringLiteral("topMargin"), 8}, {QStringLiteral("bottomMargin"), 24},
        {QStringLiteral("fontFamily"), QStringLiteral("Sans Serif")}, {QStringLiteral("fontPixelSize"), 10},
        {QStringLiteral("axisLabelHeight"), 14}, {QStringLiteral("tickHalfWidth"), 60},
        {QStringLiteral("smallGap"), 4}, {QStringLiteral("gridWidth"), 1},
        {QStringLiteral("lineWidth"), 2}, {QStringLiteral("pointRadius"), 4},
        {QStringLiteral("bubbleMinRadius"), 4}, {QStringLiteral("bubbleMaxRadius"), 16},
        {QStringLiteral("arrowSize"), 6}, {QStringLiteral("minimumBarWidth"), 1},
        {QStringLiteral("selectionWidth"), 2}, {QStringLiteral("selectionRadius"), 7},
        {QStringLiteral("hitRadius"), 10}});
    auto list = renderer.datasets();
    list.append(&list, &data);
    const QImage image = renderer.renderImage(QSize(240, 140));
    QCOMPARE(image.size(), QSize(240, 140));
    int nonBackground = 0;
    for (int row = 0; row < image.height(); ++row)
        for (int col = 0; col < image.width(); ++col)
            if (image.pixelColor(col, row) != QColor(QStringLiteral("#101010"))) ++nonBackground;
    QVERIFY(nonBackground > 500);
    bool keptEarlierSegment = false;
    for (int row = 76; row <= 80; ++row)
        for (int col = 92; col <= 96; ++col)
            if (image.pixelColor(col, row).red() > 220 && image.pixelColor(col, row).green() < 80)
                keptEarlierSegment = true;
    QVERIFY(keptEarlierSegment);

    QTemporaryDir output;
    QVERIFY(output.isValid());
    const QString pngPath = output.filePath(QStringLiteral("chart.png"));
    const QString svgPath = output.filePath(QStringLiteral("chart.svg"));
    QVERIFY(renderer.exportPng(pngPath, QSize(240, 140)));
    const QImage exportedPng(pngPath);
    QCOMPARE(exportedPng.size(), QSize(240, 140));
    int exportedPixels = 0;
    for (int row = 0; row < exportedPng.height(); ++row)
        for (int col = 0; col < exportedPng.width(); ++col)
            if (exportedPng.pixelColor(col, row) != QColor(QStringLiteral("#101010"))) ++exportedPixels;
    QVERIFY(exportedPixels > 500);
    QVERIFY(renderer.exportSvg(svgPath, QSize(240, 140)));
    QFile svg(svgPath);
    QVERIFY(svg.open(QIODevice::ReadOnly));
    const QByteArray vectorMarkup = svg.readAll();
    QVERIFY(vectorMarkup.contains("<svg"));
    QVERIFY(vectorMarkup.contains("<path") || vectorMarkup.contains("<line")
            || vectorMarkup.contains("<ellipse"));
    QVERIFY(!renderer.exportPng(output.filePath(QStringLiteral("invalid.png")), QSize(0, 140)));
}

void TestChartDataset::axisDestructionIsSafe()
{
    ChartRenderer renderer;
    auto *axis = new ChartAxis;
    renderer.setXAxis(axis);
    renderer.setYAxis(axis);
    delete axis;
    QCOMPARE(renderer.xAxis(), nullptr);
    QCOMPARE(renderer.yAxis(), nullptr);
    const QImage image = renderer.renderImage(QSize(100, 60));
    QCOMPARE(image.size(), QSize(100, 60));
}

void TestChartDataset::linkedAxesShareViewport()
{
    ChartAxis sharedX, leftY, rightY;
    sharedX.setMinimum(0); sharedX.setMaximum(100); sharedX.setUnit(QStringLiteral("s"));
    ChartRenderer left, right;
    left.setXAxis(&sharedX); left.setYAxis(&leftY);
    right.setXAxis(&sharedX); right.setYAxis(&rightY);
    QSignalSpy rightViewport(&right, &ChartRenderer::viewportChanged);
    left.setViewport(0.2, 0.8, 0.1, 0.9);
    QCOMPARE(sharedX.viewportStart(), 0.2);
    QCOMPARE(sharedX.viewportEnd(), 0.8);
    QCOMPARE(right.viewX(), 0.6);
    QCOMPARE(right.viewY(), 1.0); // Only the shared axis follows.
    QVERIFY(rightViewport.size() >= 1);
    right.pan(0.25, 0);
    QCOMPARE(left.viewX(), 0.6);
    QVERIFY(std::abs(sharedX.viewportStart() - 0.05) < 1e-12);
    QVERIFY(!sharedX.setViewport(-0.1, 0.5));
    QVERIFY(std::abs(sharedX.viewportStart() - 0.05) < 1e-12);
    right.setViewport(sharedX.viewportStart(), sharedX.viewportEnd(), 0.2, 0.8);
    sharedX.setMaximum(200);
    QCOMPARE(left.viewX(), 1.0);
    QCOMPARE(right.viewX(), 1.0);
    QVERIFY(std::abs(right.viewY() - 0.6) < 1e-12);
    QVERIFY(std::abs(left.viewY() - 0.8) < 1e-12);
}

void TestChartDataset::annotationIdentityAndCoordinates()
{
    ChartAnnotationModel annotations;
    const QVariantMap point{{QStringLiteral("id"), QStringLiteral("p-1")},
        {QStringLiteral("label"), QStringLiteral("Peak, measured")},
        {QStringLiteral("kind"), int(ChartAnnotationModel::Point)},
        {QStringLiteral("x"), 5.0}, {QStringLiteral("y"), 5.0},
        {QStringLiteral("xUnit"), QStringLiteral("s")},
        {QStringLiteral("yUnit"), QStringLiteral("m")}};
    QVERIFY(annotations.replaceAnnotations({point}));
    QSignalSpy reset(&annotations, &QAbstractItemModel::modelReset);
    QVERIFY(!annotations.replaceAnnotations({point, point}));
    QCOMPARE(annotations.count(), 1);
    QCOMPARE(reset.size(), 0);
    ChartAxis x, y;
    x.setMinimum(0); x.setMaximum(10); x.setUnit(QStringLiteral("s"));
    y.setMinimum(0); y.setMaximum(10); y.setUnit(QStringLiteral("m"));
    ChartRenderer renderer;
    renderer.setWidth(300); renderer.setHeight(200);
    renderer.setXAxis(&x); renderer.setYAxis(&y);
    renderer.setAnnotationModel(&annotations);
    renderer.setMetrics({{QStringLiteral("hitRadius"), 8},
                         {QStringLiteral("pointRadius"), 4},
                         {QStringLiteral("lineWidth"), 2}});
    QCOMPARE(renderer.annotationAt(150, 100).value(QStringLiteral("id")).toString(),
             QStringLiteral("p-1"));
    QVERIFY(renderer.selectAnnotationAt(150, 100));
    QCOMPARE(renderer.selectedAnnotationId(), QStringLiteral("p-1"));
    QVERIFY(renderer.annotationsCsv().contains(QStringLiteral("\"Peak, measured\"")));
    QVERIFY(renderer.annotationsCsv().contains(QStringLiteral("\"s\",\"m\"")));
    x.setUnit(QStringLiteral("ms"));
    QVERIFY(renderer.annotationError().contains(QStringLiteral("X")));
    QVERIFY(renderer.annotationAt(150, 100).isEmpty());
    const QVariantMap exactTime{{QStringLiteral("id"), QStringLiteral("utc-1")},
        {QStringLiteral("kind"), int(ChartAnnotationModel::Point)},
        {QStringLiteral("utcMs"), QStringLiteral("9007199254741493")},
        {QStringLiteral("y"), 5.0}, {QStringLiteral("xUnit"), QStringLiteral("UTC ms")},
        {QStringLiteral("yUnit"), QStringLiteral("m")}};
    QVERIFY(annotations.replaceAnnotations({exactTime}));
    x.setKind(ChartAxis::DateTime);
    x.setUtcDomain(9007199254740993LL, 9007199254741993LL);
    x.setUnit(QStringLiteral("UTC ms"));
    QVERIFY(renderer.annotationError().isEmpty());
    QCOMPARE(renderer.annotationAt(150, 100).value(QStringLiteral("id")).toString(),
             QStringLiteral("utc-1"));
    QCOMPARE(annotations.get(0).value(QStringLiteral("utcMs")).toString(),
             QStringLiteral("9007199254741493"));
}

void TestChartDataset::histogramSelectionUsesSourceEdges()
{
    ChartAxis x, y;
    x.setMinimum(0); x.setMaximum(4); x.setUnit(QStringLiteral("signal"));
    y.setMinimum(0); y.setMaximum(10);
    ChartRenderer renderer;
    renderer.setXAxis(&x); renderer.setYAxis(&y);
    QVERIFY(renderer.setHistogram({0, 1, 3, 4}, {2, 5, 3},
        ChartHistogram::Count, QStringLiteral("red"), QStringLiteral("signal")));
    QSignalSpy selected(&renderer, &ChartRenderer::histogramRangeSelected);
    QVERIFY(renderer.selectHistogramBins(2, 1));
    auto *range = renderer.histogramSelection();
    QVERIFY(range->valid());
    QCOMPARE(range->firstBin(), 1); QCOMPARE(range->lastBin(), 2);
    QCOMPARE(range->lower(), 1.0); QCOMPARE(range->upper(), 4.0);
    QVERIFY(range->lowerInclusive()); QVERIFY(range->upperInclusive());
    QCOMPARE(range->unit(), QStringLiteral("signal"));
    QCOMPARE(range->channel(), QStringLiteral("red"));
    QCOMPARE(selected.size(), 1);
    QVERIFY(renderer.selectHistogramBins(0, 0));
    QCOMPARE(range->lower(), 0.0); QCOMPARE(range->upper(), 1.0);
    QVERIFY(!range->upperInclusive());
    QVERIFY(!renderer.selectHistogramBins(0, 3));
    QCOMPARE(range->firstBin(), 0);
    QVERIFY(!renderer.setHistogram({0, 1, 1}, {2, 5},
        ChartHistogram::Count, QStringLiteral("red"), QStringLiteral("signal")));
    QVERIFY(!range->valid());
}

QTEST_MAIN(TestChartDataset)
#include "tst_chart_dataset.moc"
