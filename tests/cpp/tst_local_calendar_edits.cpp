// SPDX-License-Identifier: LGPL-3.0-or-later
// Persisted calendar edits: recurrence rules, exclusions, single-occurrence
// overrides and scoped moves, re-expanded by LocalCalendarProjectionModel.
#include "local_calendar_projection_model.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
QVariantMap weeklyReview()
{
    // Thursdays 09:00 in Denver, across the 8 March 2026 DST change.
    return {{QStringLiteral("id"), QStringLiteral("review")}, {QStringLiteral("eventId"), QStringLiteral("review")},
            {QStringLiteral("title"), QStringLiteral("Review")},
            {QStringLiteral("startLocal"), QStringLiteral("2026-03-05T09:00:00")},
            {QStringLiteral("endLocal"), QStringLiteral("2026-03-05T10:00:00")},
            {QStringLiteral("zone"), QStringLiteral("America/Denver")}, {QStringLiteral("allDay"), false},
            {QStringLiteral("recurrenceRule"), QVariant::fromValue(nullptr)}, {QStringLiteral("exceptions"), QVariantList()}};
}

QStringList column(const QVariantList &rows, const char *field)
{
    QStringList out;
    for (const QVariant &row : rows) out.append(row.toMap().value(QString::fromLatin1(field)).toString());
    return out;
}

bool ok(const QVariantMap &result) { return result.value(QStringLiteral("ok")).toBool(); }
} // namespace

class LocalCalendarEditsTest : public QObject {
    Q_OBJECT
private slots:
    // Req 3: a rule edit and an exclusion from RecurrenceEditor persist in
    // the event record and the projection re-expands them after a reload.
    void recurrenceEditsPersistAndReproject()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("calendar.json"));
        LocalProductivityStore store;
        QVERIFY(ok(store.upsert(QStringLiteral("events"), weeklyReview())));
        QVERIFY(ok(store.setEventRecurrence(QStringLiteral("review"), {
            {QStringLiteral("frequency"), QStringLiteral("weekly")}, {QStringLiteral("interval"), 1},
            {QStringLiteral("weekdays"), QVariantList{4}}, {QStringLiteral("endMode"), QStringLiteral("count")},
            {QStringLiteral("count"), 4}, {QStringLiteral("exceptions"), QVariantList{QStringLiteral("ignored")}}})));
        QVERIFY(ok(store.setOccurrenceExcluded(QStringLiteral("review"), QStringLiteral("review/2026-03-12T09:00:00"), true)));
        QVERIFY(!ok(store.setOccurrenceExcluded(QStringLiteral("review"), QStringLiteral("review/2026-03-13T09:00:00"), true)));
        QVERIFY(store.saveTo(path));

        LocalProductivityStore reopened;
        QVERIFY(reopened.loadFrom(path));
        const QVariantMap saved = reopened.events().first().toMap();
        QCOMPARE(saved.value(QStringLiteral("recurrenceRule")).toMap().value(QStringLiteral("count")).toInt(), 4);
        QVERIFY(!saved.value(QStringLiteral("recurrenceRule")).toMap().contains(QStringLiteral("exceptions")));
        QCOMPARE(saved.value(QStringLiteral("exceptions")).toList(), QVariantList{QStringLiteral("2026-03-12T09:00:00")});
        RecurrenceService recurrence;
        LocalCalendarProjectionModel model;
        model.setStore(&reopened);
        model.setRecurrenceService(&recurrence);
        QVERIFY(model.project(QStringLiteral("2026-03-01T00:00:00Z"), QStringLiteral("2026-04-01T00:00:00Z")));
        QCOMPARE(column(model.occurrences(), "startLocal"), QStringList({QStringLiteral("2026-03-05T09:00:00"),
            QStringLiteral("2026-03-19T09:00:00"), QStringLiteral("2026-03-26T09:00:00")}));
        QCOMPARE(column(model.occurrences(), "startUtc"), QStringList({QStringLiteral("2026-03-05T16:00:00.000Z"),
            QStringLiteral("2026-03-19T15:00:00.000Z"), QStringLiteral("2026-03-26T15:00:00.000Z")}));

        // Changing the rule re-expands at once and drops exclusions the new
        // rule no longer produces; undo brings the previous rule back.
        QVERIFY(ok(reopened.setEventRecurrence(QStringLiteral("review"), {
            {QStringLiteral("frequency"), QStringLiteral("weekly")}, {QStringLiteral("weekdays"), QVariantList{2, 4}},
            {QStringLiteral("endMode"), QStringLiteral("until")}, {QStringLiteral("until"), QStringLiteral("2026-03-12")}})));
        QCOMPARE(column(model.occurrences(), "startLocal"), QStringList({QStringLiteral("2026-03-05T09:00:00"),
            QStringLiteral("2026-03-10T09:00:00")}));
        QVERIFY(ok(reopened.undoEventChange()));
        QCOMPARE(model.occurrences().size(), 3);
        QVERIFY(!ok(reopened.setEventRecurrence(QStringLiteral("review"), {{QStringLiteral("frequency"), QStringLiteral("hourly")}})));
        QVERIFY(ok(reopened.setEventRecurrence(QStringLiteral("review"), {})));
        QCOMPARE(column(model.occurrences(), "occurrenceId"), QStringList{QStringLiteral("review/2026-03-05T09:00:00")});
    }

    // OFF-31/OFF-30: moving or retitling one occurrence is an override that
    // keeps the occurrence ID; the series and its other occurrences stay put.
    void singleOccurrenceOverrideStaysSeparateFromSeries()
    {
        LocalProductivityStore store;
        QVariantMap event = weeklyReview();
        event.insert(QStringLiteral("recurrenceRule"), QVariantMap{{QStringLiteral("frequency"), QStringLiteral("weekly")},
                                                                   {QStringLiteral("endMode"), QStringLiteral("never")}});
        QVERIFY(ok(store.upsert(QStringLiteral("events"), event)));
        RecurrenceService recurrence;
        LocalCalendarProjectionModel model;
        model.setStore(&store);
        model.setRecurrenceService(&recurrence);
        QVERIFY(model.project(QStringLiteral("2026-03-01T00:00:00Z"), QStringLiteral("2026-03-22T00:00:00Z")));
        QCOMPARE(model.occurrences().size(), 3);

        // Move the 12 March instance (after the DST change) by +2 hours.
        QVERIFY(ok(store.shiftEventLocal(QStringLiteral("review"), 120, QStringLiteral("occurrence"),
                                         QStringLiteral("review/2026-03-12T09:00:00"))));
        QCOMPARE(store.events().first().toMap().value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-05T09:00:00"));
        QVariantMap moved = model.occurrences().at(1).toMap();
        QCOMPARE(moved.value(QStringLiteral("occurrenceId")).toString(), QStringLiteral("review/2026-03-12T09:00:00"));
        QCOMPARE(moved.value(QStringLiteral("recurrenceId")).toString(), QStringLiteral("2026-03-12T09:00:00"));
        QCOMPARE(moved.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-12T11:00:00"));
        QCOMPARE(moved.value(QStringLiteral("startUtc")).toString(), QStringLiteral("2026-03-12T17:00:00.000Z"));
        QCOMPARE(moved.value(QStringLiteral("overridden")).toBool(), true);
        QCOMPARE(column(model.occurrences(), "startLocal").at(2), QStringLiteral("2026-03-19T09:00:00"));

        // A second occurrence move accumulates on the override; a title
        // override replaces it. A move out of the range hides it, a move of
        // another week's instance into the range shows it once.
        QVERIFY(ok(store.shiftEventLocal(QStringLiteral("review"), 60, QStringLiteral("occurrence"),
                                         QStringLiteral("review/2026-03-12T09:00:00"))));
        QCOMPARE(model.occurrences().at(1).toMap().value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-12T12:00:00"));
        QVERIFY(ok(store.shiftEventLocal(QStringLiteral("review"), -7 * 1440, QStringLiteral("occurrence"),
                                         QStringLiteral("review/2026-03-26T09:00:00"))));
        QCOMPARE(column(model.occurrences(), "occurrenceId"), QStringList({QStringLiteral("review/2026-03-05T09:00:00"),
            QStringLiteral("review/2026-03-12T09:00:00"), QStringLiteral("review/2026-03-19T09:00:00"),
            QStringLiteral("review/2026-03-26T09:00:00")}));
        QCOMPARE(model.occurrences().at(3).toMap().value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-19T09:00:00"));
        QVERIFY(ok(store.setOccurrenceOverride(QStringLiteral("review"), QStringLiteral("review/2026-03-19T09:00:00"),
                                               {{QStringLiteral("title"), QStringLiteral("Review (guest)")}})));
        QCOMPARE(model.occurrences().at(2).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Review (guest)"));
        QCOMPARE(model.occurrences().at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Review"));

        // Excluding an overridden instance removes its override; undo
        // restores both. A date the rule never produces is refused.
        QVERIFY(ok(store.setOccurrenceExcluded(QStringLiteral("review"), QStringLiteral("review/2026-03-12T09:00:00"), true)));
        QCOMPARE(model.occurrences().size(), 3);
        QVERIFY(ok(store.undoEventChange()));
        QCOMPARE(model.occurrences().at(1).toMap().value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-12T12:00:00"));
        QVERIFY(!ok(store.shiftEventLocal(QStringLiteral("review"), 30, QStringLiteral("occurrence"),
                                          QStringLiteral("review/2026-03-11T09:00:00"))));

        // Restoring clears the override; the series then moves as a whole.
        QVERIFY(ok(store.setOccurrenceOverride(QStringLiteral("review"), QStringLiteral("review/2026-03-12T09:00:00"), {})));
        QVERIFY(ok(store.shiftEventLocal(QStringLiteral("review"), 60)));
        QCOMPARE(column(model.occurrences(), "startLocal").mid(0, 2), QStringList({QStringLiteral("2026-03-05T10:00:00"),
            QStringLiteral("2026-03-12T10:00:00")}));
    }

    // OFF-30: a recurring event moved across DST keeps its local wall time
    // for every occurrence; "following" splits the series; undo reverts.
    void scopedMovesAcrossDstWithUndo()
    {
        LocalProductivityStore store;
        QVariantMap event = weeklyReview();
        event.insert(QStringLiteral("recurrenceRule"), QVariantMap{{QStringLiteral("frequency"), QStringLiteral("weekly")},
            {QStringLiteral("weekdays"), QVariantList{4}}, {QStringLiteral("endMode"), QStringLiteral("count")},
            {QStringLiteral("count"), 4}});
        event.insert(QStringLiteral("exceptions"), QVariantList{QStringLiteral("2026-03-19T09:00:00")});
        event.insert(QStringLiteral("remoteAccount"), QStringLiteral("work"));
        event.insert(QStringLiteral("remoteHref"), QStringLiteral("/dav/review.ics"));
        QVERIFY(ok(store.upsert(QStringLiteral("events"), event)));
        RecurrenceService recurrence;
        LocalCalendarProjectionModel model;
        model.setStore(&store);
        model.setRecurrenceService(&recurrence);
        QVERIFY(model.project(QStringLiteral("2026-03-01T00:00:00Z"), QStringLiteral("2026-04-15T00:00:00Z")));

        // Whole series one day later: Thursdays become Fridays, the
        // exclusion follows, and 09:00 stays 09:00 on both sides of DST.
        QVERIFY(ok(store.shiftEventLocal(QStringLiteral("review"), 1440, QStringLiteral("series"))));
        QVariantMap row = store.events().first().toMap();
        QCOMPARE(row.value(QStringLiteral("recurrenceRule")).toMap().value(QStringLiteral("weekdays")).toList(), QVariantList{5});
        QCOMPARE(row.value(QStringLiteral("exceptions")).toList(), QVariantList{QStringLiteral("2026-03-20T09:00:00")});
        QCOMPARE(column(model.occurrences(), "startUtc"), QStringList({QStringLiteral("2026-03-06T16:00:00.000Z"),
            QStringLiteral("2026-03-13T15:00:00.000Z"), QStringLiteral("2026-03-27T15:00:00.000Z")}));
        QVERIFY(ok(store.undoEventChange()));
        QCOMPARE(store.events().first().toMap(), event);

        // This and following from 12 March, one hour later: the head keeps
        // one occurrence (count 1), the new series has the remaining three
        // (one still excluded) and is a new remote resource.
        const QVariantMap split = store.shiftEventLocal(QStringLiteral("review"), 60, QStringLiteral("following"),
                                                        QStringLiteral("review/2026-03-12T09:00:00"));
        QVERIFY2(ok(split), qPrintable(split.value(QStringLiteral("error")).toString()));
        QCOMPARE(split.value(QStringLiteral("eventId")).toString(), QStringLiteral("review~20260312"));
        QCOMPARE(store.events().size(), 2);
        const QVariantMap head = store.events().at(0).toMap();
        const QVariantMap tail = store.events().at(1).toMap();
        QCOMPARE(head.value(QStringLiteral("recurrenceRule")).toMap().value(QStringLiteral("count")).toInt(), 1);
        QCOMPARE(head.value(QStringLiteral("exceptions")).toList(), QVariantList());
        QCOMPARE(tail.value(QStringLiteral("recurrenceRule")).toMap().value(QStringLiteral("count")).toInt(), 3);
        QCOMPARE(tail.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-12T10:00:00"));
        QCOMPARE(tail.value(QStringLiteral("exceptions")).toList(), QVariantList{QStringLiteral("2026-03-19T10:00:00")});
        QCOMPARE(tail.value(QStringLiteral("remoteAccount")).toString(), QStringLiteral("work"));
        QVERIFY(!tail.contains(QStringLiteral("remoteHref")));
        QCOMPARE(column(model.occurrences(), "startLocal"), QStringList({QStringLiteral("2026-03-05T09:00:00"),
            QStringLiteral("2026-03-12T10:00:00"), QStringLiteral("2026-03-26T10:00:00")}));
        QVERIFY(ok(store.undoEventChange()));
        QCOMPARE(store.events().size(), 1);
        QCOMPARE(store.events().first().toMap(), event);

        // Refusals: a move into the 02:00 gap, a partial-day all-day move,
        // an unknown scope.
        QVariantMap early = weeklyReview();
        early.insert(QStringLiteral("id"), QStringLiteral("early"));
        early.insert(QStringLiteral("eventId"), QStringLiteral("early"));
        early.insert(QStringLiteral("startLocal"), QStringLiteral("2026-03-07 02:30"));
        early.insert(QStringLiteral("endLocal"), QStringLiteral("2026-03-07 03:00"));
        QVERIFY(ok(store.upsert(QStringLiteral("events"), early)));
        QVERIFY(!ok(store.shiftEventLocal(QStringLiteral("early"), 1440)));
        QVERIFY(ok(store.shiftEventLocal(QStringLiteral("early"), 2 * 1440)));
        QCOMPARE(store.events().last().toMap().value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-09 02:30"));
        QVERIFY(!ok(store.shiftEventLocal(QStringLiteral("early"), 60, QStringLiteral("sideways"))));
    }

    // OFF-30: all-day events (date-only, as iCalendar VALUE=DATE maps them)
    // move by whole days across DST and keep whole local days.
    void allDayEventsMoveAcrossDstWithUndo()
    {
        LocalProductivityStore store;
        const QVariantMap offsite{{QStringLiteral("id"), QStringLiteral("offsite")}, {QStringLiteral("title"), QStringLiteral("Offsite")},
            {QStringLiteral("startLocal"), QStringLiteral("2026-03-06")}, {QStringLiteral("endLocal"), QStringLiteral("2026-03-08")},
            {QStringLiteral("zone"), QStringLiteral("America/Denver")}, {QStringLiteral("allDay"), true}};
        QVERIFY(ok(store.upsert(QStringLiteral("events"), offsite)));
        RecurrenceService recurrence;
        LocalCalendarProjectionModel model;
        model.setStore(&store);
        model.setRecurrenceService(&recurrence);
        QVERIFY(model.project(QStringLiteral("2026-03-01T00:00:00Z"), QStringLiteral("2026-03-31T00:00:00Z")));
        QCOMPARE(model.occurrences().first().toMap().value(QStringLiteral("occurrenceId")).toString(), QStringLiteral("offsite/2026-03-06"));

        QVERIFY(!ok(store.shiftEventLocal(QStringLiteral("offsite"), 60)));
        QVERIFY(ok(store.shiftEventLocal(QStringLiteral("offsite"), 2 * 1440)));
        QVariantMap row = store.events().first().toMap();
        QCOMPARE(row.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-08"));
        QCOMPARE(row.value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-03-10"));
        QVariantMap projected = model.occurrences().first().toMap();
        QCOMPARE(projected.value(QStringLiteral("startUtc")).toString(), QStringLiteral("2026-03-08T07:00:00.000Z"));
        QCOMPARE(projected.value(QStringLiteral("endUtc")).toString(), QStringLiteral("2026-03-10T06:00:00.000Z"));
        QVERIFY(ok(store.undoEventChange()));
        QCOMPARE(store.events().first().toMap(), offsite);

        // A recurring all-day series: one occurrence moves off the DST day.
        QVariantMap daily = offsite;
        daily.insert(QStringLiteral("endLocal"), QStringLiteral("2026-03-07"));
        daily.insert(QStringLiteral("recurrenceRule"), QVariantMap{{QStringLiteral("frequency"), QStringLiteral("daily")},
            {QStringLiteral("endMode"), QStringLiteral("count")}, {QStringLiteral("count"), 4}});
        QVERIFY(ok(store.upsert(QStringLiteral("events"), daily)));
        QVERIFY(ok(store.shiftEventLocal(QStringLiteral("offsite"), 7 * 1440, QStringLiteral("occurrence"),
                                         QStringLiteral("offsite/2026-03-08"))));
        QCOMPARE(column(model.occurrences(), "startLocal"), QStringList({QStringLiteral("2026-03-06"),
            QStringLiteral("2026-03-07"), QStringLiteral("2026-03-09"), QStringLiteral("2026-03-15")}));
        QCOMPARE(model.occurrences().last().toMap().value(QStringLiteral("occurrenceId")).toString(), QStringLiteral("offsite/2026-03-08"));
        QCOMPARE(model.occurrences().last().toMap().value(QStringLiteral("startUtc")).toString(), QStringLiteral("2026-03-15T06:00:00.000Z"));
    }
};

QTEST_GUILESS_MAIN(LocalCalendarEditsTest)
#include "tst_local_calendar_edits.moc"
