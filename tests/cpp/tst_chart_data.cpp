// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QTest>
#include <cmath>
#include <limits>

#include "chart_axis.h"
#include "chart_data.h"
#include "chart_histogram.h"
#include "chart_series.h"

using namespace QindaTK;

class TestChartData : public QObject {
    Q_OBJECT
private slots:
    void axesAndExactTime();
    void idsBoundsAndAtomicUpdates();
    void stackGroupsAndGaps();
    void groupedBarsAndWaterfall();
    void scatterBubbleVectorAndHit();
    void decimationAndAccessibleCsv();
    void histogramBoundariesAndNormalization();
};

void TestChartData::axesAndExactTime()
{
    ChartAxis axis;
    axis.setMinimum(-10); axis.setMaximum(10); axis.setUnit(QStringLiteral("m"));
    QCOMPARE(axis.map(0), 0.5);
    QCOMPARE(axis.unmap(0.75), 5.0);
    QCOMPARE(axis.ticks(5).size(), 5);
    axis.setReversed(true);
    QCOMPARE(axis.map(-10), 1.0);

    axis.setKind(ChartAxis::Logarithmic);
    axis.setMinimum(1); axis.setMaximum(100);
    QVERIFY(std::abs(axis.map(10) - 0.5) < 1e-12);
    QCOMPARE(axis.ticks().size(), 3);
    axis.setMinimum(0);
    QVERIFY(!axis.isValid());
    QVERIFY(std::isnan(axis.map(10)));

    axis.setKind(ChartAxis::Category);
    axis.setCategories({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
    QCOMPARE(axis.categoryIndex(QStringLiteral("B")), 1);
    QVERIFY(std::abs(axis.map(1) - 0.5) < 1e-12);
    QCOMPARE(axis.unmap(1), 0.0); // reversed
    axis.setCategories({QStringLiteral("A"), QStringLiteral("A")});
    QVERIFY(!axis.isValid());

    axis.setKind(ChartAxis::DateTime);
    axis.setReversed(false);
    constexpr qint64 start = 9007199254740993LL;
    axis.setUtcDomain(start, start + 1000);
    QVERIFY(axis.isValid());
    QCOMPARE(axis.minimumUtc(), QStringLiteral("9007199254740993"));
    QCOMPARE(axis.mapUtc(start + 1), 0.001);
    QCOMPARE(axis.unmapUtc(0.001), start + 1);
    QVERIFY(std::isnan(axis.map(double(start + 1))));
    axis.setTimeZoneId(QStringLiteral("Invalid/Zone"));
    QVERIFY(!axis.isValid());
}

void TestChartData::idsBoundsAndAtomicUpdates()
{
    ChartSeries series;
    series.setId(QStringLiteral("s"));
    series.setCapacity(3);
    QVERIFY(series.replacePoints({{"a", 0, 1}, {"b", 1, 2}}));
    QString error;
    QVERIFY(!series.replacePoints({{"z", 0, 0}, {"z", 1, 1}}, &error));
    QCOMPARE(series.count(), 2);
    QVERIFY(error.contains(QStringLiteral("Duplicate")));
    QVERIFY(!series.appendPoint({"a", 2, 3}, &error));
    QVERIFY(series.appendPoint({"c", 2, 3}));
    QVERIFY(series.appendPoint({"d", 3, 4}));
    QCOMPARE(series.count(), 3);
    QVERIFY(series.point(QStringLiteral("a")) == nullptr);
    QCOMPARE(series.points().first().id, QStringLiteral("b"));
    QVERIFY(!series.appendPoint({"bad", 0, std::numeric_limits<double>::infinity()}, &error));
    QCOMPARE(series.count(), 3);
    QVERIFY(series.removePoint(QStringLiteral("c")));
    QVERIFY(!series.removePoint(QStringLiteral("c")));
}

void TestChartData::stackGroupsAndGaps()
{
    ChartAxis x, y;
    x.setMinimum(0); x.setMaximum(2);
    y.setMinimum(-10); y.setMaximum(10); y.setUnit(QStringLiteral("kWh"));
    ChartSeries first, second;
    first.setId(QStringLiteral("one")); second.setId(QStringLiteral("two"));
    first.setKind(ChartSeries::StackedArea); second.setKind(ChartSeries::StackedArea);
    first.setStackKey(QStringLiteral("energy")); second.setStackKey(QStringLiteral("energy"));
    first.setYUnit(QStringLiteral("kWh")); second.setYUnit(QStringLiteral("kWh"));
    QVERIFY(first.replacePoints({{"a", 0, 3}, {"gap", 1, 0, 0, {}, 0, 0, 0, true}, {"b", 2, -2}}));
    QVERIFY(second.replacePoints({{"c", 0, 4}, {"d", 2, -5}}));
    const ChartPlot plot = ChartData::project({&first, &second}, x, y);
    QVERIFY2(plot.valid(), qPrintable(plot.error));
    QCOMPARE(plot.series.at(0).points.at(1).gap, true);
    QCOMPARE(plot.series.at(1).points.at(0).baselineY, y.map(3));
    QCOMPARE(plot.series.at(1).points.at(0).y, y.map(7));
    QCOMPARE(plot.series.at(1).points.at(1).baselineY, y.map(-2));
    QCOMPARE(plot.series.at(1).points.at(1).y, y.map(-7));
    second.setYUnit(QStringLiteral("W"));
    QVERIFY(ChartData::project({&first, &second}, x, y).error.contains(QStringLiteral("units")));
    second.setYUnit(QStringLiteral("kWh"));
    second.setBaseline(1);
    QVERIFY(ChartData::project({&first, &second}, x, y).error.contains(QStringLiteral("baselines")));
}

void TestChartData::groupedBarsAndWaterfall()
{
    ChartAxis x, y;
    x.setKind(ChartAxis::Category); x.setCategories({QStringLiteral("Q1"), QStringLiteral("Q2"), QStringLiteral("Q3")});
    y.setMinimum(-10); y.setMaximum(20);
    ChartSeries a, b, fall;
    a.setId(QStringLiteral("a")); b.setId(QStringLiteral("b")); fall.setId(QStringLiteral("fall"));
    a.setKind(ChartSeries::Bar); b.setKind(ChartSeries::Bar);
    fall.setKind(ChartSeries::Waterfall);
    QVERIFY(a.replacePoints({{"a1", 0, 5, 0, "Q1"}}));
    QVERIFY(b.replacePoints({{"b1", 0, 7, 0, "Q1"}}));
    QVERIFY(fall.replacePoints({{"f1", 0, 5, 0, "Q1"}, {"f2", 0, -2, 0, "Q2"},
                                {"total", 0, 8, 0, "Q3", 0, 0, 0, false, true}}));
    const ChartPlot p = ChartData::project({&a, &b, &fall}, x, y);
    QVERIFY2(p.valid(), qPrintable(p.error));
    QVERIFY(p.series.at(0).points.first().x < p.series.at(1).points.first().x);
    QCOMPARE(p.series.at(0).points.first().barWidth, 0.8 / 3 / 2);
    QCOMPARE(p.series.at(2).points.at(1).baselineY, y.map(5));
    QCOMPARE(p.series.at(2).points.at(1).y, y.map(3));
    QCOMPARE(p.series.at(2).points.at(2).baselineY, y.map(0));
    QCOMPARE(p.series.at(2).points.at(2).y, y.map(8));
}

void TestChartData::scatterBubbleVectorAndHit()
{
    ChartAxis x, y;
    x.setMinimum(0); x.setMaximum(10);
    y.setMinimum(0); y.setMaximum(10);
    ChartSeries scatter, bubble, vector;
    scatter.setId(QStringLiteral("scatter")); scatter.setKind(ChartSeries::Scatter);
    bubble.setId(QStringLiteral("bubble")); bubble.setKind(ChartSeries::Bubble);
    vector.setId(QStringLiteral("vector")); vector.setKind(ChartSeries::Vector);
    QVERIFY(scatter.replacePoints({{"p", 2, 3}}));
    QVERIFY(bubble.replacePoints({{"b", 4, 6, 0, {}, 25}}));
    QVERIFY(vector.replacePoints({{"v", 4, 4, 0, {}, 0, 2, -1}}));
    const ChartPlot plot = ChartData::project({&scatter, &bubble, &vector}, x, y);
    QVERIFY2(plot.valid(), qPrintable(plot.error));
    QCOMPARE(plot.series.at(1).points.first().size, 25.0);
    QCOMPARE(plot.series.at(2).points.first().endX, 0.6);
    QCOMPARE(plot.series.at(2).points.first().endY, 0.3);
    ChartHitIndex hit;
    hit.rebuild(plot);
    QCOMPARE(hit.count(), 3);
    QCOMPARE(hit.nearest(0.2, 0.3, 0.02).pointId, QStringLiteral("p"));
    QCOMPARE(hit.find(QStringLiteral("bubble"), QStringLiteral("b"))->size, 25.0);
    QVERIFY(hit.find(QStringLiteral("bubble"), QStringLiteral("p")) == nullptr);
    QVERIFY(hit.nearest(0.9, 0.9, 0.02).pointId.isEmpty());
    y.setKind(ChartAxis::Logarithmic); y.setMinimum(1);
    scatter.setBaseline(0);
    QVERIFY(ChartData::project({&scatter}, x, y).series.first().points.first().gap == false);
}

void TestChartData::decimationAndAccessibleCsv()
{
    QVector<ChartPoint> points;
    for (int i = 0; i < 100; ++i) points.append({QString::number(i), double(i), i == 37 ? -100.0 : i == 68 ? 200.0 : 0.0});
    auto result = ChartData::decimate(points, 4);
    QVERIFY2(result.valid(), qPrintable(result.error));
    QCOMPARE(result.points.size(), 4);
    QCOMPARE(result.points.at(0).id, QStringLiteral("0"));
    QCOMPARE(result.points.at(1).id, QStringLiteral("37"));
    QCOMPARE(result.points.at(2).id, QStringLiteral("68"));
    QCOMPARE(result.points.at(3).id, QStringLiteral("99"));
    points[50].gap = true;
    QVERIFY(!ChartData::decimate(points, 4).valid());

    ChartSeries series;
    series.setId(QStringLiteral("readings")); series.setName(QStringLiteral("Voltage, A"));
    series.setYUnit(QStringLiteral("V"));
    QVERIFY(series.replacePoints({{"stable", 1, 4}, {"missing", 2, 0, 0, {}, 0, 0, 0, true}}));
    ChartAxis x;
    const auto rows = ChartData::table({&series}, x);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(1).pointId, QStringLiteral("missing"));
    QVERIFY(rows.at(1).gap);
    const QByteArray csv = ChartData::csv(rows);
    QVERIFY(csv.contains("\"Voltage, A\""));
    QVERIFY(csv.contains("\"missing\""));
    QVERIFY(csv.contains("\"true\""));
}

void TestChartData::histogramBoundariesAndNormalization()
{
    const QVector<double> edges = {0, 1, 3};
    const QVector<double> samples = {-1, 0, 0.5, 1, 2, 3, 4, std::numeric_limits<double>::quiet_NaN()};
    const auto histogram = ChartHistogram::fromSamples(edges, samples, ChartHistogram::Density,
                                                        QStringLiteral("luma"), QStringLiteral("cd/m2"));
    QVERIFY2(histogram.valid(), qPrintable(histogram.error));
    QCOMPARE(histogram.bins.at(0).count, quint64(2));
    QCOMPARE(histogram.bins.at(1).count, quint64(3));
    QCOMPARE(histogram.underflow, quint64(1));
    QCOMPARE(histogram.overflow, quint64(1));
    QCOMPARE(histogram.missing, quint64(1));
    QCOMPARE(histogram.bins.at(0).displayed, 0.4);
    QCOMPARE(histogram.bins.at(1).displayed, 0.3);
    QCOMPARE(histogram.channel, QStringLiteral("luma"));
    const auto empty = ChartHistogram::fromCounts(edges, {0, 0}, ChartHistogram::Probability);
    QCOMPARE(empty.bins.at(0).displayed, 0.0);
    QVERIFY(!ChartHistogram::fromCounts({0, 0}, {1}, ChartHistogram::Count).valid());
}

QTEST_MAIN(TestChartData)
#include "tst_chart_data.moc"
