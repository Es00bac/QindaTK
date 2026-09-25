// SPDX-License-Identifier: LGPL-3.0-or-later
// CalDavProvider::discoverCalendars (RFC 6764 well-known redirect, RFC 5397
// current-user-principal, RFC 4791 calendar-home-set) against the in-process
// CalDAV server, with basic and bearer credentials; the site and plain-HTTP
// guards; and the multistatus parsing helpers.
#include "caldav_discovery.h"
#include "caldav_provider.h"
#include "caldav_test_server.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
const QString kSecret = QStringLiteral("discover-Secret-8");

void layout(CalDavTestServer &server)
{
    server.principal = QStringLiteral("/dav/principals/ada/");
    server.homeSet = QStringLiteral("/dav/calendars/ada/");
    server.calendars = {{QStringLiteral("/dav/calendars/ada/work/"), QStringLiteral("Work"), QStringLiteral("#FF8800FF"),
                         QStringLiteral("Team calendar"), {QStringLiteral("VEVENT"), QStringLiteral("VTODO")}, false},
                        {QStringLiteral("/dav/calendars/ada/holidays/"), {}, QStringLiteral("#0a0b0c"), {},
                         {QStringLiteral("VEVENT")}, true}};
}

QVariantMap discover(CalDavProvider &provider, const QString &url)
{
    QSignalSpy spy(&provider, &CalDavProvider::discoveryFinished);
    provider.discoverCalendars(QStringLiteral("discover"), url);
    return spy.wait(15000) ? spy.takeFirst().at(1).toMap() : QVariantMap();
}

QVariantMap calendar(const QVariantMap &result, const QString &url)
{
    for (const QVariant &value : result.value(QStringLiteral("calendars")).toList())
        if (value.toMap().value(QStringLiteral("url")).toString() == url) return value.toMap();
    return {};
}

QString text(const QVariantMap &result)
{
    return result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n'))
        + result.value(QStringLiteral("error")).toString();
}
} // namespace

class CalDavDiscoveryTest : public QObject {
    Q_OBJECT
private slots:
    void helpers()
    {
        QCOMPARE(CalDavDiscovery::normalizedColor(QStringLiteral("#FF8800FF")), QStringLiteral("#ff8800"));
        QCOMPARE(CalDavDiscovery::normalizedColor(QStringLiteral("#0a0b0c")), QStringLiteral("#0a0b0c"));
        QCOMPARE(CalDavDiscovery::normalizedColor(QStringLiteral("red")), QString());
        QVERIFY(CalDavDiscovery::sameSite(QStringLiteral("caldav.icloud.com"), QStringLiteral("p42-caldav.icloud.com")));
        QVERIFY(CalDavDiscovery::sameSite(QStringLiteral("example.com"), QStringLiteral("dav.example.com")));
        QVERIFY(CalDavDiscovery::sameSite(QStringLiteral("127.0.0.1"), QStringLiteral("localhost")));
        QVERIFY(!CalDavDiscovery::sameSite(QStringLiteral("example.com"), QStringLiteral("evil.com")));
        QVERIFY(!CalDavDiscovery::sameSite(QStringLiteral("caldav.example.com"), QStringLiteral("caldav.example.com.evil.net")));
        QVERIFY(!CalDavDiscovery::sameSite(QStringLiteral("10.0.0.5"), QStringLiteral("10.0.0.6")));
        QString error;
        const QList<CalDavDiscovery::Resource> parsed = CalDavDiscovery::parseMultistatus(
            "<d:multistatus xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:response><d:href>/c/</d:href>"
            "<d:propstat><d:prop><d:resourcetype><d:collection/><c:calendar/></d:resourcetype></d:prop>"
            "<d:status>HTTP/1.1 200 OK</d:status></d:propstat><d:propstat><d:prop><d:displayname>Hidden</d:displayname>"
            "</d:prop><d:status>HTTP/1.1 404 Not Found</d:status></d:propstat></d:response></d:multistatus>", &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(parsed.size(), 1);
        QCOMPARE(parsed.first().types, QStringList({QStringLiteral("collection"), QStringLiteral("calendar")}));
        QVERIFY(parsed.first().displayName.isEmpty());                 // only 200 propstats count
        CalDavDiscovery::parseMultistatus("<d:multistatus xmlns:d=\"DAV:\"><d:response>", &error);
        QVERIFY(!error.isEmpty());
    }

    void wellKnownPrincipalAndHomeSetWithBasic()
    {
        CalDavTestServer server;
        layout(server);
        server.wellKnown = true;
        server.authorization = "Basic " + QByteArray("ada:" + kSecret.toUtf8()).toBase64();
        CalDavProvider provider;
        provider.setProperty("userName", QStringLiteral("ada"));
        provider.setProperty("timeoutMs", 10000);
        provider.setSecret(kSecret);
        // A server root: "/" is not a DAV resource, so the client asks the
        // well-known URI, follows its redirect, then the principal.
        const QVariantMap result = discover(provider, server.root());
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(result.value(QStringLiteral("requestId")).toString(), QStringLiteral("discover"));
        QCOMPARE(server.count(QStringLiteral("PROPFIND /.well-known/caldav")), 1);
        QVERIFY(text(result).contains(QStringLiteral("* redirected to ") + server.root() + QStringLiteral("/dav/")));
        QCOMPARE(result.value(QStringLiteral("principalUrl")).toString(), server.root() + server.principal);
        QCOMPARE(result.value(QStringLiteral("homeSetUrl")).toString(), server.root() + server.homeSet);
        QCOMPARE(result.value(QStringLiteral("calendars")).toList().size(), 2);          // the schedule inbox is not one
        const QVariantMap work = calendar(result, server.url());
        QCOMPARE(work.value(QStringLiteral("displayName")).toString(), QStringLiteral("Work"));
        QCOMPARE(work.value(QStringLiteral("color")).toString(), QStringLiteral("#ff8800"));
        QCOMPARE(work.value(QStringLiteral("description")).toString(), QStringLiteral("Team calendar"));
        QCOMPARE(work.value(QStringLiteral("components")).toStringList(),
                 QStringList({QStringLiteral("VEVENT"), QStringLiteral("VTODO")}));
        QVERIFY(!work.value(QStringLiteral("readOnly")).toBool());
        const QVariantMap holidays = calendar(result, server.root() + QStringLiteral("/dav/calendars/ada/holidays/"));
        QCOMPARE(holidays.value(QStringLiteral("displayName")).toString(), QStringLiteral("holidays"));
        QVERIFY(holidays.value(QStringLiteral("readOnly")).toBool());
        QCOMPARE(holidays.value(QStringLiteral("components")).toStringList(), QStringList{QStringLiteral("VEVENT")});
        QVERIFY(text(result).contains(QStringLiteral("[credentials redacted]")));
        QVERIFY(!text(result).contains(kSecret));

        // The discovered URL is what sync takes.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("calendar.json")));
        server.put(server.path(QStringLiteral("a.ics")), "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:a\r\n"
                   "SUMMARY:Found\r\nDTSTART:20260901T100000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
        provider.setProperty("collectionUrl", work.value(QStringLiteral("url")));
        provider.setProperty("accountId", QStringLiteral("work"));
        provider.setStore(&store);
        QSignalSpy spy(&provider, &CalDavProvider::syncFinished);
        provider.sync(QStringLiteral("sync"));
        QVERIFY(spy.wait(15000));
        QVERIFY(spy.takeFirst().at(1).toMap().value(QStringLiteral("ok")).toBool());
        QCOMPARE(store.events().size(), 1);
    }

    void bearerPrincipalAbsoluteHomeSetAndCollection()
    {
        // Google's shape: the start URL is the principal, the home set comes
        // back as an absolute URL, and the token is a bearer.
        CalDavTestServer server;
        server.principal = QStringLiteral("/caldav/v2/ada/user");
        server.homeSet = QStringLiteral("/caldav/v2/ada/");
        server.absoluteHomeSet = true;
        server.calendars = {{QStringLiteral("/caldav/v2/ada/events/"), QStringLiteral("ada@example.test"), {}, {},
                             {QStringLiteral("VEVENT")}, false}};
        const QString token = QStringLiteral("ya29.discover-Token");
        server.authorization = "Bearer " + token.toUtf8();
        CalDavProvider provider;
        provider.setProperty("timeoutMs", 10000);
        provider.setSecret(QStringLiteral("expired"), QStringLiteral("bearer"));
        QVariantMap result = discover(provider, server.root() + server.principal);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("401")));
        QVERIFY(!result.value(QStringLiteral("retryable")).toBool());

        provider.setSecret(token, QStringLiteral("bearer"));
        result = discover(provider, server.root() + server.principal);
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(server.count(QStringLiteral("PROPFIND /.well-known")), 0);
        QCOMPARE(result.value(QStringLiteral("homeSetUrl")).toString(), server.root() + server.homeSet);
        QCOMPARE(result.value(QStringLiteral("calendars")).toList().size(), 1);
        QCOMPARE(calendar(result, server.root() + QStringLiteral("/caldav/v2/ada/events/"))
                     .value(QStringLiteral("displayName")).toString(), QStringLiteral("ada@example.test"));
        QVERIFY(!text(result).contains(token));

        // A collection URL is answered by itself; collectionUrl is the default.
        provider.setProperty("collectionUrl", server.root() + QStringLiteral("/caldav/v2/ada/events/"));
        result = discover(provider, {});
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(result.value(QStringLiteral("calendars")).toList().size(), 1);
        QCOMPARE(result.value(QStringLiteral("principalUrl")).toString(), server.root() + server.principal);
    }

    void guardsRedirectsAndOffline()
    {
        CalDavProvider provider;
        provider.setProperty("userName", QStringLiteral("ada"));
        provider.setProperty("timeoutMs", 10000);
        provider.setSecret(kSecret);
        QVariantMap result = discover(provider, QStringLiteral("http://calendar.example.test/"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("plain HTTP")));
        result = discover(provider, QStringLiteral("ftp://calendar.example.test/"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));

        // The well-known redirect points at another site: credentials stay here.
        CalDavTestServer server;
        layout(server);
        server.wellKnown = true;
        server.wellKnownTarget = "https://calendar.example.test/dav/";
        result = discover(provider, server.root());
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("another site")));
        // A redirect loop ends at the bound.
        server.wellKnownTarget = "/.well-known/caldav";
        result = discover(provider, server.root());
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("redirects")));
        // No DAV service at all.
        server.wellKnown = false;
        result = discover(provider, server.root());
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("No CalDAV service")));

        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        const quint16 closed = probe.serverPort();
        probe.close();
        result = discover(provider, QStringLiteral("http://127.0.0.1:%1/").arg(closed));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());
        QVERIFY(!text(result).contains(kSecret));
    }
};

QTEST_MAIN(CalDavDiscoveryTest)
#include "tst_caldav_discovery.moc"
