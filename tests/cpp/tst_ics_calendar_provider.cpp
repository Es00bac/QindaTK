// SPDX-License-Identifier: LGPL-3.0-or-later
// D-300: IcsCalendarProvider — feeds are read-only replicas (conditional GET,
// 304, redirects, size bound, malformed data, credentials), files import as
// local events idempotently by UID, and export -> import round-trips
// recurrence, EXDATE and overrides. Against IcsFeedTestServer on 127.0.0.1.
#include "caldav_provider.h"
#include "caldav_test_server.h"
#include "ics_calendar_jobs.h"
#include "ics_calendar_provider.h"
#include "ics_feed_test_server.h"

#include <QFile>
#include <QJsonDocument>
#include <QMutex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
const QString kSecret = QStringLiteral("feed-Secret-64");
QMutex logMutex;
QStringList capturedLog;
QtMessageHandler previousHandler = nullptr;

void captureMessages(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    {
        QMutexLocker lock(&logMutex);
        capturedLog.append(message);
    }
    if (previousHandler) previousHandler(type, context, message);
}

QByteArray feed(bool second = false)
{
    QByteArray ics = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Feed//EN\r\nX-WR-CALNAME:Team holidays\r\n"
        "BEGIN:VTIMEZONE\r\nTZID:/mozilla.org/20050126_1/Europe/Berlin\r\nX-LIC-LOCATION:Europe/Berlin\r\n"
        "BEGIN:STANDARD\r\nTZOFFSETFROM:+0200\r\nTZOFFSETTO:+0100\r\nDTSTART:19701025T030000\r\nEND:STANDARD\r\n"
        "END:VTIMEZONE\r\n"
        "BEGIN:VTIMEZONE\r\nTZID:Office Zone\r\nX-LIC-LOCATION:Europe/Berlin\r\nEND:VTIMEZONE\r\n"
        "BEGIN:VEVENT\r\nUID:standup@feed\r\nDTSTAMP:20260201T000000Z\r\nSUMMARY:" + QByteArray(second ? "Daily sync" : "Standup")
        + "\r\nDTSTART;TZID=/mozilla.org/20050126_1/Europe/Berlin:20260302T090000\r\n"
        "DTEND;TZID=/mozilla.org/20050126_1/Europe/Berlin:20260302T091500\r\nRRULE:FREQ=WEEKLY;BYDAY=MO;COUNT=6\r\n"
        "EXDATE;TZID=/mozilla.org/20050126_1/Europe/Berlin:20260309T090000\r\n"
        "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:Soon\r\nTRIGGER:-PT10M\r\nEND:VALARM\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:standup@feed\r\nRECURRENCE-ID;TZID=/mozilla.org/20050126_1/Europe/Berlin:20260316T090000\r\n"
        "SUMMARY:Standup (late)\r\nDTSTART;TZID=/mozilla.org/20050126_1/Europe/Berlin:20260316T100000\r\n"
        "DTEND;TZID=/mozilla.org/20050126_1/Europe/Berlin:20260316T101500\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:review@feed\r\nSUMMARY:Review\r\nDTSTART;TZID=Office Zone:20260325T140000\r\n"
        "DURATION:PT1H\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nDTSTAMP:20260201T000000Z\r\nSUMMARY:No UID\r\nDTSTART:20260401T120000Z\r\nEND:VEVENT\r\n"
        "BEGIN:VTODO\r\nUID:task@feed\r\nSUMMARY:Task\r\nEND:VTODO\r\n"
        "BEGIN:VJOURNAL\r\nUID:journal@feed\r\nEND:VJOURNAL\r\n"
        "BEGIN:VEVENT\r\nUID:broken@feed\r\nSUMMARY:Broken\r\nDTSTART;TZID=Mars/Olympus:20260401T100000\r\nEND:VEVENT\r\n";
    if (!second)
        ics += "BEGIN:VEVENT\r\nUID:offsite@feed\r\nSUMMARY:Offsite\r\nDTSTART;VALUE=DATE:20260320\r\n"
               "DTEND;VALUE=DATE:20260322\r\nEND:VEVENT\r\n";
    return ics + "END:VCALENDAR\r\n";
}

template <typename Signal>
QVariantMap run(IcsCalendarProvider &provider, Signal signal, const std::function<void()> &start)
{
    QSignalSpy spy(&provider, signal);
    start();
    return spy.wait(15000) ? spy.takeFirst().at(1).toMap() : QVariantMap();
}

QVariantMap sync(IcsCalendarProvider &provider)
{
    return run(provider, &IcsCalendarProvider::syncFinished, [&] { provider.sync(QStringLiteral("sync")); });
}

QVariantMap import(IcsCalendarProvider &provider, const QString &path, const QString &calendar)
{
    return run(provider, &IcsCalendarProvider::importFinished,
               [&] { provider.importFile(QStringLiteral("import"), path, calendar); });
}

QVariantMap eventRow(const LocalProductivityStore &store, const QString &id)
{
    for (const QVariant &value : store.events())
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    return {};
}

QString text(const QVariantMap &result)
{
    return result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n'))
        + result.value(QStringLiteral("error")).toString();
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Recurrence identity of an event: rule, exclusions and overrides by instance.
// Local times compare in one form: the store writes "yyyy-MM-ddTHH:mm:ss",
// the codec "yyyy-MM-dd HH:mm".
QString wall(QString text)
{
    text.replace(QLatin1Char(' '), QLatin1Char('T'));
    return text.size() == 16 ? text + QStringLiteral(":00") : text;
}

QStringList shape(const QVariantMap &e)
{
    QStringList out{e.value(QStringLiteral("title")).toString(), wall(e.value(QStringLiteral("startLocal")).toString()),
                    wall(e.value(QStringLiteral("endLocal")).toString()), e.value(QStringLiteral("zone")).toString(),
                    QString::fromUtf8(QJsonDocument::fromVariant(e.value(QStringLiteral("recurrenceRule"))).toJson(QJsonDocument::Compact)),
                    e.value(QStringLiteral("exceptions")).toStringList().join(QLatin1Char(','))};
    for (const QVariant &value : e.value(QStringLiteral("overrides")).toList()) {
        const QVariantMap o = value.toMap();
        out.append(wall(o.value(QStringLiteral("recurrenceId")).toString()) + QLatin1Char('=')
                   + wall(o.value(QStringLiteral("startLocal")).toString()) + QLatin1Char('/')
                   + wall(o.value(QStringLiteral("endLocal")).toString()));
    }
    std::sort(out.begin() + 6, out.end());
    return out;
}
} // namespace

class IcsCalendarProviderTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { previousHandler = qInstallMessageHandler(captureMessages); }
    void cleanupTestCase()
    {
        qInstallMessageHandler(previousHandler);
        for (const QString &line : std::as_const(capturedLog)) QVERIFY(!line.contains(kSecret));
    }

    void feedUrlsAndPaths()
    {
        QString error;
        QCOMPARE(IcsJobs::normalizedFeedUrl(QStringLiteral("webcal://cal.example.test/a.ics"), &error).toString(),
                 QStringLiteral("https://cal.example.test/a.ics"));
        QCOMPARE(IcsJobs::normalizedFeedUrl(QStringLiteral("WEBCALS://cal.example.test/a.ics#x"), &error).toString(),
                 QStringLiteral("https://cal.example.test/a.ics"));
        QVERIFY(IcsJobs::normalizedFeedUrl(QStringLiteral("ftp://cal.example.test/a.ics"), &error).isEmpty());
        QVERIFY(IcsJobs::normalizedFeedUrl(QStringLiteral("https://ada:pw@cal.example.test/a.ics"), &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("setSecret")));
        QCOMPARE(IcsJobs::localPath(QStringLiteral("file:///tmp/a.ics")), QStringLiteral("/tmp/a.ics"));
        QCOMPARE(IcsJobs::localPath(QStringLiteral("/tmp/a.ics")), QStringLiteral("/tmp/a.ics"));
        QVERIFY(IcsJobs::localPath(QStringLiteral("https://x.test/a.ics")).isEmpty());
    }

    void feedIsAReadOnlyReplicaWithConditionalGet()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        IcsFeedTestServer server;
        server.routes.insert(QStringLiteral("/feeds/team.ics"),
                             {200, feed(), "\"v1\"", "Wed, 01 Jul 2026 10:00:00 GMT", {}, false});
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("calendar.json")));
        IcsCalendarProvider provider;
        provider.setStore(&store);
        provider.setProperty("accountId", QStringLiteral("holidays"));
        provider.setProperty("calendarId", QStringLiteral("team"));
        provider.setProperty("feedUrl", server.url(QStringLiteral("/feeds/team.ics")));
        provider.setProperty("defaultZone", QStringLiteral("UTC"));
        provider.setProperty("timeoutMs", 10000);
        QVariantMap result = sync(provider);
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(result.value(QStringLiteral("added")).toInt(), 4);
        QCOMPARE(result.value(QStringLiteral("skipped")).toInt(), 2);       // the VTODO and the unknown zone
        QCOMPARE(result.value(QStringLiteral("calendarName")).toString(), QStringLiteral("Team holidays"));
        QCOMPARE(result.value(QStringLiteral("feedEtag")).toString(), QStringLiteral("\"v1\""));
        const QString warnings = result.value(QStringLiteral("warnings")).toStringList().join(QLatin1Char('\n'));
        QVERIFY(warnings.contains(QStringLiteral("VJOURNAL")));
        QVERIFY(warnings.contains(QStringLiteral("broken@feed")));
        QVERIFY(warnings.contains(QStringLiteral("without UID")));
        QCOMPARE(store.events().size(), 4);

        const QVariantMap standup = eventRow(store, QStringLiteral("ics:holidays:team:standup@feed"));
        QVERIFY(standup.value(QStringLiteral("readOnly")).toBool());
        QCOMPARE(standup.value(QStringLiteral("remoteAccount")).toString(), QStringLiteral("holidays"));
        QCOMPARE(standup.value(QStringLiteral("icsFeed")).toString(), server.url(QStringLiteral("/feeds/team.ics")));
        QCOMPARE(standup.value(QStringLiteral("feedEtag")).toString(), QStringLiteral("\"v1\""));
        QCOMPARE(standup.value(QStringLiteral("zone")).toString(), QStringLiteral("Europe/Berlin"));
        QCOMPARE(standup.value(QStringLiteral("exceptions")).toStringList(), QStringList{QStringLiteral("2026-03-09T09:00:00")});
        const QVariantList overrides = standup.value(QStringLiteral("overrides")).toList();
        QCOMPARE(overrides.size(), 1);
        QCOMPARE(overrides.first().toMap().value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-16 10:00"));
        const QString remoteIcs = standup.value(QStringLiteral("remoteIcs")).toString();
        QVERIFY(remoteIcs.contains(QStringLiteral("DTSTART;TZID=Europe/Berlin:20260302T090000")));
        QVERIFY(remoteIcs.contains(QStringLiteral("TZID:Europe/Berlin")));
        QVERIFY(remoteIcs.contains(QStringLiteral("TRIGGER:-PT10M")));
        const QVariantMap review = eventRow(store, QStringLiteral("ics:holidays:team:review@feed"));
        QCOMPARE(review.value(QStringLiteral("zone")).toString(), QStringLiteral("Europe/Berlin"));    // X-LIC-LOCATION
        QCOMPARE(review.value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-03-25 15:00"));

        // Unchanged feed: validators go out and a 304 changes nothing.
        const QString revision = store.revisionText();
        result = sync(provider);
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QVERIFY(result.value(QStringLiteral("unchanged")).toBool());
        QVERIFY(server.requests.last().contains(QStringLiteral("If-None-Match: \"v1\"")));
        QVERIFY(server.requests.last().contains(QStringLiteral("If-Modified-Since: Wed, 01 Jul 2026")));
        QCOMPARE(store.revisionText(), revision);

        // A new version replaces the replica: one edit, one removal, and the
        // UID-less event keeps its derived identity.
        server.routes.insert(QStringLiteral("/feeds/team.ics"), {200, feed(true), "\"v2\"", {}, {}, true});
        result = sync(provider);
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(result.value(QStringLiteral("added")).toInt(), 0);
        QCOMPARE(result.value(QStringLiteral("updated")).toInt(), 1);
        QCOMPARE(result.value(QStringLiteral("removed")).toInt(), 1);
        QCOMPARE(eventRow(store, QStringLiteral("ics:holidays:team:standup@feed")).value(QStringLiteral("title")).toString(),
                 QStringLiteral("Daily sync"));
        QVERIFY(eventRow(store, QStringLiteral("ics:holidays:team:offsite@feed")).isEmpty());

        // After a restart the stored validators make the request conditional.
        IcsCalendarProvider restarted;
        restarted.setStore(&store);
        restarted.setProperty("accountId", QStringLiteral("holidays"));
        restarted.setProperty("calendarId", QStringLiteral("team"));
        restarted.setProperty("feedUrl", server.url(QStringLiteral("/feeds/team.ics")));
        result = sync(restarted);
        QVERIFY(result.value(QStringLiteral("unchanged")).toBool());

        // CalDAV never uploads a feed replica, even under the same account ID.
        CalDavTestServer dav;
        CalDavProvider caldav;
        caldav.setProperty("collectionUrl", dav.url());
        caldav.setProperty("accountId", QStringLiteral("holidays"));
        caldav.setProperty("timeoutMs", 10000);
        caldav.setStore(&store);
        QSignalSpy spy(&caldav, &CalDavProvider::syncFinished);
        caldav.sync(QStringLiteral("dav"));
        QVERIFY(spy.wait(15000));
        QVERIFY(spy.takeFirst().at(1).toMap().value(QStringLiteral("ok")).toBool());
        QCOMPARE(dav.count(QStringLiteral("PUT")), 0);
        QCOMPARE(store.events().size(), 3);
    }

    void feedRedirectsBoundsFailuresAndCredentials()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        IcsFeedTestServer server;
        server.routes.insert(QStringLiteral("/team.ics"), {200, feed(), {}, {}, {}, false});
        server.routes.insert(QStringLiteral("/old.ics"), {301, {}, {}, {}, "/team.ics", false});
        server.routes.insert(QStringLiteral("/loop.ics"), {302, {}, {}, {}, "/loop.ics", false});
        server.routes.insert(QStringLiteral("/big.ics"), {200, feed() + QByteArray(4000, 'x'), {}, {}, {}, false});
        server.routes.insert(QStringLiteral("/bigchunks.ics"), {200, feed() + QByteArray(4000, 'x'), {}, {}, {}, true});
        server.routes.insert(QStringLiteral("/page.html"), {200, "<html>not a calendar</html>", {}, {}, {}, false});
        server.routes.insert(QStringLiteral("/busy.ics"), {503, {}, {}, {}, {}, false});
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("calendar.json")));
        IcsCalendarProvider provider;
        provider.setStore(&store);
        provider.setProperty("timeoutMs", 10000);
        auto syncUrl = [&](const QString &path) {
            provider.setProperty("feedUrl", server.url(path));
            return sync(provider);
        };
        QVariantMap result = syncUrl(QStringLiteral("/old.ics"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QVERIFY(text(result).contains(QStringLiteral("redirected to")));
        QCOMPARE(result.value(QStringLiteral("finalUrl")).toString(), server.url(QStringLiteral("/team.ics")));
        const int stored = store.events().size();
        QCOMPARE(stored, 4);

        result = syncUrl(QStringLiteral("/loop.ics"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("redirected more than")));
        provider.setProperty("maximumBytes", 3000);
        for (const QString &path : {QStringLiteral("/big.ics"), QStringLiteral("/bigchunks.ics")}) {
            result = syncUrl(path);
            QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        }
        QVERIFY(text(result).contains(QStringLiteral("oversized")) || text(result).contains(QStringLiteral("exceeds")));
        provider.setProperty("maximumBytes", 16 * 1024 * 1024);
        result = syncUrl(QStringLiteral("/page.html"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("not an iCalendar")));
        result = syncUrl(QStringLiteral("/missing.ics"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        result = syncUrl(QStringLiteral("/busy.ics"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());
        QCOMPARE(store.events().size(), stored);                           // failures change nothing

        // A private feed: refused without the secret, served with it.
        server.authorization = "Basic " + QByteArray("ada:" + kSecret.toUtf8()).toBase64();
        provider.setProperty("userName", QStringLiteral("ada"));
        result = syncUrl(QStringLiteral("/team.ics"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("401")));
        provider.setSecret(kSecret);
        result = syncUrl(QStringLiteral("/team.ics"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QVERIFY(server.requests.last().endsWith(QStringLiteral("[auth]")));
        QVERIFY(text(result).contains(QStringLiteral("[credentials redacted]")));
        QVERIFY(!text(result).contains(kSecret));
        QVERIFY(!text(result).contains(QString::fromLatin1(QByteArray("ada:" + kSecret.toUtf8()).toBase64())));
        QVERIFY(store.save());
        QVERIFY(!readFile(store.filePath()).contains(kSecret.toUtf8()));

        // Credentials never travel over plain HTTP to a remote host.
        const qsizetype before = server.requests.size();
        provider.setProperty("feedUrl", QStringLiteral("http://calendar.example.test/team.ics"));
        result = sync(provider);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("plain HTTP")));
        QCOMPARE(server.requests.size(), before);
    }

    void importIsIdempotentByUidAndExportRoundTrips()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("team.ics"));
        QVERIFY(writeFile(source, feed()));
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("calendar.json")));
        IcsCalendarProvider provider;
        provider.setStore(&store);
        provider.setProperty("defaultZone", QStringLiteral("UTC"));
        QVariantMap result = import(provider, source, QStringLiteral("personal"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(result.value(QStringLiteral("imported")).toInt(), 4);
        QCOMPARE(result.value(QStringLiteral("skipped")).toInt(), 2);
        const QString id = QStringLiteral("import:personal:standup@feed");
        QVariantMap standup = eventRow(store, id);
        QVERIFY(standup.value(QStringLiteral("remoteAccount")).toString().isEmpty());
        QVERIFY(!standup.value(QStringLiteral("readOnly")).toBool());
        QCOMPARE(standup.value(QStringLiteral("remoteUid")).toString(), QStringLiteral("standup@feed"));

        // Same file again (as a file: URL): nothing duplicates or changes.
        result = import(provider, QUrl::fromLocalFile(source).toString(), QStringLiteral("personal"));
        QCOMPARE(result.value(QStringLiteral("imported")).toInt(), 0);
        QCOMPARE(result.value(QStringLiteral("updated")).toInt(), 0);
        QCOMPARE(result.value(QStringLiteral("unchanged")).toInt(), 4);
        QCOMPARE(store.events().size(), 4);
        QVERIFY(writeFile(source, feed(true)));
        result = import(provider, source, QStringLiteral("personal"));
        QCOMPARE(result.value(QStringLiteral("updated")).toInt(), 1);
        QCOMPARE(store.events().size(), 4);                                // import never deletes
        QVERIFY(writeFile(source, feed()));

        // Local edits: move one more occurrence, exclude another.
        QVERIFY(store.shiftEventLocal(id, 30, QStringLiteral("occurrence"), id + QStringLiteral("/2026-03-23T09:00:00"))
                    .value(QStringLiteral("ok")).toBool());
        QVERIFY(store.setOccurrenceExcluded(id, id + QStringLiteral("/2026-03-30T09:00:00"), true).value(QStringLiteral("ok")).toBool());
        standup = eventRow(store, id);
        const QString exported = directory.filePath(QStringLiteral("out.ics"));
        result = run(provider, &IcsCalendarProvider::exportFinished,
                     [&] { provider.exportFile(QStringLiteral("export"), exported, QStringLiteral("personal")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(result.value(QStringLiteral("exported")).toInt(), 4);
        const QByteArray bytes = readFile(exported);
        QVERIFY(bytes.startsWith("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:"));
        QCOMPARE(bytes.count("END:VCALENDAR"), 1);
        QCOMPARE(bytes.count("BEGIN:VTIMEZONE"), 1);                       // Europe/Berlin once
        QVERIFY(bytes.contains("RRULE:FREQ=WEEKLY;BYDAY=MO;COUNT=6\r\n"));
        QVERIFY(bytes.contains("EXDATE;TZID=Europe/Berlin:20260309T090000\r\n"));
        QVERIFY(bytes.contains("RECURRENCE-ID;TZID=Europe/Berlin:20260323T090000\r\n"));
        QVERIFY(bytes.contains("TRIGGER:-PT10M"));
        QVERIFY(bytes.contains("UID:standup@feed\r\n"));

        LocalProductivityStore copy;
        IcsCalendarProvider reader;
        reader.setStore(&copy);
        reader.setProperty("autoSave", false);
        reader.setProperty("defaultZone", QStringLiteral("UTC"));
        result = import(reader, exported, QStringLiteral("copy"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(result.value(QStringLiteral("imported")).toInt(), 4);
        for (const QVariant &value : store.events()) {
            const QVariantMap original = value.toMap();
            const QVariantMap back = eventRow(copy, QStringLiteral("import:copy:") + original.value(QStringLiteral("remoteUid")).toString());
            QCOMPARE(shape(back), shape(original));
        }

        result = run(provider, &IcsCalendarProvider::exportFinished, [&] {
            provider.exportFile(QStringLiteral("export"), exported, QStringLiteral("personal"), {id});
        });
        QCOMPARE(result.value(QStringLiteral("exported")).toInt(), 1);
        for (const auto &[path, calendar] : {std::pair{exported, QStringLiteral("empty")},
                                             std::pair{directory.filePath(QStringLiteral("no/dir/x.ics")), QStringLiteral("personal")}}) {
            result = run(provider, &IcsCalendarProvider::exportFinished,
                         [&] { provider.exportFile(QStringLiteral("export"), path, calendar); });
            QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        }
        result = import(provider, directory.filePath(QStringLiteral("missing.ics")), QStringLiteral("personal"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
    }

    void importDoesNotTouchAccountEventsAndParseText()
    {
        LocalProductivityStore store;
        QVERIFY(store.upsert(QStringLiteral("events"), {{QStringLiteral("id"), QStringLiteral("feed-copy")},
            {QStringLiteral("calendarId"), QStringLiteral("team")}, {QStringLiteral("remoteUid"), QStringLiteral("review@feed")},
            {QStringLiteral("remoteAccount"), QStringLiteral("holidays")}, {QStringLiteral("readOnly"), true},
            {QStringLiteral("title"), QStringLiteral("Server copy")}}).value(QStringLiteral("ok")).toBool());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("team.ics"));
        QVERIFY(writeFile(source, feed()));
        IcsCalendarProvider provider;
        provider.setStore(&store);
        provider.setProperty("autoSave", false);
        const QVariantMap result = import(provider, source, QStringLiteral("team"));
        QCOMPARE(result.value(QStringLiteral("imported")).toInt(), 3);
        QCOMPARE(result.value(QStringLiteral("skipped")).toInt(), 3);
        QVERIFY(result.value(QStringLiteral("warnings")).toStringList().join(QLatin1Char('\n'))
                    .contains(QStringLiteral("review@feed: an account event")));
        QCOMPARE(eventRow(store, QStringLiteral("feed-copy")).value(QStringLiteral("title")).toString(), QStringLiteral("Server copy"));

        // parseText and serializeEvents never touch the store.
        const QString revision = store.revisionText();
        const QVariantMap parsed = provider.parseText(QString::fromUtf8(feed()));
        QVERIFY(parsed.value(QStringLiteral("ok")).toBool());
        QCOMPARE(parsed.value(QStringLiteral("events")).toList().size(), 4);     // the zone-less one is skipped
        QCOMPARE(parsed.value(QStringLiteral("tasks")).toList().size(), 1);
        const QVariantMap written = provider.serializeEvents(parsed.value(QStringLiteral("events")).toList());
        QCOMPARE(written.value(QStringLiteral("count")).toInt(), 4);
        const QVariantMap again = provider.parseText(written.value(QStringLiteral("text")).toString());
        QCOMPARE(again.value(QStringLiteral("events")).toList().size(), 4);
        QVERIFY(!provider.parseText(QStringLiteral("hello")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(store.revisionText(), revision);
    }
};

QTEST_MAIN(IcsCalendarProviderTest)
#include "tst_ics_calendar_provider.moc"
