// SPDX-License-Identifier: LGPL-3.0-or-later
#include "event_log_model.h"

#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;

class TstEventLogModel final : public QObject {
    Q_OBJECT
private slots:
    void batchedBurstIsBoundedAndEvictsSelection()
    {
        EventLogModel model;
        model.setMaxEntries(12000);
        QVariantList burst;
        burst.reserve(100000);
        for (int i = 0; i < 100000; ++i)
            burst.push_back(QVariantMap{{"eventId", QStringLiteral("evt-%1").arg(i)},
                {"timestamp", QStringLiteral("2026-09-23T12:00:00.000Z")},
                {"severity", i % 2 ? "info" : "warning"},
                {"source", QStringLiteral("worker-%1").arg(i % 8)},
                {"message", QStringLiteral("event %1").arg(i)}});
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy appended(&model, &EventLogModel::eventsAppended);
        QElapsedTimer timer; timer.start();
        QCOMPARE(model.appendEvents(burst), 12000);
        qInfo() << "100k event burst at 12k retention ms:" << timer.elapsed();
        QCOMPARE(model.retainedCount(), 12000);
        QCOMPARE(model.count(), 12000);
        QCOMPARE(inserted.size(), 1); // one batched insertion, not 100k row notifications
        QCOMPARE(appended.size(), 1);
        QCOMPARE(model.eventAt(0).value("eventId").toString(), QStringLiteral("evt-88000"));
        QVERIFY(model.selectEvent(QStringLiteral("evt-99995")));
        model.setMaxEntries(4);
        QCOMPARE(model.retainedCount(), 4);
        QVERIFY(model.selectedEventId().isEmpty());
        QCOMPARE(model.eventAt(3).value("eventId").toString(), QStringLiteral("evt-99999"));
    }

    void oversizedOrMalformedRowsAreRejectedWhole()
    {
        EventLogModel model;
        QSignalSpy rejected(&model, &EventLogModel::eventsRejected);
        const QVariantList rows{
            QVariantMap{{"eventId", "valid"}, {"timestamp", "2026-09-23T09:00:00Z"},
                        {"message", "kept"}},
            QVariantMap{{"eventId", "large"}, {"message", QString(9000, QLatin1Char('x'))}},
            QVariantMap{{"eventId", "bad-time"}, {"timestamp", "yesterday-ish"}, {"message", "no"}}
        };
        QCOMPARE(model.appendEvents(rows), 1);
        QCOMPARE(model.retainedCount(), 1);
        QCOMPARE(model.lastRejectedCount(), 2);
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(model.eventAt(0).value("eventId").toString(), QStringLiteral("valid"));
    }

    void filtersKeepStableIdsAndSelection()
    {
        EventLogModel model;
        model.setMaxEntries(4);
        model.appendEvents({
            QVariantMap{{"eventId", "a"}, {"timestamp", "2026-09-23T10:00:00+02:00"},
                        {"severity", "info"}, {"source", "Audio.Engine"}, {"message", "Ready"}},
            QVariantMap{{"eventId", "b"}, {"timestamp", "2026-09-23T09:01:00Z"},
                        {"severity", "error"}, {"source", "Audio.Engine"}, {"message", "Buffer failed"},
                        {"details", "underrun"}},
            QVariantMap{{"eventId", "c"}, {"timestamp", "2026-09-23T09:02:00Z"},
                        {"severity", "warning"}, {"source", "UI"}, {"message", "Retry"}}
        });
        QVERIFY(model.selectEvent("b"));
        model.setSeverityFilter("error");
        model.setSourceFilter("audio");
        model.setTextFilter("underrun");
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.data(model.index(0), EventLogModel::EventIdRole).toString(), QStringLiteral("b"));
        QCOMPARE(model.selectedEventId(), QStringLiteral("b"));
        QCOMPARE(model.data(model.index(0), EventLogModel::TimestampRole).toString(),
                 QStringLiteral("2026-09-23T09:01:00.000Z"));
        model.setFromUtc(QDateTime::fromString("2026-09-23T09:01:00Z", Qt::ISODate));
        model.setToUtc(QDateTime::fromString("2026-09-23T09:01:00Z", Qt::ISODate));
        QCOMPARE(model.count(), 1);
        model.setTextFilter("missing");
        QCOMPARE(model.count(), 0);
        QCOMPARE(model.selectedEventId(), QStringLiteral("b"));
        model.setTextFilter({}); model.setSeverityFilter({}); model.setSourceFilter({});
        model.setFromUtc({}); model.setToUtc({});
        QCOMPARE(model.indexOfEventId("b"), 1);
    }

    void exportQuotesCsvFieldsAndSupportsSelection()
    {
        EventLogModel model;
        model.appendEvents({
            QVariantMap{{"eventId", "csv-1"}, {"timestamp", "2026-09-23T09:00:00Z"},
                        {"severity", "error"}, {"source", "plug,in"},
                        {"message", "said \"stop\""}, {"details", "line one\nline two"}},
            QVariantMap{{"eventId", "csv-2"}, {"severity", "info"},
                        {"source", "engine"}, {"message", "fine"}}
        });
        const QString csv = model.exportCsv();
        QVERIFY(csv.startsWith("eventId,timestamp,severity,source,message,details\r\n"));
        QVERIFY(csv.contains("\"plug,in\",\"said \"\"stop\"\"\",\"line one\nline two\"\r\n"));
        const QString selected = model.exportCsv({"csv-2"});
        QVERIFY(selected.contains("csv-2"));
        QVERIFY(!selected.contains("csv-1"));
        QVERIFY(selected.endsWith("\r\n"));
    }

    void stagingByteBudgetKeepsOnlyNewestCompleteRows()
    {
        EventLogModel model;
        model.setMaxEntries(4000);
        const QString payload(8192, QLatin1Char('p'));
        QVariantList rows;
        rows.reserve(3000);
        for (int i = 0; i < 3000; ++i)
            rows.push_back(QVariantMap{{"eventId", QStringLiteral("large-%1").arg(i)},
                {"timestamp", "2026-09-23T09:00:00Z"}, {"message", payload}});
        const int retained = model.appendEvents(rows);
        QVERIFY(retained > 0 && retained < rows.size());
        QCOMPARE(model.retainedCount(), retained);
        QCOMPARE(model.lastRejectedCount(), 0);
        QCOMPARE(model.eventAt(model.count() - 1).value("eventId").toString(),
                 QStringLiteral("large-2999"));
    }
};

QTEST_MAIN(TstEventLogModel)
#include "tst_event_log_model.moc"
