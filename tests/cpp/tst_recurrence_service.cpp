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
};

QTEST_GUILESS_MAIN(RecurrenceServiceTest)
#include "tst_recurrence_service.moc"
