// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_calendar_projection_model.h"

#include <QTemporaryDir>
#include <QtTest>

class LocalCalendarProjectionTest : public QObject {
    Q_OBJECT
private slots:
    void weeklyExceptionAndRangeNavigation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString storePath = directory.filePath(QStringLiteral("calendar.json"));
        QindaTK::LocalProductivityStore store;
        store.upsert(QStringLiteral("events"), {
            {QStringLiteral("id"), QStringLiteral("event-weekly")},
            {QStringLiteral("eventId"), QStringLiteral("event-weekly")},
            {QStringLiteral("title"), QStringLiteral("Weekly review")},
            {QStringLiteral("startLocal"), QStringLiteral("2026-09-24T09:30:00")},
            {QStringLiteral("endLocal"), QStringLiteral("2026-09-24T10:15:00")},
            {QStringLiteral("zone"), QStringLiteral("America/Denver")},
            {QStringLiteral("allDay"), false},
            {QStringLiteral("recurrenceRule"), QVariantMap{
                {QStringLiteral("frequency"), QStringLiteral("weekly")},
                {QStringLiteral("interval"), 1}, {QStringLiteral("weekdays"), QVariantList{4}},
                {QStringLiteral("endMode"), QStringLiteral("never")}}},
            {QStringLiteral("exceptions"), QVariantList{QStringLiteral("event-weekly/2026-10-01T09:30:00")}}
        });
        QVERIFY(store.saveTo(storePath));

        QindaTK::LocalProductivityStore reopened;
        QVERIFY(reopened.loadFrom(storePath));
        QindaTK::RecurrenceService recurrence;
        QindaTK::LocalCalendarProjectionModel model;
        model.setStore(&reopened);
        model.setRecurrenceService(&recurrence);

        QVERIFY(model.project(QStringLiteral("2026-09-21T00:00:00Z"),
                              QStringLiteral("2026-10-12T00:00:00Z")));
        QCOMPARE(model.occurrences().size(), 2);
        const QVariantMap first = model.occurrences().at(0).toMap();
        const QVariantMap second = model.occurrences().at(1).toMap();
        QCOMPARE(first.value(QStringLiteral("eventId")).toString(), QStringLiteral("event-weekly"));
        QCOMPARE(first.value(QStringLiteral("occurrenceId")).toString(), QStringLiteral("event-weekly/2026-09-24T09:30:00"));
        QCOMPARE(first.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-09-24T09:30:00"));
        QCOMPARE(first.value(QStringLiteral("zone")).toString(), QStringLiteral("America/Denver"));
        QCOMPARE(second.value(QStringLiteral("occurrenceId")).toString(), QStringLiteral("event-weekly/2026-10-08T09:30:00"));

        QVERIFY(model.project(QStringLiteral("2026-10-08T00:00:00Z"),
                              QStringLiteral("2026-10-29T00:00:00Z")));
        QCOMPARE(model.rangeStartUtc(), QStringLiteral("2026-10-08T00:00:00Z"));
        QCOMPARE(model.occurrences().size(), 3);
        QCOMPARE(model.occurrences().first().toMap().value(QStringLiteral("occurrenceId")).toString(),
                 QStringLiteral("event-weekly/2026-10-08T09:30:00"));
        QCOMPARE(model.occurrences().last().toMap().value(QStringLiteral("occurrenceId")).toString(),
                 QStringLiteral("event-weekly/2026-10-22T09:30:00"));
    }
};

QTEST_GUILESS_MAIN(LocalCalendarProjectionTest)
#include "tst_local_calendar_projection.moc"
