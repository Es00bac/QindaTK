// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QSignalSpy>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include "chart_dataset.h"
#include "chart_renderer.h"

using namespace QindaTK;

class TestChartDataset : public QObject {
    Q_OBJECT
private slots:
    void boundedStablePointRows();
    void rejectedUpdatesAreAtomic();
    void exactUtcInput();
    void rasterContainsPlotPixels();
    void axisDestructionIsSafe();
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

QTEST_MAIN(TestChartDataset)
#include "tst_chart_dataset.moc"
