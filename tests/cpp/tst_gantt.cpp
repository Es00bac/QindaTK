// SPDX-License-Identifier: LGPL-3.0-or-later
#include "gantt_calendar.h"
#include "gantt_task_model.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;

namespace {
const QString a = QStringLiteral("00000000-0000-0000-0000-000000000001");
const QString b = QStringLiteral("00000000-0000-0000-0000-000000000002");
QString utc(const char *iso)
{ return QString::number(QDateTime::fromString(QString::fromLatin1(iso), Qt::ISODate).toMSecsSinceEpoch()); }
QVariantMap task(const QString &id, const char *start, qint64 duration,
                 const QVariantList &links = {})
{
    return {{QStringLiteral("taskId"), id}, {QStringLiteral("label"), id},
            {QStringLiteral("startUtc"), utc(start)},
            {QStringLiteral("durationMinutes"), duration},
            {QStringLiteral("progress"), 0.5},
            {QStringLiteral("dependencies"), links}};
}

class RejectCommitAdapter : public GanttSimpleScheduleAdapter {
public:
    bool commitMove(const QVariantMap &, const QVariantList &) override { return false; }
};
}

class TstGantt : public QObject {
    Q_OBJECT
private slots:
    void calendarSkipsWeekendAndDst();
    void springDstWindowCountsElapsedMinutes();
    void fallDstWindowCountsRepeatedHour();
    void sourceValidationIsAtomic();
    void previewCommitCancelUndo();
    void rejectedCommitPreservesSource();
    void optionalFieldsValidate();
    void optionalFieldsReachRoles();
    void referenceAdapterDropsExactFinish();
    void localDateHelpers();
    void planningFieldsValidate();
    void planningFieldsReachRoles();
    void groupRowsAreNeverScheduled();
};

void TstGantt::calendarSkipsWeekendAndDst()
{
    GanttCalendar calendar;
    calendar.setTimeZoneId(QStringLiteral("America/New_York"));
    const QString friday = utc("2025-03-07T21:00:00Z"); // Friday 16:00 EST
    const QString monday = utc("2025-03-10T14:00:00Z"); // Monday 10:00 EDT
    QCOMPARE(calendar.addWorkingMinutes(friday, 120), monday);
    QCOMPARE(calendar.workingMinutesBetween(friday, monday), 120.0);
    QCOMPARE(calendar.workingMinutesBetween(monday, friday), -120.0);
    QCOMPARE(calendar.snapForward(utc("2025-03-08T15:00:00Z")),
             utc("2025-03-10T13:00:00Z"));
    calendar.setHolidays({QStringLiteral("2025-03-10")});
    QCOMPARE(calendar.addWorkingMinutes(friday, 120), utc("2025-03-11T14:00:00Z"));
}

void TstGantt::springDstWindowCountsElapsedMinutes()
{
    GanttCalendar calendar;
    calendar.setTimeZoneId(QStringLiteral("America/New_York"));
    calendar.setWorkingDays(1 << 6); // Sunday only.
    calendar.setWorkStart(QTime(1, 0));
    calendar.setWorkEnd(QTime(5, 0));
    const QString start = utc("2025-03-09T06:00:00Z"); // 01:00 EST
    const QString end = utc("2025-03-09T09:00:00Z"); // 05:00 EDT
    QCOMPARE(calendar.workingMinutesBetween(start, end), 180.0);
    QCOMPARE(calendar.addWorkingMinutes(start, 180), end);
    QVERIFY(calendar.formatLocal(end).startsWith(QStringLiteral("2025-03-09 05:00")));
}

void TstGantt::fallDstWindowCountsRepeatedHour()
{
    GanttCalendar calendar;
    calendar.setTimeZoneId(QStringLiteral("America/New_York"));
    calendar.setWorkingDays(1 << 6);
    calendar.setWorkStart(QTime(0, 0));
    calendar.setWorkEnd(QTime(3, 0));
    const QString start = utc("2025-11-02T04:00:00Z"); // 00:00 EDT
    const QString end = utc("2025-11-02T08:00:00Z"); // 03:00 EST
    QCOMPARE(calendar.workingMinutesBetween(start, end), 240.0);
    QCOMPARE(calendar.addWorkingMinutes(start, 240), end);
}

void TstGantt::sourceValidationIsAtomic()
{
    GanttTaskModel model;
    QVERIFY(model.replaceTasks({task(a, "2025-03-07T21:00:00Z", 60),
                                task(b, "2025-03-10T13:00:00Z", 120, {a})}));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.indexOfId(b), 1);
    QCOMPARE(model.get(1).value(QStringLiteral("dependencies")).toList().size(), 1);
    QVERIFY(!model.replaceTasks({task(a, "2025-03-07T21:00:00Z", 60),
                                 task(a, "2025-03-10T13:00:00Z", 120)}));
    QCOMPARE(model.count(), 2);
    QVERIFY(model.lastError().contains(QStringLiteral("unique")));
    QVERIFY(!model.replaceTasks({task(a, "2025-03-07T21:00:00Z", 60, {b}),
                                 task(b, "2025-03-10T13:00:00Z", 120, {a})}));
    QVERIFY(model.lastError().contains(QStringLiteral("cycle")));
    QVariantMap invalid = task(a, "2025-03-07T21:00:00Z", 60);
    invalid.insert(QStringLiteral("progress"), QStringLiteral("unknown"));
    QVERIFY(!model.replaceTasks({invalid}));
    QCOMPARE(model.count(), 2);
}

void TstGantt::previewCommitCancelUndo()
{
    GanttCalendar calendar;
    calendar.setTimeZoneId(QStringLiteral("America/New_York"));
    GanttSimpleScheduleAdapter adapter;
    GanttTaskModel model;
    model.setCalendar(&calendar);
    model.setTransactionAdapter(&adapter);
    const QVariantList initial{task(a, "2025-03-07T21:00:00Z", 60),
                               task(b, "2025-03-10T13:00:00Z", 120, {a})};
    QVERIFY(model.replaceTasks(initial));
    const QString moved = utc("2025-03-10T14:00:00Z"); // Monday 10:00 EDT
    QVERIFY(model.previewMove(a, moved));
    QVERIFY(model.previewActive());
    QCOMPARE(model.get(0).value(QStringLiteral("startUtc")).toString(),
             utc("2025-03-07T21:00:00Z"));
    QCOMPARE(model.displayGet(0).value(QStringLiteral("startUtc")).toString(), moved);
    QCOMPARE(model.displayGet(1).value(QStringLiteral("startUtc")).toString(),
             utc("2025-03-10T15:00:00Z")); // predecessor ends at 11:00 EDT
    model.cancelPreview();
    QVERIFY(!model.previewActive());
    QCOMPARE(model.get(1), initial.at(1).toMap());
    QVERIFY(model.previewMove(a, moved));
    QSignalSpy committed(&model, &GanttTaskModel::committed);
    QVERIFY(model.commitPreview());
    QCOMPARE(committed.size(), 1);
    QCOMPARE(model.get(1).value(QStringLiteral("startUtc")).toString(),
             utc("2025-03-10T15:00:00Z"));
    QVERIFY(model.undo());
    QCOMPARE(model.get(0), initial.at(0).toMap());
    QCOMPARE(model.get(1), initial.at(1).toMap());
}

void TstGantt::rejectedCommitPreservesSource()
{
    GanttCalendar calendar;
    RejectCommitAdapter adapter;
    GanttTaskModel model;
    model.setCalendar(&calendar);
    model.setTransactionAdapter(&adapter);
    const QVariantMap original = task(a, "2025-03-10T09:00:00Z", 60);
    QVERIFY(model.replaceTasks({original}));
    QVERIFY(model.previewMove(a, utc("2025-03-10T10:00:00Z")));
    QVERIFY(!model.commitPreview());
    QVERIFY(model.previewActive());
    QCOMPARE(model.get(0), original);
    model.cancelPreview();
    QVERIFY(!model.previewActive());
}

void TstGantt::optionalFieldsValidate()
{
    GanttTaskModel model;
    QVariantMap first = task(a, "2025-03-10T13:00:00Z", 480);
    first.insert(QStringLiteral("finishUtc"), utc("2025-03-10T21:00:00Z"));
    first.insert(QStringLiteral("baselineStartUtc"), utc("2025-03-10T13:00:00Z"));
    first.insert(QStringLiteral("baselineFinishUtc"), utc("2025-03-11T21:00:00Z"));
    first.insert(QStringLiteral("kind"), QStringLiteral("summary"));
    first.insert(QStringLiteral("critical"), true);
    first.insert(QStringLiteral("inactive"), false);
    first.insert(QStringLiteral("outlineLevel"), 2);
    first.insert(QStringLiteral("barText"), QStringLiteral("Ana, Ben"));
    QVariantMap second = task(b, "2025-03-11T13:00:00Z", 0, {a});
    second.insert(QStringLiteral("linkTypes"), QVariantList{QStringLiteral("SS")});
    second.insert(QStringLiteral("kind"), QStringLiteral("milestone"));
    QVERIFY2(model.replaceTasks({first, second}), qPrintable(model.lastError()));
    QCOMPARE(model.count(), 2);

    // Every bad value rejects the whole replacement and names the field.
    const auto rejects = [&](const QString &key, const QVariant &value, const QString &needle) {
        QVariantMap bad = second;
        bad.insert(key, value);
        const bool accepted = model.replaceTasks({first, bad});
        return !accepted && model.lastError().contains(needle) && model.count() == 2
            && model.get(1) == second;
    };
    QVERIFY(rejects(QStringLiteral("finishUtc"), utc("2025-03-11T12:00:00Z"), QStringLiteral("finishUtc")));
    QVERIFY(rejects(QStringLiteral("finishUtc"), 12345, QStringLiteral("finishUtc")));
    QVERIFY(rejects(QStringLiteral("baselineFinishUtc"), QStringLiteral("x"), QStringLiteral("baselineFinishUtc")));
    QVERIFY(rejects(QStringLiteral("kind"), QStringLiteral("phase"), QStringLiteral("kind")));
    QVERIFY(rejects(QStringLiteral("critical"), QStringLiteral("yes"), QStringLiteral("critical")));
    QVERIFY(rejects(QStringLiteral("inactive"), 1, QStringLiteral("inactive")));
    QVERIFY(rejects(QStringLiteral("outlineLevel"), 65, QStringLiteral("outlineLevel")));
    QVERIFY(rejects(QStringLiteral("outlineLevel"), 1.5, QStringLiteral("outlineLevel")));
    QVERIFY(rejects(QStringLiteral("barText"), QVariantList{}, QStringLiteral("barText")));
    QVERIFY(rejects(QStringLiteral("linkTypes"), QVariantList{QStringLiteral("XX")}, QStringLiteral("linkTypes")));
    QVERIFY(rejects(QStringLiteral("linkTypes"), QVariantList{QStringLiteral("FS"), QStringLiteral("FF")},
                    QStringLiteral("parallel")));
    // A baseline finish is checked against the baseline start, not the start.
    QVariantMap early = second;
    early.insert(QStringLiteral("baselineStartUtc"), utc("2025-03-01T13:00:00Z"));
    early.insert(QStringLiteral("baselineFinishUtc"), utc("2025-03-02T13:00:00Z"));
    QVERIFY2(model.replaceTasks({first, early}), qPrintable(model.lastError()));
}

void TstGantt::optionalFieldsReachRoles()
{
    GanttTaskModel model;
    QVariantMap row = task(a, "2025-03-10T13:00:00Z", 60);
    row.insert(QStringLiteral("critical"), true);
    row.insert(QStringLiteral("kind"), QStringLiteral("task"));
    row.insert(QStringLiteral("barText"), QStringLiteral("Ana"));
    QVERIFY(model.replaceTasks({row}));
    const QHash<int, QByteArray> roles = model.roleNames();
    for (const char *name : {"finishUtc", "baselineFinishUtc", "kind", "critical", "inactive",
                             "outlineLevel", "barText", "linkTypes"}) {
        QVERIFY2(roles.values().contains(QByteArray(name)), name);
    }
    QCOMPARE(model.data(model.index(0), GanttTaskModel::CriticalRole).toBool(), true);
    QCOMPARE(model.data(model.index(0), GanttTaskModel::BarTextRole).toString(), QStringLiteral("Ana"));
    QCOMPARE(model.get(0).value(QStringLiteral("kind")).toString(), QStringLiteral("task"));
}

void TstGantt::referenceAdapterDropsExactFinish()
{
    GanttCalendar calendar;
    calendar.setTimeZoneId(QStringLiteral("America/New_York"));
    GanttSimpleScheduleAdapter adapter;
    GanttTaskModel model;
    model.setCalendar(&calendar);
    model.setTransactionAdapter(&adapter);
    QVariantMap first = task(a, "2025-03-07T21:00:00Z", 60);
    first.insert(QStringLiteral("finishUtc"), utc("2025-03-10T13:00:00Z"));
    QVariantMap second = task(b, "2025-03-11T13:00:00Z", 60, {a});
    second.insert(QStringLiteral("finishUtc"), utc("2025-03-11T14:00:00Z"));
    QVERIFY(model.replaceTasks({first, second}));
    QVERIFY(model.previewMove(a, utc("2025-03-10T14:00:00Z")));
    QVERIFY(!model.displayGet(0).contains(QStringLiteral("finishUtc")));
    // The successor did not move (it already starts after the new finish).
    QCOMPARE(model.displayGet(1).value(QStringLiteral("finishUtc")).toString(), utc("2025-03-11T14:00:00Z"));
}

void TstGantt::localDateHelpers()
{
    GanttCalendar calendar;
    calendar.setTimeZoneId(QStringLiteral("America/New_York"));
    // 03:30 UTC on 10 March is still 9 March in New York (EDT, UTC-4).
    QCOMPARE(calendar.localDate(utc("2025-03-10T03:30:00Z")), QStringLiteral("2025-03-09"));
    QCOMPARE(calendar.dayStartUtc(QStringLiteral("2025-03-09")), utc("2025-03-09T05:00:00Z"));
    // The spring-forward day is 23 hours long, the fall-back day 25.
    const qint64 spring = calendar.dayStartUtc(QStringLiteral("2025-03-10")).toLongLong()
                          - calendar.dayStartUtc(QStringLiteral("2025-03-09")).toLongLong();
    QCOMPARE(spring, qint64(23) * 3600000);
    const qint64 fall = calendar.dayStartUtc(QStringLiteral("2025-11-03")).toLongLong()
                        - calendar.dayStartUtc(QStringLiteral("2025-11-02")).toLongLong();
    QCOMPARE(fall, qint64(25) * 3600000);
    QVERIFY(calendar.dayStartUtc(QStringLiteral("2025-02-30")).isEmpty());
    QVERIFY(calendar.localDate(QStringLiteral("1e3")).isEmpty());
    QVERIFY(calendar.isWorkingDate(QStringLiteral("2025-03-10")));
    QVERIFY(!calendar.isWorkingDate(QStringLiteral("2025-03-09")));
    // A zone whose DST starts at midnight has no local 00:00 that day: the
    // day starts at the first valid instant (01:00 local).
    GanttCalendar havana;
    havana.setTimeZoneId(QStringLiteral("America/Havana"));
    const QString start = havana.dayStartUtc(QStringLiteral("2025-03-09"));
    QVERIFY(!start.isEmpty());
    QCOMPARE(havana.localDate(start), QStringLiteral("2025-03-09"));
}

// D-312/D-313: group headers, slack, deadlines, progress points, split bars
// and the second baseline lane are validated with the rest of the row.
void TstGantt::planningFieldsValidate()
{
    const QString g = QStringLiteral("00000000-0000-0000-0000-000000000003");
    GanttTaskModel model;
    QVariantMap group = task(g, "2025-03-10T13:00:00Z", 960);
    group.insert(QStringLiteral("kind"), QStringLiteral("group"));
    group.insert(QStringLiteral("collapsed"), false);
    group.insert(QStringLiteral("summaryText"), QStringLiteral("2 tasks · 16 h"));
    group.insert(QStringLiteral("finishUtc"), utc("2025-03-11T21:00:00Z"));
    QVariantMap first = task(a, "2025-03-10T13:00:00Z", 480);
    first.insert(QStringLiteral("slackFinishUtc"), utc("2025-03-12T21:00:00Z"));
    first.insert(QStringLiteral("freeSlackFinishUtc"), utc("2025-03-11T21:00:00Z"));
    first.insert(QStringLiteral("deadlineUtc"), utc("2025-03-09T21:00:00Z"));
    first.insert(QStringLiteral("progressUtc"), utc("2025-03-10T17:00:00Z"));
    first.insert(QStringLiteral("segments"), QVariantList{
        QVariantMap{{QStringLiteral("startUtc"), utc("2025-03-10T13:00:00Z")}, {QStringLiteral("finishUtc"), utc("2025-03-10T17:00:00Z")}},
        QVariantMap{{QStringLiteral("startUtc"), utc("2025-03-11T13:00:00Z")}, {QStringLiteral("finishUtc"), utc("2025-03-11T17:00:00Z")}}});
    first.insert(QStringLiteral("baseline2StartUtc"), utc("2025-03-03T13:00:00Z"));
    first.insert(QStringLiteral("baseline2FinishUtc"), utc("2025-03-03T21:00:00Z"));
    QVariantMap second = task(b, "2025-03-11T13:00:00Z", 480, {a});
    second.insert(QStringLiteral("baseline2StartUtc"), utc("2025-03-04T13:00:00Z"));
    second.insert(QStringLiteral("baseline2DurationMinutes"), 480);
    QVERIFY2(model.replaceTasks({group, first, second}), qPrintable(model.lastError()));
    QCOMPARE(model.count(), 3);

    const auto rejects = [&](const QVariantMap &bad, int at, const QString &needle) {
        QVariantList rows{group, first, second};
        rows[at] = bad;
        return !model.replaceTasks(rows) && model.lastError().contains(needle) && model.count() == 3;
    };
    const auto with = [](QVariantMap row, const QString &key, const QVariant &value) {
        row.insert(key, value);
        return row;
    };
    QVERIFY(rejects(with(first, QStringLiteral("slackFinishUtc"), utc("2025-03-09T13:00:00Z")), 1, QStringLiteral("slackFinishUtc")));
    QVERIFY(rejects(with(first, QStringLiteral("freeSlackFinishUtc"), 7), 1, QStringLiteral("freeSlackFinishUtc")));
    QVERIFY(rejects(with(first, QStringLiteral("deadlineUtc"), QStringLiteral("soon")), 1, QStringLiteral("deadlineUtc")));
    QVERIFY(rejects(with(first, QStringLiteral("progressUtc"), utc("2025-03-01T13:00:00Z")), 1, QStringLiteral("progressUtc")));
    QVERIFY(rejects(with(group, QStringLiteral("collapsed"), QStringLiteral("yes")), 0, QStringLiteral("collapsed")));
    QVERIFY(rejects(with(group, QStringLiteral("summaryText"), 3), 0, QStringLiteral("summaryText")));
    QVERIFY(rejects(with(group, QStringLiteral("dependencies"), QVariantList{a}), 0, QStringLiteral("group row has no")));
    QVERIFY(rejects(with(second, QStringLiteral("dependencies"), QVariantList{g}), 2, QStringLiteral("group row")));
    QVERIFY(rejects(with(second, QStringLiteral("baseline2FinishUtc"), utc("2025-03-01T13:00:00Z")), 2, QStringLiteral("baseline2FinishUtc")));
    QVERIFY(rejects(with(second, QStringLiteral("baseline2DurationMinutes"), -1), 2, QStringLiteral("baseline2DurationMinutes")));
    QVariantMap orphanLane = task(b, "2025-03-11T13:00:00Z", 480, {a});
    orphanLane.insert(QStringLiteral("baseline2FinishUtc"), utc("2025-03-12T13:00:00Z"));
    QVERIFY(rejects(orphanLane, 2, QStringLiteral("baseline2StartUtc")));
    const QVariantMap piece{{QStringLiteral("startUtc"), utc("2025-03-10T13:00:00Z")},
                            {QStringLiteral("finishUtc"), utc("2025-03-10T15:00:00Z")}};
    QVERIFY(rejects(with(first, QStringLiteral("segments"), QVariantList{}), 1, QStringLiteral("segments")));
    QVERIFY(rejects(with(first, QStringLiteral("segments"), QVariantList{piece, piece}), 1, QStringLiteral("segments")));
    QVariantMap backwards = piece;
    backwards.insert(QStringLiteral("finishUtc"), utc("2025-03-10T12:00:00Z"));
    QVERIFY(rejects(with(first, QStringLiteral("segments"), QVariantList{backwards}), 1, QStringLiteral("segments")));
    QVariantMap early = piece;
    early.insert(QStringLiteral("startUtc"), utc("2025-03-10T12:00:00Z"));
    QVERIFY(rejects(with(first, QStringLiteral("segments"), QVariantList{early}), 1, QStringLiteral("segments")));
    QVERIFY(rejects(with(first, QStringLiteral("kind"), QStringLiteral("band")), 1, QStringLiteral("group")));
    // A rejected replacement left the accepted rows in place.
    QCOMPARE(model.get(0).value(QStringLiteral("kind")).toString(), QStringLiteral("group"));
}

void TstGantt::planningFieldsReachRoles()
{
    GanttTaskModel model;
    QVariantMap row = task(a, "2025-03-10T13:00:00Z", 60);
    row.insert(QStringLiteral("deadlineUtc"), utc("2025-03-12T13:00:00Z"));
    row.insert(QStringLiteral("summaryText"), QStringLiteral("late"));
    QVERIFY(model.replaceTasks({row}));
    const QHash<int, QByteArray> roles = model.roleNames();
    for (const char *name : {"collapsed", "summaryText", "slackFinishUtc", "freeSlackFinishUtc", "deadlineUtc",
                             "progressUtc", "segments", "baseline2StartUtc", "baseline2FinishUtc",
                             "baseline2DurationMinutes"}) {
        QVERIFY2(roles.values().contains(QByteArray(name)), name);
    }
    QCOMPARE(model.data(model.index(0), GanttTaskModel::DeadlineUtcRole).toString(), utc("2025-03-12T13:00:00Z"));
    QCOMPARE(model.data(model.index(0), GanttTaskModel::SummaryTextRole).toString(), QStringLiteral("late"));
    // Earlier roles keep their values (appended enum).
    QCOMPARE(int(GanttTaskModel::LinkTypesRole), int(Qt::UserRole) + 16);
}

void TstGantt::groupRowsAreNeverScheduled()
{
    const QString g = QStringLiteral("00000000-0000-0000-0000-000000000003");
    GanttCalendar calendar;
    calendar.setTimeZoneId(QStringLiteral("America/New_York"));
    GanttSimpleScheduleAdapter adapter;
    GanttTaskModel model;
    model.setCalendar(&calendar);
    model.setTransactionAdapter(&adapter);
    // The group header starts on a Saturday: a scheduler would snap it.
    QVariantMap group = task(g, "2025-03-08T15:00:00Z", 0);
    group.insert(QStringLiteral("kind"), QStringLiteral("group"));
    QVariantMap first = task(a, "2025-03-10T13:00:00Z", 60);
    first.insert(QStringLiteral("segments"), QVariantList{
        QVariantMap{{QStringLiteral("startUtc"), utc("2025-03-10T13:00:00Z")}, {QStringLiteral("finishUtc"), utc("2025-03-10T13:30:00Z")}},
        QVariantMap{{QStringLiteral("startUtc"), utc("2025-03-10T15:00:00Z")}, {QStringLiteral("finishUtc"), utc("2025-03-10T15:30:00Z")}}});
    QVERIFY2(model.replaceTasks({group, first}), qPrintable(model.lastError()));
    QVERIFY(!model.previewMove(g, utc("2025-03-10T13:00:00Z")));
    QVERIFY(model.lastError().contains(QStringLiteral("group")));
    QVERIFY2(model.previewMove(a, utc("2025-03-10T14:00:00Z")), qPrintable(model.lastError()));
    QCOMPARE(model.displayGet(0), group);
    QVERIFY(!model.displayGet(1).contains(QStringLiteral("segments")));
    QVERIFY(model.commitPreview());
    QCOMPARE(model.get(0), group);
}

QTEST_MAIN(TstGantt)
#include "tst_gantt.moc"
