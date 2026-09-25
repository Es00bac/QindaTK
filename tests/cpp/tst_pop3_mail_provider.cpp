// SPDX-License-Identifier: LGPL-3.0-or-later
// Pop3MailProvider (D-298) against the in-process POP3 server
// (pop3_test_server.h): UIDL-idempotent download, local-is-the-mailbox
// actions, deletion strictly after the local save, retention, TOP for
// oversized mail, SASL/USER authentication, STLS policy (D-187) and redaction
// of the secret from results, transcripts, log output and the saved store.
#include "pop3_mail_jobs.h"
#include "pop3_mail_provider.h"
#include "pop3_session.h"
#include "pop3_test_server.h"

#include <QFile>
#include <QMutex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
const QString kSecret = QStringLiteral("pop3-Secret-91");
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

const QByteArray kPlainMessage =
    "From: \"Bob Builder\" <bob@example.test>\r\n"
    "To: ada@example.test\r\n"
    "Subject: =?UTF-8?B?w5xiZXJzaWNodA==?=\r\n"
    "Date: Thu, 24 Sep 2026 09:15:00 -0600\r\n"
    "Message-ID: <root-1@example.test>\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "\r\n"
    "Totals below.\r\n"
    ".hidden line starts with a dot\r\n"
    "..two dots\r\n";

const QByteArray kAttachmentMessage =
    "From: Carol <carol@example.test>\r\n"
    "To: ada@example.test\r\n"
    "Subject: Re: Totals\r\n"
    "Message-ID: <reply-2@example.test>\r\n"
    "In-Reply-To: <root-1@example.test>\r\n"
    "References: <root-1@example.test>\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: multipart/mixed; boundary=\"b1\"\r\n"
    "\r\n"
    "--b1\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "\r\n"
    "See the attached file.\r\n"
    "--b1\r\n"
    "Content-Type: text/csv; name=\"report.csv\"\r\n"
    "Content-Disposition: attachment; filename=\"report.csv\"\r\n"
    "Content-Transfer-Encoding: base64\r\n"
    "\r\n"
    "aWQsdG90YWwKMSw0Mgo=\r\n"
    "--b1--\r\n";

QByteArray bigMessage()
{
    return "From: dave@example.test\r\nTo: ada@example.test\r\nSubject: Huge scan\r\n"
           "Message-ID: <big-3@example.test>\r\n\r\n" + QByteArray(2000, 'x') + "\r\n";
}

void configure(Pop3MailProvider &provider, quint16 port, LocalProductivityStore *store,
               LocalAttachmentStore *attachments = nullptr)
{
    provider.setProperty("host", QStringLiteral("127.0.0.1"));
    provider.setProperty("port", port);
    provider.setProperty("security", QStringLiteral("none"));
    provider.setProperty("userName", QStringLiteral("ada"));
    provider.setProperty("accountId", QStringLiteral("home"));
    provider.setProperty("timeoutMs", 10000);
    provider.setStore(store);
    provider.setAttachmentStore(attachments);
    provider.setSecret(kSecret);
}

template <typename Signal>
QVariantMap waitFor(Pop3MailProvider &provider, Signal signal, const std::function<void()> &start)
{
    QSignalSpy spy(&provider, signal);
    start();
    if (!spy.wait(15000)) return {};
    return spy.takeFirst().at(1).toMap();
}

QVariantMap sync(Pop3MailProvider &provider, const QString &id)
{
    return waitFor(provider, &Pop3MailProvider::syncFinished, [&] { provider.sync(id); });
}

QVariantMap act(Pop3MailProvider &provider, const QString &messageId, const QString &action)
{
    return waitFor(provider, &Pop3MailProvider::actionFinished,
                   [&] { provider.applyAction(QStringLiteral("act-") + action, messageId, action); });
}

QVariantMap fetch(Pop3MailProvider &provider, const QString &messageId, const QString &attachmentId)
{
    return waitFor(provider, &Pop3MailProvider::attachmentFinished,
                   [&] { provider.fetchAttachment(QStringLiteral("att"), messageId, attachmentId); });
}

QVariantMap message(const LocalProductivityStore &store, const QString &id)
{
    for (const QVariant &value : store.messages())
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    return {};
}

QString everything(const QVariantMap &result)
{
    QString text;
    for (auto it = result.cbegin(); it != result.cend(); ++it)
        text += it.key() + QLatin1Char('=') + (it.value().canConvert<QStringList>()
                                                    ? it.value().toStringList().join(QLatin1Char('\n'))
                                                    : it.value().toString()) + QLatin1Char('\n');
    return text;
}

QString error(const QVariantMap &result)
{
    return result.value(QStringLiteral("error")).toString()
        + result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n'));
}

bool fileHas(const QString &path, const QByteArray &needle)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.readAll().contains(needle);
}

quint16 closedPort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

Pop3TestServer *newServer(const QString &secret = kSecret)
{
    auto *server = new Pop3TestServer;
    server->user = QStringLiteral("ada");
    server->secret = secret;
    return server;
}
} // namespace

class Pop3MailProviderTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { previousHandler = qInstallMessageHandler(captureMessages); }
    void cleanupTestCase()
    {
        qInstallMessageHandler(previousHandler);
        // D-187: nothing the provider logged during the whole run holds a secret.
        QMutexLocker lock(&logMutex);
        const QString log = capturedLog.join(QLatin1Char('\n'));
        QVERIFY(!log.contains(kSecret));
        QVERIFY(!log.contains(QStringLiteral("oauth-Token")));
    }

    void identifiersAndUidlValidation()
    {
        QVERIFY(Pop3::validUidl("abc-123"));
        QVERIFY(!Pop3::validUidl(""));
        QVERIFY(!Pop3::validUidl("has space"));
        QVERIFY(!Pop3::validUidl(QByteArray(71, 'a')));
        QCOMPARE(Pop3Jobs::recordId(QStringLiteral("home"), QStringLiteral("a:b/c%")),
                 QStringLiteral("pop3:home:a%3Ab%2Fc%25"));
    }

    void downloadIsIdempotentAndActionsAreLocal()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        std::unique_ptr<Pop3TestServer> server(newServer());
        server->sasl.clear();                       // USER/PASS only
        server->add("uid-1", kPlainMessage);
        server->add("uid:2/x", kAttachmentMessage);
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("mail.json")));
        LocalAttachmentStore attachments;
        attachments.setRootPath(directory.filePath(QStringLiteral("attachments")));
        Pop3MailProvider provider;
        configure(provider, server->port(), &store, &attachments);

        QVariantMap result = sync(provider, QStringLiteral("sync-1"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(error(result)));
        QCOMPARE(result.value(QStringLiteral("requestId")).toString(), QStringLiteral("sync-1"));
        QCOMPARE(result.value(QStringLiteral("addedCount")).toInt(), 2);
        QCOMPARE(result.value(QStringLiteral("deletedCount")).toInt(), 0);
        QVERIFY(!result.contains(QStringLiteral("added")));
        const QString transcript = result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n'));
        QVERIFY(transcript.contains(QStringLiteral("C: PASS <redacted>")));
        QVERIFY(transcript.contains(QStringLiteral("bytes}")));
        QVERIFY(!transcript.contains(QStringLiteral("Totals below")));   // bodies are byte counts
        QVERIFY(!everything(result).contains(kSecret));

        const QString id1 = QStringLiteral("pop3:home:uid-1");
        const QString id2 = QStringLiteral("pop3:home:uid%3A2%2Fx");
        const QVariantMap first = message(store, id1);
        QCOMPARE(first.value(QStringLiteral("messageId")).toString(), id1);
        QCOMPARE(first.value(QStringLiteral("remoteUidl")).toString(), QStringLiteral("uid-1"));
        QCOMPARE(first.value(QStringLiteral("remoteAccount")).toString(), QStringLiteral("home"));
        QCOMPARE(first.value(QStringLiteral("remoteMailbox")).toString(), QStringLiteral("INBOX"));
        QCOMPARE(first.value(QStringLiteral("localMailbox")).toString(), QStringLiteral("INBOX"));
        QVERIFY(first.value(QStringLiteral("remoteOnServer")).toBool());
        QVERIFY(QDateTime::fromString(first.value(QStringLiteral("remoteFetchedAt")).toString(), Qt::ISODate).isValid());
        QCOMPARE(first.value(QStringLiteral("subject")).toString(), QStringLiteral("Übersicht"));
        QCOMPARE(first.value(QStringLiteral("sender")).toString(), QStringLiteral("Bob Builder"));
        QCOMPARE(first.value(QStringLiteral("date")).toString(), QStringLiteral("2026-09-24T15:15:00Z"));
        QCOMPARE(first.value(QStringLiteral("threadId")).toString(), QStringLiteral("root-1@example.test"));
        // Dot-unstuffing: the server sent "..hidden" and "...two dots".
        QCOMPARE(first.value(QStringLiteral("bodyPlain")).toString(),
                 QStringLiteral("Totals below.\n.hidden line starts with a dot\n..two dots\n"));
        QVERIFY(first.value(QStringLiteral("unread")).toBool());
        QVERIFY(!first.value(QStringLiteral("bodyTruncated")).toBool());
        const QVariantMap second = message(store, id2);
        QCOMPARE(second.value(QStringLiteral("threadId")).toString(), QStringLiteral("root-1@example.test"));
        const QVariantMap attachment = second.value(QStringLiteral("attachments")).toList().value(0).toMap();
        QCOMPARE(attachment.value(QStringLiteral("id")).toString(), id2 + QStringLiteral("/2"));
        QCOMPARE(attachment.value(QStringLiteral("section")).toString(), QStringLiteral("2"));
        QCOMPARE(attachment.value(QStringLiteral("state")).toString(), QStringLiteral("remote"));

        // UIDL idempotency: nothing new is downloaded.
        QCOMPARE(server->retrieved.size(), 2);
        result = sync(provider, QStringLiteral("sync-2"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("addedCount")).toInt(), 0);
        QCOMPARE(server->retrieved.size(), 2);

        // Flags and archive are local edits: no connection at all.
        const qsizetype lines = server->log.size();
        QCOMPARE(act(provider, id1, QStringLiteral("read")).value(QStringLiteral("state")).toString(), QStringLiteral("accepted"));
        QVERIFY(!message(store, id1).value(QStringLiteral("unread")).toBool());
        QVERIFY(act(provider, id1, QStringLiteral("flag")).value(QStringLiteral("ok")).toBool());
        QVERIFY(message(store, id1).value(QStringLiteral("flagged")).toBool());
        QVERIFY(act(provider, id1, QStringLiteral("archive")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(message(store, id1).value(QStringLiteral("localMailbox")).toString(), QStringLiteral("Archive"));
        QCOMPARE(server->log.size(), lines);
        QCOMPARE(act(provider, id1, QStringLiteral("reply")).value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QCOMPARE(act(provider, QStringLiteral("pop3:home:nope"), QStringLiteral("read")).value(QStringLiteral("state")).toString(),
                 QStringLiteral("rejected"));
        QVERIFY(fileHas(store.filePath(), "\"localMailbox\": \"Archive\"") || fileHas(store.filePath(), "\"localMailbox\":\"Archive\""));

        // The attachment is decoded from a fresh RETR of the server copy.
        result = fetch(provider, id2, id2 + QStringLiteral("/2"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(error(result)));
        QCOMPARE(server->retrieved.size(), 3);
        QVERIFY(!result.contains(QStringLiteral("bytes")));
        QCOMPARE(attachments.read(id2 + QStringLiteral("/2")).value(QStringLiteral("bytes")).toByteArray(),
                 QByteArray("id,total\n1,42\n"));
        QCOMPARE(message(store, id2).value(QStringLiteral("attachments")).toList().value(0).toMap()
                     .value(QStringLiteral("state")).toString(), QStringLiteral("local"));

        // Removed on the server by another client: the local copy stays.
        server->maildrop.removeFirst();
        result = sync(provider, QStringLiteral("sync-3"));
        QCOMPARE(result.value(QStringLiteral("updatedCount")).toInt(), 1);
        QCOMPARE(result.value(QStringLiteral("removedCount")).toInt(), 0);
        QVERIFY(!message(store, id1).isEmpty());
        QVERIFY(!message(store, id1).value(QStringLiteral("remoteOnServer")).toBool());

        // delete: DELE + QUIT for a server copy, a local removal otherwise.
        result = act(provider, id2, QStringLiteral("delete"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(error(result)));
        QVERIFY(result.value(QStringLiteral("removedFromServer")).toBool());
        QVERIFY(server->maildrop.isEmpty());
        QVERIFY(message(store, id2).isEmpty());
        const qsizetype before = server->log.size();
        result = act(provider, id1, QStringLiteral("delete"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QVERIFY(result.value(QStringLiteral("local")).toBool());
        QCOMPARE(server->log.size(), before);
        QVERIFY(store.messages().isEmpty());
        QVERIFY(!fileHas(store.filePath(), kSecret.toUtf8()));
    }

    void leaveOnServerOffDeletesOnlyAfterTheSave()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("mail.json"));
        std::unique_ptr<Pop3TestServer> server(newServer());
        server->add("a1", kPlainMessage);
        server->add("a2", kAttachmentMessage);
        server->add("a3", bigMessage());
        QStringList savedAtDelete;
        server->onDelete = [&](const QByteArray &uidl) {
            // The saved store file must already hold the message being deleted.
            if (fileHas(path, "\"remoteUidl\": \"" + uidl + '"') || fileHas(path, "\"remoteUidl\":\"" + uidl + '"'))
                savedAtDelete.append(QString::fromLatin1(uidl));
        };
        LocalProductivityStore store;
        store.setFilePath(path);
        LocalAttachmentStore attachments;
        attachments.setRootPath(directory.filePath(QStringLiteral("attachments")));
        Pop3MailProvider provider;
        configure(provider, server->port(), &store, &attachments);
        provider.setProperty("leaveOnServer", false);
        provider.setProperty("maximumMessageBytes", 1000);

        // Without autoSave nothing proves the local copy is on disk.
        provider.setProperty("autoSave", false);
        QVariantMap result = sync(provider, QStringLiteral("sync-unsaved"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(error(result)));
        QCOMPARE(server->maildrop.size(), 3);
        QVERIFY(!server->log.contains(QStringLiteral("DELE 1")));
        QVERIFY(store.save());

        // Another writer saved the file first: conflict, and nothing is deleted.
        provider.setProperty("autoSave", true);
        LocalProductivityStore other;
        other.setFilePath(path);
        QVERIFY(other.load());
        QVERIFY(other.upsert(QStringLiteral("messages"), {{QStringLiteral("id"), QStringLiteral("note")},
                                                          {QStringLiteral("messageId"), QStringLiteral("note")}})
                    .value(QStringLiteral("ok")).toBool());
        QVERIFY(other.save());
        QVERIFY(store.upsert(QStringLiteral("messages"), {{QStringLiteral("id"), QStringLiteral("stale")},
                                                          {QStringLiteral("messageId"), QStringLiteral("stale")}})
                    .value(QStringLiteral("ok")).toBool());
        result = sync(provider, QStringLiteral("sync-conflict"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QCOMPARE(server->maildrop.size(), 3);
        for (const QString &line : std::as_const(server->log)) QVERIFY(!line.startsWith(QStringLiteral("DELE")));

        // Reloaded: the saved copies are complete (attachment bytes imported)
        // before the server copies go; the oversized one stays on the server.
        QVERIFY(store.load());
        store.remove(QStringLiteral("messages"), QStringLiteral("note"));
        result = sync(provider, QStringLiteral("sync-delete"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(error(result)));
        QCOMPARE(result.value(QStringLiteral("deleteState")).toString(), QStringLiteral("accepted"));
        QCOMPARE(result.value(QStringLiteral("deletedCount")).toInt(), 2);
        QCOMPARE(result.value(QStringLiteral("keptOnServerCount")).toInt(), 0);
        QCOMPARE(savedAtDelete, QStringList({QStringLiteral("a2"), QStringLiteral("a1")}));
        QCOMPARE(server->maildrop.size(), 1);
        QVERIFY(server->contains("a3"));
        const QVariantMap withFile = message(store, QStringLiteral("pop3:home:a2"));
        QVERIFY(!withFile.value(QStringLiteral("remoteOnServer")).toBool());
        const QVariantMap part = withFile.value(QStringLiteral("attachments")).toList().value(0).toMap();
        QCOMPARE(part.value(QStringLiteral("state")).toString(), QStringLiteral("local"));
        QCOMPARE(attachments.read(part.value(QStringLiteral("id")).toString()).value(QStringLiteral("bytes")).toByteArray(),
                 QByteArray("id,total\n1,42\n"));
        QVERIFY(message(store, QStringLiteral("pop3:home:a3")).value(QStringLiteral("remoteOnServer")).toBool());
        QVERIFY(!fileHas(path, kSecret.toUtf8()));

        // A server copy that is gone cannot serve an attachment any more.
        result = fetch(provider, QStringLiteral("pop3:home:a2"), part.value(QStringLiteral("id")).toString());
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("no longer on the POP3 server")));
    }

    void retentionAndOversizedMessages()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        std::unique_ptr<Pop3TestServer> server(newServer());
        server->add("old", kPlainMessage);
        server->add("big", bigMessage());
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("mail.json")));
        Pop3MailProvider provider;
        configure(provider, server->port(), &store);
        provider.setProperty("deleteAfterDays", 7);
        provider.setProperty("maximumMessageBytes", 1000);

        QVariantMap result = sync(provider, QStringLiteral("sync-1"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(error(result)));
        QCOMPARE(server->maildrop.size(), 2);                  // nothing is 7 days old yet
        QCOMPARE(server->topped, QStringList{QStringLiteral("big")});
        QCOMPARE(server->retrieved, QStringList{QStringLiteral("old")});
        const QVariantMap big = message(store, QStringLiteral("pop3:home:big"));
        QVERIFY(big.value(QStringLiteral("bodyTruncated")).toBool());
        QCOMPARE(big.value(QStringLiteral("subject")).toString(), QStringLiteral("Huge scan"));
        QVERIFY(big.value(QStringLiteral("bodyPlain")).toString().isEmpty());

        // Ten days later (by the stored download time) both are due; only the
        // complete local copy lets its server copy go.
        const QString tenDaysAgo = QDateTime::currentDateTimeUtc().addDays(-10).toString(Qt::ISODate);
        for (const QString &id : {QStringLiteral("pop3:home:old"), QStringLiteral("pop3:home:big")}) {
            QVariantMap row = message(store, id);
            row.insert(QStringLiteral("remoteFetchedAt"), tenDaysAgo);
            QVERIFY(store.upsert(QStringLiteral("messages"), row).value(QStringLiteral("ok")).toBool());
        }
        result = sync(provider, QStringLiteral("sync-2"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(error(result)));
        QCOMPARE(result.value(QStringLiteral("addedCount")).toInt(), 0);
        QCOMPARE(result.value(QStringLiteral("deletedCount")).toInt(), 1);
        QVERIFY(!server->contains("old"));
        QVERIFY(server->contains("big"));
        QVERIFY(!message(store, QStringLiteral("pop3:home:old")).value(QStringLiteral("remoteOnServer")).toBool());
        QCOMPARE(server->retrieved.size(), 1);                 // no attachments: no second RETR
    }

    void saslMechanismsAndExpiredToken()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString shortToken = QStringLiteral("oauth-Token-short");
        const QString longToken = QStringLiteral("oauth-Token-") + QString(220, QLatin1Char('z'));
        std::unique_ptr<Pop3TestServer> server(newServer());
        server->sasl = {QStringLiteral("PLAIN"), QStringLiteral("XOAUTH2")};
        server->add("m1", kPlainMessage);
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("mail.json")));
        Pop3MailProvider provider;
        configure(provider, server->port(), &store);
        provider.setProperty("autoSave", false);

        QVariantMap result = sync(provider, QStringLiteral("plain"));     // "auto" picks SASL PLAIN
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(error(result)));
        QVERIFY(error(result).contains(QStringLiteral("C: AUTH PLAIN <redacted>")));
        QVERIFY(!server->log.contains(QStringLiteral("USER ada")));

        // XOAUTH2 with an initial response, then a token too long for the
        // 255-octet AUTH line (sent after the empty "+ " challenge).
        for (const QString &token : {shortToken, longToken}) {
            server->secret = token;
            provider.setSecret(token, QStringLiteral("xoauth2"));
            result = sync(provider, QStringLiteral("xoauth2"));
            QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(error(result)));
            QVERIFY(!everything(result).contains(token));
            const QByteArray payload = QByteArray("user=ada\x01" "auth=Bearer " + token.toUtf8() + "\x01\x01").toBase64();
            QVERIFY(!everything(result).contains(QString::fromLatin1(payload)));
        }
        QVERIFY(server->log.contains(QStringLiteral("AUTH XOAUTH2")));

        // An expired token: the error challenge gets an empty answer and the
        // rejection lets a host adapter refresh and retry (D-189).
        provider.setSecret(QStringLiteral("oauth-Token-expired"), QStringLiteral("xoauth2"));
        result = sync(provider, QStringLiteral("expired"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(!result.value(QStringLiteral("retryable")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("AUTH failed")));
        QVERIFY(!everything(result).contains(QStringLiteral("oauth-Token-expired")));

        // A mechanism the server does not offer is refused before sending it.
        server->sasl.clear();
        provider.setSecret(kSecret, QStringLiteral("plain"));
        result = sync(provider, QStringLiteral("no-plain"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("SASL PLAIN")));

        // A locked maildrop (RFC 2449 [IN-USE]) is worth retrying.
        server->passReply = "-ERR [IN-USE] Mailbox locked by another session";
        provider.setSecret(kSecret, QStringLiteral("user"));
        result = sync(provider, QStringLiteral("locked"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());
        QVERIFY(!everything(result).contains(kSecret));
    }

    void stlsRequiredButMissingAndOffline()
    {
        std::unique_ptr<Pop3TestServer> server(newServer());
        server->add("m1", kPlainMessage);
        LocalProductivityStore store;
        Pop3MailProvider provider;
        configure(provider, server->port(), &store);
        provider.setProperty("autoSave", false);
        provider.setProperty("security", QStringLiteral("starttls"));
        QVariantMap result = sync(provider, QStringLiteral("no-stls"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("STLS")));
        for (const QString &line : std::as_const(server->log)) {
            QVERIFY(!line.startsWith(QStringLiteral("USER")));
            QVERIFY(!line.startsWith(QStringLiteral("PASS")));
            QVERIFY(!line.startsWith(QStringLiteral("AUTH")));
        }
        QVERIFY(store.messages().isEmpty());

        Pop3MailProvider offline;
        configure(offline, closedPort(), &store);
        result = sync(offline, QStringLiteral("offline"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());

        Pop3MailProvider unconfigured;
        QSignalSpy spy(&unconfigured, &Pop3MailProvider::syncFinished);
        QVERIFY(!unconfigured.sync(QStringLiteral("no-store")));
        QVERIFY(spy.isEmpty());                                 // delivered on the next turn
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.first().at(1).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(!unconfigured.busy());
    }
};

QTEST_MAIN(Pop3MailProviderTest)
#include "tst_pop3_mail_provider.moc"
