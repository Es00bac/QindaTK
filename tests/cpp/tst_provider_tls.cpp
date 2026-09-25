// SPDX-License-Identifier: LGPL-3.0-or-later
// D-187: the providers' TLS paths against local TLS servers that present a
// certificate from a test CA (tests/fixtures/tls). Implicit TLS and STARTTLS
// succeed only when the CA is trusted; an untrusted certificate fails the
// handshake before any credential is written. Also covers the IMAP XOAUTH2
// and CalDAV bearer mechanisms that host adapters use (D-189).
#include "caldav_provider.h"
#include "caldav_test_server.h"
#include "imap_mail_provider.h"
#include "local_productivity_store.h"
#include "mail_test_servers.h"
#include "smtp_mail_provider.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
const QString kSecret = QStringLiteral("tls-Secret-42");
const QString kToken = QStringLiteral("ya29.oauth-Token_5");

const QVariantMap kDraft{{QStringLiteral("to"), QStringLiteral("bob@example.test")},
                         {QStringLiteral("subject"), QStringLiteral("Over TLS")},
                         {QStringLiteral("body"), QStringLiteral("Encrypted submission")}};

const QByteArray kMessage = "From: Bob <bob@example.test>\r\nTo: ada@example.test\r\nSubject: Hello\r\n"
                            "Message-ID: <m1@example.test>\r\nDate: Thu, 24 Sep 2026 10:00:00 +0000\r\n\r\nHi Ada\r\n";

void configureSmtp(SmtpMailProvider &provider, quint16 port, const QString &security)
{
    provider.setProperty("host", QStringLiteral("127.0.0.1"));
    provider.setProperty("port", port);
    provider.setProperty("security", security);
    provider.setProperty("userName", QStringLiteral("ada"));
    provider.setProperty("fromAddress", QStringLiteral("ada@example.test"));
    provider.setProperty("timeoutMs", 10000);
    provider.setSecret(kSecret);
}

QVariantMap send(SmtpMailProvider &provider, const QString &requestId)
{
    QSignalSpy spy(&provider, &SmtpMailProvider::sendFinished);
    provider.send(requestId, kDraft);
    return spy.wait(15000) ? spy.takeFirst().at(1).toMap() : QVariantMap();
}

QVariantMap syncImap(ImapMailProvider &provider, const QString &requestId)
{
    QSignalSpy spy(&provider, &ImapMailProvider::syncFinished);
    provider.sync(requestId);
    return spy.wait(15000) ? spy.takeFirst().at(1).toMap() : QVariantMap();
}

QString text(const QVariantMap &result)
{
    return result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n'))
        + result.value(QStringLiteral("error")).toString();
}

bool anyStartsWith(const QStringList &lines, const QString &prefix)
{
    for (const QString &line : lines) if (line.startsWith(prefix)) return true;
    return false;
}
} // namespace

class ProviderTlsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!TestTls::available()) QSKIP("No TLS backend or fixture certificates");
    }
    void cleanup() { TestTls::trustTestCa(false); }

    void smtpStartTlsAuthenticatesOnlyAfterHandshake()
    {
        TestTls::trustTestCa(true);
        SmtpTestServer server;
        server.user = QStringLiteral("ada");
        server.secret = kSecret;
        server.enableTls(false);
        server.offerStartTls = true;
        server.requireTlsForAuth = true;
        SmtpMailProvider provider;
        configureSmtp(provider, server.port(), QStringLiteral("starttls"));
        const QVariantMap result = send(provider, QStringLiteral("send-starttls"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(server.messages.size(), 1);
        QVERIFY(text(result).contains(QStringLiteral("* TLS established")));
        // The plaintext part of the session ends at STARTTLS; AUTH, the
        // envelope and the message travel only inside TLS.
        QVERIFY(server.log.indexOf(QStringLiteral("STARTTLS")) >= 0);
        QVERIFY(anyStartsWith(server.encryptedLog, QStringLiteral("AUTH PLAIN")));
        QVERIFY(anyStartsWith(server.encryptedLog, QStringLiteral("MAIL FROM:")));
        QVERIFY(!text(result).contains(kSecret));
    }

    void smtpImplicitTlsAndUntrustedCertificate()
    {
        SmtpTestServer server;
        server.user = QStringLiteral("ada");
        server.secret = kSecret;
        server.enableTls(true);
        SmtpMailProvider provider;
        configureSmtp(provider, server.port(), QStringLiteral("tls"));

        // Without the test CA the certificate is untrusted: the handshake
        // fails, the result is rejected (not retryable) and nothing is sent.
        QVariantMap result = send(provider, QStringLiteral("send-untrusted"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("TLS handshake failed")));
        QVERIFY(server.log.isEmpty());
        QVERIFY(server.messages.isEmpty());

        TestTls::trustTestCa(true);
        result = send(provider, QStringLiteral("send-trusted"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(server.messages.size(), 1);
        QCOMPARE(server.encryptedLog, server.log);            // every client line was encrypted
    }

    void imapImplicitTlsWithXoauth2()
    {
        TestTls::trustTestCa(true);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("mail.json")));
        ImapTestServer server;
        server.user = QStringLiteral("ada");
        server.secret = kToken;
        server.capabilities = {QStringLiteral("IMAP4rev1"), QStringLiteral("AUTH=XOAUTH2"), QStringLiteral("SASL-IR"),
                               QStringLiteral("UIDPLUS"), QStringLiteral("MOVE")};
        server.implicitTlsOnly();
        server.add(1, kMessage);
        ImapMailProvider provider;
        provider.setProperty("host", QStringLiteral("127.0.0.1"));
        provider.setProperty("port", server.port());
        provider.setProperty("security", QStringLiteral("tls"));
        provider.setProperty("userName", QStringLiteral("ada"));
        provider.setProperty("accountId", QStringLiteral("work"));
        provider.setProperty("timeoutMs", 10000);
        provider.setStore(&store);

        // An expired token: the server's XOAUTH2 error challenge is answered
        // with an empty line and the result is a rejection the host adapter
        // can retry after refreshing the token.
        provider.setSecret(QStringLiteral("expired"), QStringLiteral("xoauth2"));
        QVariantMap result = syncImap(provider, QStringLiteral("sync-expired"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("AUTHENTICATIONFAILED")));
        QVERIFY(store.messages().isEmpty());

        provider.setSecret(kToken, QStringLiteral("xoauth2"));
        result = syncImap(provider, QStringLiteral("sync-token"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QVERIFY(server.authenticatedOverTls);
        QCOMPARE(store.messages().size(), 1);
        QVERIFY(text(result).contains(QStringLiteral("AUTHENTICATE XOAUTH2 <redacted>")));
        QVERIFY(!text(result).contains(kToken));
        const QByteArray payload = QByteArray("user=ada\x01" "auth=Bearer " + kToken.toUtf8() + "\x01\x01").toBase64();
        QVERIFY(!text(result).contains(QString::fromLatin1(payload)));
        QFile saved(store.filePath());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(!saved.readAll().contains(kToken.toUtf8()));
    }

    void imapStartTlsUpgradesBeforeLogin()
    {
        TestTls::trustTestCa(true);
        LocalProductivityStore store;
        ImapTestServer server;
        server.user = QStringLiteral("ada");
        server.secret = kSecret;
        server.enableTls(false);
        server.add(7, kMessage);
        ImapMailProvider provider;
        provider.setProperty("host", QStringLiteral("127.0.0.1"));
        provider.setProperty("port", server.port());
        provider.setProperty("security", QStringLiteral("starttls"));
        provider.setProperty("userName", QStringLiteral("ada"));
        provider.setProperty("accountId", QStringLiteral("work"));
        provider.setProperty("timeoutMs", 10000);
        provider.setProperty("autoSave", false);
        provider.setStore(&store);
        provider.setSecret(kSecret);
        const QVariantMap result = syncImap(provider, QStringLiteral("sync-starttls"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(store.messages().size(), 1);
        QVERIFY(anyStartsWith(server.log, QStringLiteral("a2 STARTTLS")));
        QVERIFY(anyStartsWith(server.encryptedLog, QStringLiteral("a4 AUTHENTICATE PLAIN")));
        for (const QString &line : std::as_const(server.log))
            if (line.contains(QStringLiteral("AUTHENTICATE"))) QVERIFY(server.encryptedLog.contains(line));
    }

    void caldavHttpsWithBearerToken()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        CalDavTestServer server;
        server.enableTls();
        server.authorization = "Bearer " + kToken.toUtf8();
        server.put(server.path(QStringLiteral("task.ics")),
                   "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VTODO\r\nUID:task\r\nSUMMARY:Pack cables\r\n"
                   "STATUS:NEEDS-ACTION\r\nEND:VTODO\r\nEND:VCALENDAR\r\n");
        QVERIFY(server.url().startsWith(QStringLiteral("https://127.0.0.1:")));
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("calendar.json")));
        CalDavProvider provider;
        provider.setProperty("collectionUrl", server.url());
        provider.setProperty("accountId", QStringLiteral("work"));
        provider.setProperty("timeoutMs", 10000);
        provider.setStore(&store);
        provider.setSecret(kToken, QStringLiteral("bearer"));
        QSignalSpy spy(&provider, &CalDavProvider::syncFinished);

        provider.sync(QStringLiteral("sync-untrusted"));
        QVERIFY(spy.wait(15000));
        QVariantMap result = spy.takeFirst().at(1).toMap();
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(server.requests.isEmpty());

        TestTls::trustTestCa(true);
        provider.sync(QStringLiteral("sync-trusted"));
        QVERIFY(spy.wait(15000));
        result = spy.takeFirst().at(1).toMap();
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(store.tasks().size(), 1);
        QVERIFY(!text(result).contains(kToken));
    }
};

QTEST_MAIN(ProviderTlsTest)
#include "tst_provider_tls.moc"
