// SPDX-License-Identifier: LGPL-3.0-or-later
#include "caldav_provider.h"
#include "caldav_test_server.h"
#include "ical_codec.h"
#include "recurrence_service.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
const QString kSecret = QStringLiteral("dav-Secret-19");

QByteArray eventIcs(const QByteArray &summary)
{
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
           "BEGIN:VEVENT\r\nUID:ev-1\r\nDTSTAMP:20260901T000000Z\r\nSUMMARY:" + summary + "\r\n"
           "DTSTART;TZID=America/Denver:20260307T090000\r\n"
           "DTEND;TZID=America/New_York:20260307T120000\r\n"
           "RRULE:FREQ=WEEKLY;BYDAY=SA;UNTIL=20260322T055959Z\r\n"
           "EXDATE;TZID=America/Denver:20260314T090000\r\n"
           "X-CUSTOM-FLAG:keep-me\r\n"
           "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:Reminder\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\n"
           "END:VEVENT\r\nEND:VCALENDAR\r\n";
}

const QByteArray kTaskIcs =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
    "BEGIN:VTODO\r\nUID:task-1\r\nDTSTAMP:20260901T000000Z\r\nSUMMARY:Review inbox\\, then plan\r\n"
    "STATUS:NEEDS-ACTION\r\nPRIORITY:1\r\nDUE;VALUE=DATE:20260930\r\nX-QINDATK-ORDER:2\r\n"
    "END:VTODO\r\nEND:VCALENDAR\r\n";

QVariantMap record(const QVariantList &rows, const QString &id)
{
    for (const QVariant &value : rows)
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    return {};
}

QVariantMap syncNow(CalDavProvider &provider, const QString &id)
{
    QSignalSpy spy(&provider, &CalDavProvider::syncFinished);
    provider.sync(id);
    if (!spy.wait(15000)) return {};
    return spy.takeFirst().at(1).toMap();
}

QVariantMap change(CalDavProvider &provider, const std::function<void()> &start)
{
    QSignalSpy spy(&provider, &CalDavProvider::changeFinished);
    start();
    if (!spy.wait(15000)) return {};
    return spy.takeFirst().at(1).toMap();
}

void configure(CalDavProvider &provider, const QString &url, LocalProductivityStore *store)
{
    provider.setProperty("collectionUrl", url);
    provider.setProperty("userName", QStringLiteral("ada"));
    provider.setProperty("accountId", QStringLiteral("work"));
    provider.setProperty("calendarId", QStringLiteral("work"));
    provider.setProperty("timeoutMs", 10000);
    provider.setStore(store);
    provider.setSecret(kSecret);
}
} // namespace

class CalDavProviderTest : public QObject {
    Q_OBJECT
private slots:
    void icalMappingKeepsWallTimesAcrossDst()
    {
        const ICal::Mapped mapped = ICal::toRecord(eventIcs("Design review"), QStringLiteral("UTC"));
        QVERIFY2(mapped.ok, qPrintable(mapped.error));
        QCOMPARE(mapped.component, QStringLiteral("VEVENT"));
        QCOMPARE(mapped.uid, QStringLiteral("ev-1"));
        const QVariantMap &f = mapped.fields;
        QCOMPARE(f.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-07 09:00"));
        QCOMPARE(f.value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-03-07 10:00"));   // 12:00 New York
        QCOMPARE(f.value(QStringLiteral("zone")).toString(), QStringLiteral("America/Denver"));
        const QVariantMap rule = f.value(QStringLiteral("recurrenceRule")).toMap();
        QCOMPARE(rule.value(QStringLiteral("frequency")).toString(), QStringLiteral("weekly"));
        QCOMPARE(rule.value(QStringLiteral("weekdays")).toList(), QVariantList{6});
        QCOMPARE(rule.value(QStringLiteral("endMode")).toString(), QStringLiteral("until"));
        QCOMPARE(rule.value(QStringLiteral("until")).toString(), QStringLiteral("2026-03-21"));
        QCOMPARE(f.value(QStringLiteral("exceptions")).toList(), QVariantList{QStringLiteral("2026-03-14T09:00:00")});

        // The mapped record drives the local recurrence engine: 09:00 local on
        // both sides of the 8 March DST change, with the 14th excluded.
        QVariantMap effective = rule;
        effective.insert(QStringLiteral("exceptions"), f.value(QStringLiteral("exceptions")));
        RecurrenceService recurrence;
        const QVariantList rows = recurrence.expand(QStringLiteral("ev-1"), QStringLiteral("2026-03-07T09:00:00"), 3600,
                                                    QStringLiteral("America/Denver"), effective,
                                                    QStringLiteral("2026-03-01T00:00:00Z"), QStringLiteral("2026-04-01T00:00:00Z"));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("startUtc")).toString(), QStringLiteral("2026-03-07T16:00:00.000Z"));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("startUtc")).toString(), QStringLiteral("2026-03-21T15:00:00.000Z"));

        QVariantMap edited = f;
        edited.insert(QStringLiteral("title"), QStringLiteral("Design review (moved)"));
        const QByteArray written = ICal::fromRecord(QStringLiteral("VEVENT"), QStringLiteral("ev-1"), edited,
                                                    eventIcs("Design review"));
        QVERIFY(written.contains("SUMMARY:Design review (moved)\r\n"));
        QVERIFY(!written.contains("SUMMARY:Design review\r\n"));
        QVERIFY(written.contains("DTSTART;TZID=America/Denver:20260307T090000\r\n"));
        QVERIFY(written.contains("DTEND;TZID=America/Denver:20260307T100000\r\n"));
        QVERIFY(written.contains("RRULE:FREQ=WEEKLY;BYDAY=SA;UNTIL=20260322T055959Z\r\n"));
        QVERIFY(written.contains("EXDATE;TZID=America/Denver:20260314T090000\r\n"));
        QVERIFY(written.contains("X-CUSTOM-FLAG:keep-me\r\n"));                 // unknown property kept
        QVERIFY(written.contains("DESCRIPTION:Reminder\r\nTRIGGER:-PT15M\r\n"));  // nested VALARM kept
        const ICal::Mapped reread = ICal::toRecord(written, QStringLiteral("UTC"));
        QCOMPARE(ICal::digest(QStringLiteral("VEVENT"), reread.fields), ICal::digest(QStringLiteral("VEVENT"), edited));
    }

    void icalAllDayUnsupportedRulesTasksAndFolding()
    {
        const QByteArray allDay = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:a\r\nSUMMARY:Offsite\r\n"
                                  "DTSTART;VALUE=DATE:20260228\r\nDURATION:P2D\r\nRRULE:FREQ=MONTHLY;BYMONTHDAY=-1\r\n"
                                  "END:VEVENT\r\nEND:VCALENDAR\r\n";
        const ICal::Mapped mapped = ICal::toRecord(allDay, QStringLiteral("Europe/Berlin"));
        QVERIFY(mapped.ok);
        QVERIFY(mapped.fields.value(QStringLiteral("allDay")).toBool());
        QCOMPARE(mapped.fields.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-02-28"));
        QCOMPARE(mapped.fields.value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-03-02"));
        QVERIFY(mapped.fields.value(QStringLiteral("recurrenceRule")).isNull());
        QCOMPARE(mapped.fields.value(QStringLiteral("recurrenceRaw")).toString(), QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=-1"));
        const QByteArray again = ICal::fromRecord(QStringLiteral("VEVENT"), QStringLiteral("a"), mapped.fields, allDay);
        QVERIFY(again.contains("RRULE:FREQ=MONTHLY;BYMONTHDAY=-1\r\n"));
        QVERIFY(again.contains("DTSTART;VALUE=DATE:20260228\r\n"));

        const ICal::Mapped task = ICal::toRecord(kTaskIcs, QStringLiteral("UTC"));
        QCOMPARE(task.component, QStringLiteral("VTODO"));
        QCOMPARE(task.fields.value(QStringLiteral("title")).toString(), QStringLiteral("Review inbox, then plan"));
        QCOMPARE(task.fields.value(QStringLiteral("status")).toString(), QStringLiteral("todo"));
        QCOMPARE(task.fields.value(QStringLiteral("priority")).toString(), QStringLiteral("High"));
        QCOMPARE(task.fields.value(QStringLiteral("dueDate")).toString(), QStringLiteral("2026-09-30"));
        QCOMPARE(task.fields.value(QStringLiteral("order")).toInt(), 2);

        QVariantMap longTask = task.fields;
        longTask.insert(QStringLiteral("title"), QString(100, QChar(0x00e9)));
        const QByteArray folded = ICal::fromRecord(QStringLiteral("VTODO"), QStringLiteral("t"), longTask);
        for (const QByteArray &line : folded.split('\n')) QVERIFY(line.size() <= 76);   // 75 octets + CR
        QCOMPARE(ICal::toRecord(folded, QStringLiteral("UTC")).fields.value(QStringLiteral("title")).toString(),
                 QString(100, QChar(0x00e9)));
    }

    void twoWaySyncConflictsAndDeletes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        CalDavTestServer server;
        server.authorization = "Basic " + QByteArray("ada:" + kSecret.toUtf8()).toBase64();
        server.chunked = true;
        const QString eventPath = server.path(QStringLiteral("ev-1.ics"));
        const QString taskPath = server.path(QStringLiteral("task-1.ics"));
        server.put(eventPath, eventIcs("Design review"));
        server.put(taskPath, kTaskIcs);
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("calendar.json")));
        CalDavProvider provider;
        configure(provider, server.url(), &store);

        QVariantMap result = syncNow(provider, QStringLiteral("sync-1"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        const QString eventId = QStringLiteral("caldav:work:ev-1"), taskId = QStringLiteral("caldav:work:task-1");
        QVariantMap event = record(store.events(), eventId);
        QCOMPARE(event.value(QStringLiteral("eventId")).toString(), eventId);
        QCOMPARE(event.value(QStringLiteral("calendarId")).toString(), QStringLiteral("work"));
        QCOMPARE(event.value(QStringLiteral("remoteEtag")).toByteArray(), server.etagOf(eventPath));
        QCOMPARE(event.value(QStringLiteral("syncState")).toString(), QStringLiteral("synced"));
        QCOMPARE(record(store.tasks(), taskId).value(QStringLiteral("status")).toString(), QStringLiteral("todo"));

        result = syncNow(provider, QStringLiteral("sync-2"));                    // steady state
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(server.count(QStringLiteral("PUT")), 0);
        QCOMPARE(server.count(QStringLiteral("GET")), 2);

        // A local board move is written back with If-Match on the known ETag.
        const QByteArray taskEtag = server.etagOf(taskPath);
        QVERIFY(store.moveTask(taskId, QStringLiteral("doing")).value(QStringLiteral("ok")).toBool());
        result = syncNow(provider, QStringLiteral("sync-3"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QVERIFY(server.requests.contains(QStringLiteral("PUT ") + taskPath + QStringLiteral(" If-Match: ") + QString::fromLatin1(taskEtag)));
        QVERIFY(server.body(taskPath).contains("STATUS:IN-PROCESS\r\n"));
        QCOMPARE(record(store.tasks(), taskId).value(QStringLiteral("remoteEtag")).toByteArray(), server.etagOf(taskPath));
        syncNow(provider, QStringLiteral("sync-4"));
        QCOMPARE(server.count(QStringLiteral("PUT")), 1);                       // normalized: nothing left to push

        // A new local task for this account is created with If-None-Match: *.
        QVERIFY(store.upsert(QStringLiteral("tasks"), {{QStringLiteral("id"), QStringLiteral("task-new")},
            {QStringLiteral("title"), QStringLiteral("Write notes")}, {QStringLiteral("status"), QStringLiteral("todo")},
            {QStringLiteral("order"), 1}, {QStringLiteral("remoteAccount"), QStringLiteral("work")}}).value(QStringLiteral("ok")).toBool());
        result = syncNow(provider, QStringLiteral("sync-5"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        const QString newPath = server.path(QStringLiteral("task-new.ics"));
        QVERIFY(server.requests.contains(QStringLiteral("PUT ") + newPath + QStringLiteral(" If-None-Match: *")));
        QVERIFY(server.body(newPath).contains("SUMMARY:Write notes\r\n"));
        QCOMPARE(record(store.tasks(), QStringLiteral("task-new")).value(QStringLiteral("remoteHref")).toString(), newPath);

        // Edited on both sides: reported, nothing overwritten, then resolved.
        server.put(eventPath, eventIcs("Server title"));
        event = record(store.events(), eventId);
        event.insert(QStringLiteral("title"), QStringLiteral("Local title"));
        QVERIFY(store.upsert(QStringLiteral("events"), event).value(QStringLiteral("ok")).toBool());
        result = syncNow(provider, QStringLiteral("sync-6"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QCOMPARE(provider.conflicts().size(), 1);
        QVERIFY(server.body(eventPath).contains("SUMMARY:Server title\r\n"));
        QCOMPARE(record(store.events(), eventId).value(QStringLiteral("syncState")).toString(), QStringLiteral("conflict"));
        const QByteArray conflictEtag = server.etagOf(eventPath);
        result = change(provider, [&] { provider.resolveConflict(QStringLiteral("keep-local"), QStringLiteral("events"), eventId, QStringLiteral("local")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QVERIFY(server.requests.contains(QStringLiteral("PUT ") + eventPath + QStringLiteral(" If-Match: ") + QString::fromLatin1(conflictEtag)));
        QVERIFY(server.body(eventPath).contains("SUMMARY:Local title\r\n"));
        QVERIFY(server.body(eventPath).contains("TRIGGER:-PT15M\r\n"));
        QVERIFY(provider.conflicts().isEmpty());
        QCOMPARE(record(store.events(), eventId).value(QStringLiteral("syncState")).toString(), QStringLiteral("synced"));

        server.put(eventPath, eventIcs("Server again"));
        event = record(store.events(), eventId);
        event.insert(QStringLiteral("title"), QStringLiteral("Local again"));
        QVERIFY(store.upsert(QStringLiteral("events"), event).value(QStringLiteral("ok")).toBool());
        QCOMPARE(syncNow(provider, QStringLiteral("sync-7")).value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        result = change(provider, [&] { provider.resolveConflict(QStringLiteral("take-remote"), QStringLiteral("events"), eventId, QStringLiteral("remote")); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(record(store.events(), eventId).value(QStringLiteral("title")).toString(), QStringLiteral("Server again"));
        QVERIFY(syncNow(provider, QStringLiteral("sync-8")).value(QStringLiteral("ok")).toBool());

        // Deleted elsewhere, then deleted from here.
        server.resources.remove(taskPath);
        QVERIFY(syncNow(provider, QStringLiteral("sync-9")).value(QStringLiteral("ok")).toBool());
        QVERIFY(record(store.tasks(), taskId).isEmpty());
        result = change(provider, [&] { provider.removeRemote(QStringLiteral("remove-1"), QStringLiteral("events"), eventId); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QVERIFY(!server.resources.contains(eventPath));
        QVERIFY(record(store.events(), eventId).isEmpty());

        const QString transcript = result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n'));
        QVERIFY(transcript.contains(QStringLiteral("[credentials redacted]")));
        QVERIFY(!transcript.contains(kSecret));
        QFile saved(store.filePath());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray bytes = saved.readAll();
        QVERIFY(!bytes.contains(kSecret.toUtf8()));
        QVERIFY(!bytes.contains(QByteArray("ada:" + kSecret.toUtf8()).toBase64()));
    }

    void credentialsOfflineAndPlainHttp()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("calendar.json")));
        CalDavTestServer server;
        server.authorization = "Basic " + QByteArray("ada:right").toBase64();
        CalDavProvider provider;
        configure(provider, server.url(), &store);
        QVariantMap result = syncNow(provider, QStringLiteral("sync-a"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("401")));
        QVERIFY(!result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n')).contains(kSecret));

        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        const quint16 closed = probe.serverPort();
        probe.close();
        configure(provider, QStringLiteral("http://127.0.0.1:%1/dav/").arg(closed), &store);
        result = syncNow(provider, QStringLiteral("sync-b"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());

        configure(provider, QStringLiteral("http://calendar.example.test/dav/"), &store);
        result = syncNow(provider, QStringLiteral("sync-c"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("plain HTTP")));
        QVERIFY(store.events().isEmpty());
    }
};

QTEST_MAIN(CalDavProviderTest)
#include "tst_caldav_provider.moc"
