// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/recurrence_service.h"
#include <QtTest>

using namespace QindaTK;

class RecurrenceServiceTest final : public QObject {
    Q_OBJECT
private slots:
    void localWallTimeSurvivesDstAndExceptions()
    {
        RecurrenceService service;
        QVariantMap rule{{QStringLiteral("frequency"), QStringLiteral("daily")},
                         {QStringLiteral("interval"), 1},
                         {QStringLiteral("endMode"), QStringLiteral("count")},
                         {QStringLiteral("count"), 3}};
        const auto expand = [&] {
            return service.expand(QStringLiteral("event-a"), QStringLiteral("2026-03-07T09:00:00"),
                                  3600, QStringLiteral("America/Denver"), rule,
                                  QStringLiteral("2026-03-07T00:00:00Z"),
                                  QStringLiteral("2026-03-12T00:00:00Z"));
        };
        const QVariantList all = expand();
        QCOMPARE(service.error(), QString());
        QCOMPARE(all.size(), 3);
        QCOMPARE(all.at(0).toMap().value(QStringLiteral("startUtc")).toString(),
                 QStringLiteral("2026-03-07T16:00:00.000Z"));
        QCOMPARE(all.at(1).toMap().value(QStringLiteral("startUtc")).toString(),
                 QStringLiteral("2026-03-08T15:00:00.000Z"));
        QCOMPARE(all.at(2).toMap().value(QStringLiteral("localStart")).toString(),
                 QStringLiteral("2026-03-09T09:00:00"));

        rule.insert(QStringLiteral("exceptions"), QVariantList{
            QStringLiteral("event-a/2026-03-08T09:00:00")});
        const QVariantList skipped = expand();
        QCOMPARE(skipped.size(), 2);
        QCOMPARE(skipped.at(1).toMap().value(QStringLiteral("localStart")).toString(),
                 QStringLiteral("2026-03-09T09:00:00"));

        rule.remove(QStringLiteral("exceptions"));
        const QVariantList gap = service.expand(QStringLiteral("event-b"),
            QStringLiteral("2026-03-07T02:30:00"), 3600, QStringLiteral("America/Denver"), rule,
            QStringLiteral("2026-03-07T00:00:00Z"), QStringLiteral("2026-03-12T00:00:00Z"));
        QCOMPARE(gap.size(), 3);
        QCOMPARE(gap.at(1).toMap().value(QStringLiteral("localStart")).toString(),
                 QStringLiteral("2026-03-09T02:30:00"));
    }

    // OFF-31 fixtures: leap days and month ends follow RFC 5545 (a rule on a
    // date a month or year lacks skips it), and skipped dates are not counted.
    void leapDayMonthEndAndEndDateFixtures()
    {
        RecurrenceService service;
        const auto starts = [](const QVariantList &rows) {
            QStringList out;
            for (const QVariant &row : rows) out.append(row.toMap().value(QStringLiteral("localStart")).toString());
            return out;
        };
        QVariantMap yearly{{QStringLiteral("frequency"), QStringLiteral("yearly")},
                           {QStringLiteral("endMode"), QStringLiteral("count")}, {QStringLiteral("count"), 3}};
        QVariantList rows = service.expand(QStringLiteral("leap"), QStringLiteral("2024-02-29T08:00:00"), 3600,
            QStringLiteral("Europe/Berlin"), yearly, QStringLiteral("2024-01-01T00:00:00Z"), QStringLiteral("2040-01-01T00:00:00Z"));
        QCOMPARE(service.error(), QString());
        QCOMPARE(starts(rows), QStringList({QStringLiteral("2024-02-29T08:00:00"), QStringLiteral("2028-02-29T08:00:00"),
                                            QStringLiteral("2032-02-29T08:00:00")}));
        QVERIFY(service.occurrenceAt(QStringLiteral("leap"), QStringLiteral("2024-02-29T08:00:00"), 3600,
            QStringLiteral("Europe/Berlin"), yearly, QStringLiteral("2025-02-28T08:00:00")).isEmpty());

        QVariantMap monthly{{QStringLiteral("frequency"), QStringLiteral("monthly")},
                            {QStringLiteral("endMode"), QStringLiteral("until")}, {QStringLiteral("until"), QStringLiteral("2026-12-31")}};
        rows = service.expand(QStringLiteral("month-end"), QStringLiteral("2026-01-31 17:30"), 1800,
            QStringLiteral("America/Denver"), monthly, QStringLiteral("2026-01-01T00:00:00Z"), QStringLiteral("2027-06-01T00:00:00Z"));
        QCOMPARE(starts(rows), QStringList({QStringLiteral("2026-01-31T17:30:00"), QStringLiteral("2026-03-31T17:30:00"),
            QStringLiteral("2026-05-31T17:30:00"), QStringLiteral("2026-07-31T17:30:00"), QStringLiteral("2026-08-31T17:30:00"),
            QStringLiteral("2026-10-31T17:30:00"), QStringLiteral("2026-12-31T17:30:00")}));
        // The until date is inclusive in local time and the last row keeps MST.
        QCOMPARE(rows.last().toMap().value(QStringLiteral("startUtc")).toString(), QStringLiteral("2027-01-01T00:30:00.000Z"));

        QVariantMap the29th{{QStringLiteral("frequency"), QStringLiteral("monthly")},
                            {QStringLiteral("endMode"), QStringLiteral("count")}, {QStringLiteral("count"), 4}};
        rows = service.expand(QStringLiteral("29th"), QStringLiteral("2027-12-29"), 86400, QStringLiteral("UTC"), the29th,
            QStringLiteral("2027-12-01T00:00:00Z"), QStringLiteral("2029-01-01T00:00:00Z"));
        QCOMPARE(starts(rows), QStringList({QStringLiteral("2027-12-29"), QStringLiteral("2028-01-29"),
                                            QStringLiteral("2028-02-29"), QStringLiteral("2028-03-29")}));
        rows = service.expand(QStringLiteral("29th"), QStringLiteral("2026-01-29"), 86400, QStringLiteral("UTC"), the29th,
            QStringLiteral("2026-01-01T00:00:00Z"), QStringLiteral("2027-01-01T00:00:00Z"));
        QCOMPARE(starts(rows), QStringList({QStringLiteral("2026-01-29"), QStringLiteral("2026-03-29"),
                                            QStringLiteral("2026-04-29"), QStringLiteral("2026-05-29")}));
    }

    void allDaySeriesKeepWholeLocalDaysAcrossDst()
    {
        RecurrenceService service;
        const QVariantMap daily{{QStringLiteral("frequency"), QStringLiteral("daily")},
                                {QStringLiteral("endMode"), QStringLiteral("count")}, {QStringLiteral("count"), 3}};
        // Denver springs forward on 8 March 2026: that day is 23 hours long.
        QVariantList rows = service.expand(QStringLiteral("off"), QStringLiteral("2026-03-07"), 86400,
            QStringLiteral("America/Denver"), daily, QStringLiteral("2026-03-01T00:00:00Z"), QStringLiteral("2026-03-15T00:00:00Z"));
        QCOMPARE(service.error(), QString());
        QCOMPARE(rows.size(), 3);
        const QVariantMap dstDay = rows.at(1).toMap();
        QCOMPARE(dstDay.value(QStringLiteral("occurrenceId")).toString(), QStringLiteral("off/2026-03-08"));
        QCOMPARE(dstDay.value(QStringLiteral("allDay")).toBool(), true);
        QCOMPARE(dstDay.value(QStringLiteral("startUtc")).toString(), QStringLiteral("2026-03-08T07:00:00.000Z"));
        QCOMPARE(dstDay.value(QStringLiteral("endUtc")).toString(), QStringLiteral("2026-03-09T06:00:00.000Z"));
        // Santiago skips local midnight on 6 September 2026: an all-day
        // occurrence starts at 01:00 local, a timed 00:00 occurrence is skipped.
        rows = service.expand(QStringLiteral("cl"), QStringLiteral("2026-09-05"), 86400, QStringLiteral("America/Santiago"),
            daily, QStringLiteral("2026-09-01T00:00:00Z"), QStringLiteral("2026-09-10T00:00:00Z"));
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("startUtc")).toString(), QStringLiteral("2026-09-06T04:00:00.000Z"));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("endUtc")).toString(), QStringLiteral("2026-09-07T03:00:00.000Z"));
        rows = service.expand(QStringLiteral("cl-timed"), QStringLiteral("2026-09-05T00:00:00"), 3600,
            QStringLiteral("America/Santiago"), daily, QStringLiteral("2026-09-01T00:00:00Z"), QStringLiteral("2026-09-10T00:00:00Z"));
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("localStart")).toString(), QStringLiteral("2026-09-07T00:00:00"));
        // All-day exclusions accept the date or the midnight form iCalendar used.
        QVariantMap skipped = daily;
        skipped.insert(QStringLiteral("exceptions"), QVariantList{QStringLiteral("2026-03-08T00:00:00")});
        rows = service.expand(QStringLiteral("off"), QStringLiteral("2026-03-07"), 86400, QStringLiteral("America/Denver"),
            skipped, QStringLiteral("2026-03-01T00:00:00Z"), QStringLiteral("2026-03-15T00:00:00Z"));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("localStart")).toString(), QStringLiteral("2026-03-09"));
    }
};

QTEST_GUILESS_MAIN(RecurrenceServiceTest)
#include "tst_recurrence_service.moc"
