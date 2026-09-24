// SPDX-License-Identifier: LGPL-3.0-or-later
#include "trace_index.h"
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;

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
};

QTEST_GUILESS_MAIN(TstTraceIndex)
#include "tst_trace_index.moc"
