// SPDX-License-Identifier: LGPL-3.0-or-later
#include "trace_index.h"
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using namespace QindaTK;

namespace {
QVariantMap loadFixture()
{
    QFile file(QString::fromUtf8(QINDATK_TRACE_FIXTURE));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

QVariantMap traceEvent(int i, const QString &domain = QStringLiteral("mono"))
{
    return {{QStringLiteral("id"), QString::number(i)},
            {QStringLiteral("kind"), QStringLiteral("event")},
            {QStringLiteral("name"), QStringLiteral("Tick")},
            {QStringLiteral("threadId"), QStringLiteral("ui")},
            {QStringLiteral("clockDomain"), domain},
            {QStringLiteral("startTicks"), QString::number(i)}};
}

// Chrome Trace Event JSON: metadata, complete (X), begin/end, instant,
// an unsupported counter and a begin with no end.
const char ChromeTrace[] = R"({"traceEvents":[
 {"ph":"M","name":"thread_name","pid":1,"tid":7,"args":{"name":"Main"}},
 {"ph":"X","name":"Frame","cat":"render","pid":1,"tid":7,"ts":100.5,"dur":50},
 {"ph":"B","name":"Layout","cat":"render","pid":1,"tid":7,"ts":110},
 {"ph":"i","name":"Mark","pid":1,"tid":7,"ts":115.25,"s":"t"},
 {"ph":"E","pid":1,"tid":7,"ts":120},
 {"ph":"X","name":"Upload","cat":"gpu","pid":1,"tid":9,"ts":112,"dur":3.001},
 {"ph":"C","name":"Counter","pid":1,"tid":7,"ts":130,"args":{"v":1}},
 {"ph":"B","name":"Never ends","pid":1,"tid":9,"ts":140}
]})";
}

class TstTraceIndex final : public QObject {
    Q_OBJECT
private slots:
    void exactCrossThreadQueriesAndLatestSnapshot()
    {
        QFile file(QString::fromUtf8(QINDATK_TRACE_FIXTURE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto fixture = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        TraceIndex trace;
        QVERIFY(trace.setClockDomains(fixture.value(QStringLiteral("clockDomains")).toList()));
        QSignalSpy ready(&trace, &TraceIndex::analysisReady);
        QVERIFY(!trace.ingestAsync(fixture.value(QStringLiteral("records")).toList()).isEmpty());
        QVERIFY(ready.wait(5000));
        QCOMPARE(trace.count(), 4);
        QCOMPARE(trace.startNs(), QStringLiteral("100000"));
        QCOMPARE(trace.endNs(), QStringLiteral("500000"));
        QCOMPARE(trace.record(QStringLiteral("worker")).value(QStringLiteral("startNs")),
                 QStringLiteral("300000"));
        QCOMPARE(trace.record(QStringLiteral("upload")).value(QStringLiteral("endNs")),
                 QStringLiteral("350000"));
        QCOMPARE(trace.childrenOf(QStringLiteral("worker")).size(), 1);
        QCOMPARE(trace.ancestorsOf(QStringLiteral("upload")),
                 (QStringList{QStringLiteral("worker"), QStringLiteral("root")}));
        const auto gpuWindow = trace.queryWindow(0.5, 0.8, QStringLiteral("gpu"));
        QCOMPARE(gpuWindow.size(), 2);
        QCOMPARE(gpuWindow.first().toMap().value(QStringLiteral("id")),
                 QStringLiteral("worker"));
        const auto exact = trace.queryRangeNs(QStringLiteral("350000"),
                                               QStringLiteral("350000"), QStringLiteral("gpu"));
        QCOMPARE(exact.size(), 2);
        QCOMPARE(exact.last().toMap().value(QStringLiteral("id")), QStringLiteral("upload"));
        const auto exported = QJsonDocument::fromJson(trace.exportJson().toUtf8()).object();
        QCOMPARE(exported.value(QStringLiteral("records")).toArray().size(), 4);

        // A newer snapshot wins even if an earlier worker completes afterward.
        QVariantList large;
        for (int i = 0; i < 10000; ++i) {
            large.append(QVariantMap{{QStringLiteral("id"), QString::number(i)},
                {QStringLiteral("kind"), QStringLiteral("event")},
                {QStringLiteral("name"), QStringLiteral("Transient")},
                {QStringLiteral("threadId"), QStringLiteral("ui")},
                {QStringLiteral("clockDomain"), QStringLiteral("mono")},
                {QStringLiteral("startTicks"), QString::number(i)}});
        }
        trace.ingestAsync(large);
        trace.ingestAsync(fixture.value(QStringLiteral("records")).toList());
        QTRY_VERIFY_WITH_TIMEOUT(!trace.busy(), 5000);
        QCOMPARE(trace.count(), 4);
        QVERIFY(trace.record(QStringLiteral("1000")).isEmpty());
    }

    void truncationKeepsNewestAndCountsRejected()
    {
        TraceIndex trace;
        QVERIFY(trace.setClockDomains(loadFixture().value(QStringLiteral("clockDomains")).toList()));
        QVariantList rows;
        for (int i = 0; i < 20005; ++i) rows.append(traceEvent(i));
        rows.append(traceEvent(1));                               // duplicate ID
        rows.append(traceEvent(30000, QStringLiteral("nowhere")));  // unknown clock domain
        QVariantMap backwards = traceEvent(30001);
        backwards.insert(QStringLiteral("kind"), QStringLiteral("span"));
        backwards.insert(QStringLiteral("endTicks"), QStringLiteral("30000"));
        rows.append(backwards);                              // ends before it starts
        QSignalSpy ready(&trace, &TraceIndex::analysisReady);
        trace.ingestAsync(rows);
        QVERIFY(ready.wait(10000));
        QCOMPARE(trace.count(), 20000);
        QCOMPARE(trace.truncatedCount(), 5);
        QCOMPARE(trace.rejectedCount(), 3);
        QVERIFY(trace.record(QStringLiteral("4")).isEmpty());
        QCOMPARE(trace.startNs(), QStringLiteral("5000"));
        QCOMPARE(trace.endNs(), QStringLiteral("20004000"));
        QCOMPARE(trace.queryRangeNs(QStringLiteral("0"), QStringLiteral("20004000"),
                                    {}, {}, 5000).size(), 1000);  // per-call cap

        QVariantList tooMany;
        for (int i = 0; i < 50001; ++i) tooMany.append(traceEvent(i));
        ready.clear();
        trace.ingestAsync(tooMany);
        QVERIFY(ready.wait(10000));
        // Refused, and the 20,000-record snapshot stays (D-165).
        QCOMPARE(trace.count(), 20000);
        QCOMPARE(trace.truncatedCount(), 5);
        QCOMPARE(trace.lastError(), QStringLiteral("Trace input exceeds 50,000 records"));
    }

    void clockConversionIsCheckedAndSigned()
    {
        TraceIndex trace;
        QVERIFY(!trace.setClockDomains({QVariantMap{{QStringLiteral("id"), QStringLiteral("x")},
            {QStringLiteral("nanosPerTick"), QStringLiteral("1.5")},
            {QStringLiteral("offsetNs"), QStringLiteral("0")}}}));
        QVERIFY(!trace.setClockDomains({QVariantMap{{QStringLiteral("id"), QStringLiteral("x")},
            {QStringLiteral("nanosPerTick"), QStringLiteral("0")},
            {QStringLiteral("offsetNs"), QStringLiteral("0")}}}));
        QVERIFY(trace.setClockDomains({
            QVariantMap{{QStringLiteral("id"), QStringLiteral("audio")},
                        {QStringLiteral("nanosPerTick"), QStringLiteral("20833")},
                        {QStringLiteral("offsetNs"), QStringLiteral("-500")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("wall")},
                        {QStringLiteral("nanosPerTick"), QStringLiteral("1000000000")},
                        {QStringLiteral("offsetNs"), QStringLiteral("0")}}}));
        QVariantMap overflow = traceEvent(2, QStringLiteral("wall"));
        overflow.insert(QStringLiteral("startTicks"), QStringLiteral("9223372037"));
        QSignalSpy ready(&trace, &TraceIndex::analysisReady);
        trace.ingestAsync({traceEvent(0, QStringLiteral("audio")), traceEvent(48, QStringLiteral("audio")),
                           overflow});
        QVERIFY(ready.wait(5000));
        QCOMPARE(trace.count(), 2);
        QCOMPARE(trace.rejectedCount(), 1);
        QCOMPARE(trace.record(QStringLiteral("0")).value(QStringLiteral("startNs")).toString(),
                 QStringLiteral("-500"));
        QCOMPARE(trace.record(QStringLiteral("48")).value(QStringLiteral("startNs")).toString(),
                 QStringLiteral("999484"));  // 48 × 20833 − 500
    }

    void cancelStopsAnalysisAndKeepsSnapshot()
    {
        const QVariantMap fixture = loadFixture();
        TraceIndex trace;
        QVERIFY(trace.setClockDomains(fixture.value(QStringLiteral("clockDomains")).toList()));
        QSignalSpy ready(&trace, &TraceIndex::analysisReady);
        trace.ingestAsync(fixture.value(QStringLiteral("records")).toList());
        QVERIFY(ready.wait(5000));
        QVariantList large;
        for (int i = 0; i < 50000; ++i) large.append(traceEvent(i));
        QSignalSpy discarded(&trace, &TraceIndex::analysisDiscarded);
        const QString request = trace.ingestAsync(large);
        QVERIFY(trace.busy());
        trace.cancel();
        QVERIFY(!trace.busy());
        QVERIFY(discarded.wait(10000));
        QCOMPARE(discarded.first().at(0).toString(), request);
        QCOMPARE(ready.size(), 1);
        QCOMPARE(trace.count(), 4);
        QCOMPARE(trace.record(QStringLiteral("upload")).value(QStringLiteral("startNs")).toString(),
                 QStringLiteral("350000"));
    }

    void exportRoundTripsThroughImport()
    {
        const QVariantMap fixture = loadFixture();
        TraceIndex source;
        QVERIFY(source.setClockDomains(fixture.value(QStringLiteral("clockDomains")).toList()));
        QSignalSpy sourceReady(&source, &TraceIndex::analysisReady);
        source.ingestAsync(fixture.value(QStringLiteral("records")).toList());
        QVERIFY(sourceReady.wait(5000));
        const QString exported = source.exportJson();

        TraceIndex copy;  // no clock domains: they come from the export
        QSignalSpy ready(&copy, &TraceIndex::analysisReady);
        QVERIFY(!copy.ingestJsonAsync(exported).isEmpty());
        QVERIFY(ready.wait(5000));
        QCOMPARE(copy.count(), 4);
        QCOMPARE(copy.record(QStringLiteral("worker")).value(QStringLiteral("durationNs")).toString(),
                 QStringLiteral("100000"));
        QCOMPARE(copy.ancestorsOf(QStringLiteral("upload")),
                 (QStringList{QStringLiteral("worker"), QStringLiteral("root")}));
        QCOMPARE(copy.exportJson(), exported);

        ready.clear();
        copy.ingestJsonAsync(QStringLiteral("{\"records\": ["));
        QVERIFY(ready.wait(5000));
        QVERIFY(copy.lastError().startsWith(QStringLiteral("Trace JSON is invalid")));
        QCOMPARE(copy.count(), 4);  // a failed import keeps the loaded trace
        QCOMPARE(copy.exportJson(), exported);
        ready.clear();
        copy.ingestFileAsync(QStringLiteral("/nonexistent/trace.json"));
        QVERIFY(ready.wait(5000));
        QCOMPARE(copy.lastError(), QStringLiteral("Cannot read trace file"));
        QCOMPARE(copy.count(), 4);
        // The next good import clears the error.
        ready.clear();
        copy.ingestJsonAsync(exported);
        QVERIFY(ready.wait(5000));
        QVERIFY(copy.lastError().isEmpty());
    }

    void chromeTraceFileImportsWithNesting()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("chrome.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(ChromeTrace);
        file.close();
        TraceIndex trace;
        QSignalSpy ready(&trace, &TraceIndex::analysisReady);
        QVERIFY(!trace.ingestFileAsync(QUrl::fromLocalFile(path).toString()).isEmpty());
        QVERIFY(ready.wait(5000));
        QVERIFY(trace.lastError().isEmpty());
        QCOMPARE(trace.count(), 4);
        QCOMPARE(trace.rejectedCount(), 2);  // the counter and the unmatched begin
        QCOMPARE(trace.startNs(), QStringLiteral("100500"));
        QCOMPARE(trace.endNs(), QStringLiteral("150500"));
        QCOMPARE(trace.threads(), (QStringList{QStringLiteral("1:9"), QStringLiteral("Main (1:7)")}));
        const QVariantMap layout = trace.record(QStringLiteral("e3"));
        QCOMPARE(layout.value(QStringLiteral("name")).toString(), QStringLiteral("Layout"));
        QCOMPARE(layout.value(QStringLiteral("endNs")).toString(), QStringLiteral("120000"));
        QCOMPARE(layout.value(QStringLiteral("parentId")).toString(), QStringLiteral("e2"));
        QCOMPARE(layout.value(QStringLiteral("clockDomain")).toString(), QStringLiteral("chrome"));
        QCOMPARE(trace.ancestorsOf(QStringLiteral("e4")),
                 (QStringList{QStringLiteral("e3"), QStringLiteral("e2")}));
        const QVariantMap upload = trace.record(QStringLiteral("e6"));
        QCOMPARE(upload.value(QStringLiteral("durationNs")).toString(), QStringLiteral("3001"));
        QVERIFY(upload.value(QStringLiteral("parentId")).toString().isEmpty());
        QCOMPARE(trace.childrenOf(QStringLiteral("e2")).size(), 1);
    }

    void chromeLimitsAreReportedAsEnforced()
    {
        TraceIndex trace;
        QSignalSpy ready(&trace, &TraceIndex::analysisReady);
        trace.ingestJsonAsync(QString::fromUtf8(ChromeTrace));
        QVERIFY(ready.wait(5000));
        QCOMPARE(trace.count(), 4);
        auto events = [](int count, const char *pattern) {
            QByteArray json("[");
            for (int i = 0; i < count; ++i) {
                if (i) json += ',';
                json += QByteArray(pattern).replace("%1", QByteArray::number(i));
            }
            return QString::fromUtf8(json + ']');
        };
        // 50,001 complete events are 50,001 records: over the record limit.
        ready.clear();
        trace.ingestJsonAsync(events(50001, R"({"ph":"X","name":"n","pid":1,"tid":1,"ts":%1,"dur":0})"));
        QVERIFY(ready.wait(20000));
        QCOMPARE(trace.lastError(), QStringLiteral("Trace input exceeds 50,000 records"));
        QCOMPARE(trace.count(), 4);
        // 200,001 raw events: over the raw event limit, with its own message.
        ready.clear();
        trace.ingestJsonAsync(events(200001, R"({"ph":"i","ts":%1})"));
        QVERIFY(ready.wait(20000));
        QCOMPARE(trace.lastError(), QStringLiteral("Chrome trace has more than 200,000 events"));
        QCOMPARE(trace.count(), 4);
        // 60,000 raw events that pair into 30,000 records are accepted.
        ready.clear();
        trace.ingestJsonAsync(events(30000, R"({"ph":"B","name":"n","pid":1,"tid":1,"ts":%1},)"
                                            R"({"ph":"E","pid":1,"tid":1,"ts":%1.5})"));
        QVERIFY(ready.wait(20000));
        QVERIFY(trace.lastError().isEmpty());
        QCOMPARE(trace.count(), 20000);
        QCOMPARE(trace.truncatedCount(), 10000);
    }

    void layoutAggregatesWithoutDroppingRecords()
    {
        const QVariantMap fixture = loadFixture();
        TraceIndex trace;
        QVERIFY(trace.setClockDomains(fixture.value(QStringLiteral("clockDomains")).toList()));
        QSignalSpy ready(&trace, &TraceIndex::analysisReady);
        trace.ingestAsync(fixture.value(QStringLiteral("records")).toList());
        QVERIFY(ready.wait(5000));
        // Same-thread children sit one lane below their parent.
        QCOMPARE(trace.tracks(), (QVariantList{
            QVariantMap{{QStringLiteral("threadId"), QStringLiteral("gpu")},
                        {QStringLiteral("firstRow"), 0}, {QStringLiteral("lanes"), 2}},
            QVariantMap{{QStringLiteral("threadId"), QStringLiteral("ui")},
                        {QStringLiteral("firstRow"), 2}, {QStringLiteral("lanes"), 2}}}));
        auto tileFor = [](const QVariantMap &layout, const QString &id) {
            for (const QVariant &tile : layout.value(QStringLiteral("tiles")).toList())
                if (tile.toMap().value(QStringLiteral("id")).toString() == id) return tile.toMap();
            return QVariantMap{};
        };
        QVariantMap layout = trace.layoutWindow(0, 1, 400);
        QCOMPARE(layout.value(QStringLiteral("rows")).toInt(), 4);
        QCOMPARE(layout.value(QStringLiteral("tiles")).toList().size(), 4);
        QCOMPARE(tileFor(layout, QStringLiteral("worker")).value(QStringLiteral("row")).toInt(), 0);
        QCOMPARE(tileFor(layout, QStringLiteral("upload")).value(QStringLiteral("row")).toInt(), 1);
        QCOMPARE(tileFor(layout, QStringLiteral("present")).value(QStringLiteral("row")).toInt(), 3);
        QCOMPARE(tileFor(layout, QStringLiteral("worker")).value(QStringLiteral("x0")).toDouble(), 0.5);
        QCOMPARE(tileFor(layout, QStringLiteral("worker")).value(QStringLiteral("x1")).toDouble(), 0.75);
        // A span that starts before the window is clipped at both ends:
        // worker covers 0.5..0.75 of the trace, the window is 0.6..1.0.
        layout = trace.layoutWindow(0.6, 1.0, 400);
        QCOMPARE(tileFor(layout, QStringLiteral("worker")).value(QStringLiteral("x0")).toDouble(), 0.0);
        QVERIFY(qAbs(tileFor(layout, QStringLiteral("worker")).value(QStringLiteral("x1")).toDouble()
                     - 0.375) < 1e-9);
        QCOMPARE(trace.recordAt(0, 1, 400, 2, 10), QStringLiteral("root"));
        QCOMPARE(trace.recordAt(0, 1, 400, 3, 350), QStringLiteral("present"));
        QVERIFY(trace.recordAt(0, 1, 400, 3, 10).isEmpty());

        // A 20,001-record trace: every record is in exactly one mark, a row
        // never holds more marks than pixel columns, and zooming in reaches
        // any single record.
        QVariantList rows{QVariantMap{{QStringLiteral("id"), QStringLiteral("all")},
            {QStringLiteral("kind"), QStringLiteral("span")}, {QStringLiteral("name"), QStringLiteral("All")},
            {QStringLiteral("threadId"), QStringLiteral("ui")}, {QStringLiteral("clockDomain"), QStringLiteral("mono")},
            {QStringLiteral("startTicks"), QStringLiteral("0")}, {QStringLiteral("endTicks"), QStringLiteral("19999")}}};
        for (int i = 0; i < 20000; ++i) rows.append(traceEvent(i));
        rows.removeLast();  // 20,000 records in all, nothing truncated
        ready.clear();
        trace.ingestAsync(rows);
        QVERIFY(ready.wait(10000));
        QCOMPARE(trace.count(), 20000);
        layout = trace.layoutWindow(0, 1, 1000);
        const QVariantList tiles = layout.value(QStringLiteral("tiles")).toList();
        int sum = 0, merged = 0;
        for (const QVariant &tile : tiles) {
            sum += tile.toMap().value(QStringLiteral("count")).toInt();
            if (tile.toMap().value(QStringLiteral("count")).toInt() > 1) ++merged;
        }
        QCOMPARE(layout.value(QStringLiteral("matched")).toInt(), 20000);
        QCOMPARE(sum, 20000);
        QVERIFY(tiles.size() <= 1 + 1000);
        QVERIFY(merged > 0);
        QCOMPARE(tileFor(layout, QStringLiteral("all")).value(QStringLiteral("count")).toInt(), 1);
        const double at = 12345.0 / 19999.0, width = 4.0 / 19999.0;
        layout = trace.layoutWindow(at - width / 2, at + width / 2, 400);
        const QVariantMap single = tileFor(layout, QStringLiteral("12345"));
        QCOMPARE(single.value(QStringLiteral("count")).toInt(), 1);
        const int column = int(single.value(QStringLiteral("x0")).toDouble() * 400);
        QCOMPARE(trace.recordAt(at - width / 2, at + width / 2, 400,
                                single.value(QStringLiteral("row")).toInt(), column),
                 QStringLiteral("12345"));
        QCOMPARE(trace.layoutWindow(0, 1, 1000, {}, QStringLiteral("all"))
                 .value(QStringLiteral("matched")).toInt(), 1);
    }

    void exportRunsOnAWorker()
    {
        const QVariantMap fixture = loadFixture();
        TraceIndex trace;
        QVERIFY(trace.setClockDomains(fixture.value(QStringLiteral("clockDomains")).toList()));
        QSignalSpy ready(&trace, &TraceIndex::analysisReady);
        trace.ingestAsync(fixture.value(QStringLiteral("records")).toList());
        QVERIFY(ready.wait(5000));
        QSignalSpy exported(&trace, &TraceIndex::exportReady);
        const QString request = trace.exportJsonAsync();
        QCOMPARE(exported.size(), 0);  // not serialized on the calling thread
        QVERIFY(exported.wait(5000));
        QCOMPARE(exported.first().at(0).toString(), request);
        QCOMPARE(exported.first().at(1).toString(), trace.exportJson());
    }
};

QTEST_GUILESS_MAIN(TstTraceIndex)
#include "tst_trace_index.moc"
