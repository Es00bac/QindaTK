// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include "graph.h"
#include "meter.h"
#include "theme.h"
#include "theme_ramp.h"

using namespace QindaTK;

// The invariants the drawing code relies on: ring-buffer ordering, ramp
// interpolation and clamping, and the Meter's reading-to-colour mapping.
class TestTelemetry : public QObject {
    Q_OBJECT

private slots:
    void seriesKeepsNewestInOrder();
    void seriesCapacityRetainsTail();
    void seriesRejectsNonFinite();
    void seriesSetValuesTakesTail();
    void rampInterpolatesBetweenStops();
    void rampClampsOutsideRange();
    void rampSortsAndDropsInvalidStops();
    void rampForValueHandlesEmptySpan();
    void themePublishesEveryNamedRamp();
    void networkRampNeverCollapses();
    void graphAutoScaleFollowsPeak();
    void graphAppendRowKeepsSeriesAligned();
    void meterPositionClamps();
};

void TestTelemetry::seriesKeepsNewestInOrder()
{
    GraphSeries series;
    series.setCapacity(4);
    for (int i = 1; i <= 6; ++i) {
        series.append(i);
    }
    QCOMPARE(series.count(), 4);
    // at(0) is the OLDEST retained sample; the first two fell off the end.
    QCOMPARE(series.at(0), 3.0);
    QCOMPARE(series.at(3), 6.0);
    QCOMPARE(series.last(), 6.0);
    QCOMPARE(series.peak(), 6.0);
}

void TestTelemetry::seriesCapacityRetainsTail()
{
    GraphSeries series;
    series.setCapacity(8);
    for (int i = 1; i <= 8; ++i) {
        series.append(i);
    }
    series.setCapacity(3);
    QCOMPARE(series.count(), 3);
    QCOMPARE(series.at(0), 6.0);
    QCOMPARE(series.at(2), 8.0);
    // Growing again keeps what survived rather than resurrecting anything.
    series.setCapacity(6);
    QCOMPARE(series.count(), 3);
    QCOMPARE(series.at(0), 6.0);
}

void TestTelemetry::seriesRejectsNonFinite()
{
    GraphSeries series;
    series.setCapacity(4);
    series.append(std::numeric_limits<double>::quiet_NaN());
    series.append(std::numeric_limits<double>::infinity());
    // A NaN reaching the vertex buffer corrupts the whole strip.
    QCOMPARE(series.at(0), 0.0);
    QCOMPARE(series.at(1), 0.0);
}

void TestTelemetry::seriesSetValuesTakesTail()
{
    GraphSeries series;
    series.setCapacity(3);
    series.setValues({1, 2, 3, 4, 5});
    QCOMPARE(series.count(), 3);
    QCOMPARE(series.at(0), 3.0);
    QCOMPARE(series.at(2), 5.0);
}

void TestTelemetry::rampInterpolatesBetweenStops()
{
    ThemeRamp ramp;
    ramp.setStops({{0.0, QColor(0, 0, 0)}, {1.0, QColor(255, 255, 255)}});
    QCOMPARE(ramp.at(0.0), QColor(0, 0, 0));
    QCOMPARE(ramp.at(1.0), QColor(255, 255, 255));
    const QColor mid = ramp.at(0.5);
    QVERIFY(qAbs(mid.red() - 128) <= 1);
}

void TestTelemetry::rampClampsOutsideRange()
{
    ThemeRamp ramp;
    ramp.setStops({{0.0, QColor(10, 20, 30)}, {1.0, QColor(200, 210, 220)}});
    QCOMPARE(ramp.at(-5.0), QColor(10, 20, 30));
    QCOMPARE(ramp.at(5.0), QColor(200, 210, 220));
    QCOMPARE(ramp.at(qQNaN()), QColor(10, 20, 30));
}

void TestTelemetry::rampSortsAndDropsInvalidStops()
{
    ThemeRamp ramp;
    ramp.setStops({{1.0, QColor(255, 0, 0)}, {0.0, QColor(0, 255, 0)}, {0.5, QColor()}});
    QCOMPARE(ramp.count(), 2);
    // at() walks the stops assuming ascending positions.
    QCOMPARE(ramp.at(0.0), QColor(0, 255, 0));
    QCOMPARE(ramp.at(1.0), QColor(255, 0, 0));
}

void TestTelemetry::rampForValueHandlesEmptySpan()
{
    ThemeRamp ramp;
    ramp.setStops({{0.0, QColor(1, 2, 3)}, {1.0, QColor(9, 9, 9)}});
    QCOMPARE(ramp.forValue(50, 0, 100), ramp.at(0.5));
    // An inverted or zero span must not divide by zero.
    QCOMPARE(ramp.forValue(50, 10, 10), QColor(1, 2, 3));
    QCOMPARE(ramp.forValue(50, 100, 0), QColor(1, 2, 3));
}

void TestTelemetry::themePublishesEveryNamedRamp()
{
    Theme *theme = Theme::instance();
    for (const QString &name : ThemeRamps::names()) {
        ThemeRamp *ramp = theme->ramp()->byName(name);
        QVERIFY2(ramp != nullptr, qPrintable(name));
        QVERIFY2(ramp->count() >= 2, qPrintable(name));
        QVERIFY2(ramp->at(0.5).isValid(), qPrintable(name));
    }
    QVERIFY(theme->ramp()->byName(QStringLiteral("nope")) == nullptr);
}

void TestTelemetry::networkRampNeverCollapses()
{
    // AGENT-GUARD: sloom-dark resolves accent and info to the same colour, so
    // a network ramp naming both ends renders every throughput identically.
    Theme *theme = Theme::instance();
    for (const QString &preset : theme->presets()) {
        QVERIFY(theme->applyPreset(preset));
        ThemeRamp *ramp = theme->ramp()->network();
        QVERIFY2(ramp->at(0.0) != ramp->at(1.0), qPrintable(preset));
    }
    theme->applyPreset(QStringLiteral("sloom-dark"));
}

void TestTelemetry::graphAutoScaleFollowsPeak()
{
    Graph graph;
    graph.setMaxValue(100);
    QCOMPARE(graph.effectiveMax(), 100.0);

    auto *series = new GraphSeries(&graph);
    QQmlListProperty<GraphSeries> list = graph.seriesList();
    list.append(&list, series);
    series->append(40);

    graph.setAutoScale(true);
    graph.setHeadroom(1.5);
    graph.setAutoScaleFloor(1);
    QCOMPARE(graph.effectiveMax(), 60.0);
    // A floor keeps an idle graph from magnifying noise.
    graph.setAutoScaleFloor(500);
    QCOMPARE(graph.effectiveMax(), 500.0);
}

void TestTelemetry::graphAppendRowKeepsSeriesAligned()
{
    Graph graph;
    QQmlListProperty<GraphSeries> list = graph.seriesList();
    auto *rx = new GraphSeries(&graph);
    auto *tx = new GraphSeries(&graph);
    list.append(&list, rx);
    list.append(&list, tx);

    graph.appendRow({10, 20});
    // A short row repeats each series' last value rather than injecting a
    // zero, which would draw a spike that never happened.
    graph.appendRow({30});
    QCOMPARE(rx->count(), 2);
    QCOMPARE(tx->count(), 2);
    QCOMPARE(rx->last(), 30.0);
    QCOMPARE(tx->last(), 20.0);
}

void TestTelemetry::meterPositionClamps()
{
    Meter meter;
    meter.setFrom(0);
    meter.setTo(100);
    meter.setValue(150);
    QCOMPARE(meter.position(), 1.0);
    meter.setValue(-20);
    QCOMPARE(meter.position(), 0.0);
    meter.setTo(0);
    QCOMPARE(meter.position(), 0.0);

    meter.setTo(100);
    meter.setValue(100);
    meter.setRamp(Theme::instance()->ramp()->load());
    QCOMPARE(meter.valueColor(), Theme::instance()->ramp()->load()->at(1.0));
}

QTEST_MAIN(TestTelemetry)
#include "tst_telemetry.moc"
