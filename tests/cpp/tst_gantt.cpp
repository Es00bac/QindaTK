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

QTEST_MAIN(TstGantt)
#include "tst_gantt.moc"
