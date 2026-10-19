// SPDX-License-Identifier: LGPL-3.0-or-later
#include "imap_mail_provider.h"
#include "imap_session.h"
#include "mail_test_servers.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
const QString kSecret = QStringLiteral("imap-Secret-77");

const QByteArray kPlainMessage =
    "From: \"Bob Builder\" <bob@example.test>\r\n"
    "To: ada@example.test\r\n"
    "Subject: =?UTF-8?B?w5xiZXJzaWNodA==?=\r\n"
    "Date: Thu, 24 Sep 2026 09:15:00 -0600\r\n"
    "Message-ID: <root-1@example.test>\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Content-Transfer-Encoding: quoted-printable\r\n"
    "\r\n"
    "Totals are =E2=82=AC42.\r\n";

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

const QByteArray kLateMessage =
    "From: dave@example.test\r\nTo: ada@example.test\r\nSubject: Later\r\n"
    "Content-Type: text/html; charset=utf-8\r\n\r\n<p>Late &amp; <b>bold</b></p>\r\n";

void configure(ImapMailProvider &provider, quint16 port, LocalProductivityStore *store,
               LocalAttachmentStore *attachments = nullptr)
{
    provider.setProperty("host", QStringLiteral("127.0.0.1"));
    provider.setProperty("port", port);
    provider.setProperty("security", QStringLiteral("none"));
    provider.setProperty("userName", QStringLiteral("ada"));
    provider.setProperty("accountId", QStringLiteral("work"));
    provider.setProperty("timeoutMs", 10000);
    provider.setStore(store);
    provider.setAttachmentStore(attachments);
    provider.setSecret(kSecret);
}

template <typename Signal>
QVariantMap waitFor(ImapMailProvider &provider, Signal signal, const std::function<void()> &start)
{
    QSignalSpy spy(&provider, signal);
    start();
    if (!spy.wait(15000)) return {};
    return spy.takeFirst().at(1).toMap();
}

QVariantMap sync(ImapMailProvider &provider, const QString &id)
{
    return waitFor(provider, &ImapMailProvider::syncFinished, [&] { provider.sync(id); });
}

QVariantMap act(ImapMailProvider &provider, const QString &messageId, const QString &action)
{
    return waitFor(provider, &ImapMailProvider::actionFinished,
                   [&] { provider.applyAction(QStringLiteral("act-") + action, messageId, action); });
}

QVariantMap message(const LocalProductivityStore &store, const QString &id)
{
    for (const QVariant &value : store.messages())
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    return {};
}

bool logHas(const QStringList &log, const QString &suffix)
{
    for (const QString &line : log) if (line.endsWith(suffix)) return true;
    return false;
}

quint16 closedPort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}
} // namespace

class ImapMailProviderTest : public QObject {
    Q_OBJECT
private slots:
    void responseParserHandlesLiteralsAndSections()
    {
        const QByteArray raw = "12 FETCH (UID 7 FLAGS (\\Seen \\Flagged) BODY[HEADER.FIELDS (SUBJECT)] {5}\r\nHello)";
        const QList<Imap::FetchItem> items = Imap::fetchItems({raw});
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().sequence, qint64(12));
        QCOMPARE(items.first().find("UID")->text, QByteArray("7"));
        QCOMPARE(items.first().find("FLAGS")->items.size(), 2);
        QCOMPARE(items.first().find("BODY[")->text, QByteArray("Hello"));
        QCOMPARE(Imap::quoted(QStringLiteral("a\"b\\c")), QByteArray("\"a\\\"b\\\\c\""));
    }

    void syncActionsAttachmentsAndUidValidity()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ImapTestServer server;
        server.user = QStringLiteral("ada");
        server.secret = kSecret;
        server.add(11, kPlainMessage);
        server.add(12, kAttachmentMessage, {QStringLiteral("\\Seen")});
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("mail.json")));
        LocalAttachmentStore attachments;
        attachments.setRootPath(directory.filePath(QStringLiteral("attachments")));
        ImapMailProvider provider;
        configure(provider, server.port(), &store, &attachments);

        QVariantMap result = sync(provider, QStringLiteral("sync-1"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("addedCount")).toInt(), 2);
        const QString id11 = QStringLiteral("imap:work:INBOX:1000:11");
        const QString id12 = QStringLiteral("imap:work:INBOX:1000:12");
        QVariantMap first = message(store, id11);
        QCOMPARE(first.value(QStringLiteral("messageId")).toString(), id11);
        QCOMPARE(first.value(QStringLiteral("subject")).toString(), QStringLiteral("Übersicht"));
        QCOMPARE(first.value(QStringLiteral("sender")).toString(), QStringLiteral("Bob Builder"));
        QCOMPARE(first.value(QStringLiteral("senderAddress")).toString(), QStringLiteral("bob@example.test"));
        QCOMPARE(first.value(QStringLiteral("bodyPlain")).toString(), QStringLiteral("Totals are €42.\n"));
        QCOMPARE(first.value(QStringLiteral("date")).toString(), QStringLiteral("2026-09-24T15:15:00Z"));
        QCOMPARE(first.value(QStringLiteral("threadId")).toString(), QStringLiteral("root-1@example.test"));
        QVERIFY(first.value(QStringLiteral("unread")).toBool());
        const QVariantMap second = message(store, id12);
        QVERIFY(!second.value(QStringLiteral("unread")).toBool());
        QCOMPARE(second.value(QStringLiteral("threadId")).toString(), QStringLiteral("root-1@example.test"));
        QCOMPARE(second.value(QStringLiteral("bodyPlain")).toString(), QStringLiteral("See the attached file."));
        const QVariantMap attachment = second.value(QStringLiteral("attachments")).toList().value(0).toMap();
        QCOMPARE(attachment.value(QStringLiteral("id")).toString(), id12 + QStringLiteral("/2"));
        QCOMPARE(attachment.value(QStringLiteral("name")).toString(), QStringLiteral("report.csv"));
        QCOMPARE(attachment.value(QStringLiteral("size")).toLongLong(), qint64(14));
        QCOMPARE(attachment.value(QStringLiteral("state")).toString(), QStringLiteral("remote"));

        result = sync(provider, QStringLiteral("sync-2"));        // nothing changed
        QCOMPARE(result.value(QStringLiteral("addedCount")).toInt(), 0);
        QCOMPARE(result.value(QStringLiteral("updatedCount")).toInt(), 0);
        QCOMPARE(result.value(QStringLiteral("removedCount")).toInt(), 0);
        QVERIFY(logHas(server.log, QStringLiteral("UID FETCH 11:12 (UID FLAGS)")));
        QVERIFY(logHas(server.log, QStringLiteral("UID FETCH 13:* (UID RFC822.SIZE)")));

        result = waitFor(provider, &ImapMailProvider::attachmentFinished, [&] {
            provider.fetchAttachment(QStringLiteral("att-1"), id12, id12 + QStringLiteral("/2"));
        });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QVERIFY(logHas(server.log, QStringLiteral("UID FETCH 12 (UID BODY.PEEK[2])")));
        QCOMPARE(attachments.read(id12 + QStringLiteral("/2")).value(QStringLiteral("bytes")).toByteArray(),
                 QByteArray("id,total\n1,42\n"));
        QCOMPARE(message(store, id12).value(QStringLiteral("attachments")).toList().value(0).toMap()
                     .value(QStringLiteral("state")).toString(), QStringLiteral("local"));

        result = act(provider, id11, QStringLiteral("read"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QVERIFY(server.find(11)->flags.contains(QStringLiteral("\\Seen")));
        QVERIFY(!message(store, id11).value(QStringLiteral("unread")).toBool());
        result = act(provider, id12, QStringLiteral("archive"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QVERIFY(!server.find(12));
        QCOMPARE(server.archive.size(), 1);
        QVERIFY(message(store, id12).isEmpty());
        result = act(provider, id11, QStringLiteral("reply"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));

        // Another client flags 11 and delivers 13; then 13 is expunged elsewhere.
        server.find(11)->flags.append(QStringLiteral("\\Flagged"));
        server.add(13, kLateMessage);
        result = sync(provider, QStringLiteral("sync-3"));
        QCOMPARE(result.value(QStringLiteral("addedCount")).toInt(), 1);
        QCOMPARE(result.value(QStringLiteral("updatedCount")).toInt(), 1);
        QVERIFY(message(store, id11).value(QStringLiteral("flagged")).toBool());
        QCOMPARE(message(store, QStringLiteral("imap:work:INBOX:1000:13")).value(QStringLiteral("bodyPlain")).toString(),
                 QStringLiteral("Late & bold"));
        server.inbox.removeLast();
        result = sync(provider, QStringLiteral("sync-4"));
        QCOMPARE(result.value(QStringLiteral("removedCount")).toInt(), 1);

        // A rebuilt mailbox (new UIDVALIDITY) replaces every cached identity.
        server.uidValidity = 2000;
        result = sync(provider, QStringLiteral("sync-5"));
        QVERIFY(result.value(QStringLiteral("reset")).toBool());
        QCOMPARE(store.messages().size(), 1);
        QCOMPARE(store.messages().first().toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("imap:work:INBOX:2000:11"));

        QFile saved(store.filePath());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(!saved.readAll().contains(kSecret.toUtf8()));
        QVERIFY(!result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n')).contains(kSecret));
    }

    void largeMailboxCostsOnlyWhatIsKept()
    {
        // D-353: a mailbox far larger than the fetch limit (the owner's Gmail
        // inbox holds 158 000) lists only its newest messages, asks only for
        // the flags of the stored ones and fetches bodies in batches.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ImapTestServer server;
        server.user = QStringLiteral("ada");
        server.secret = kSecret;
        for (qint64 uid = 101; uid <= 130; ++uid)
            server.add(uid, uid == 128 ? kPlainMessage + QByteArray(2000, 'x') : kPlainMessage);
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("mail.json")));
        ImapMailProvider provider;
        configure(provider, server.port(), &store);
        provider.setProperty("fetchLimit", 5);
        provider.setProperty("maximumMessageBytes", 1000);

        QVariantMap result = sync(provider, QStringLiteral("big-1"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("addedCount")).toInt(), 5);
        QVERIFY(logHas(server.log, QStringLiteral(" FETCH 26:* (UID RFC822.SIZE)")));
        QVERIFY(!logHas(server.log, QStringLiteral("UID FETCH 1:* (UID RFC822.SIZE)")));
        QVERIFY(logHas(server.log, QStringLiteral("UID FETCH 126:127 (UID FLAGS BODY.PEEK[])")));
        QVERIFY(logHas(server.log, QStringLiteral("UID FETCH 128 (UID FLAGS BODY.PEEK[HEADER])")));
        QVERIFY(logHas(server.log, QStringLiteral("UID FETCH 129:130 (UID FLAGS BODY.PEEK[])")));
        QCOMPARE(store.messages().size(), 5);
        QVERIFY(message(store, QStringLiteral("imap:work:INBOX:1000:125")).isEmpty());
        QVERIFY(message(store, QStringLiteral("imap:work:INBOX:1000:128")).value(QStringLiteral("bodyTruncated")).toBool());
        QCOMPARE(message(store, QStringLiteral("imap:work:INBOX:1000:130")).value(QStringLiteral("bodyPlain")).toString(),
                 QStringLiteral("Totals are €42.\n"));

        // Another client reads 127, deletes 126 and delivers 131.
        server.find(127)->flags.append(QStringLiteral("\\Seen"));
        server.inbox.removeAt(25);
        server.add(131, kLateMessage);
        server.log.clear();
        result = sync(provider, QStringLiteral("big-2"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("addedCount")).toInt(), 1);
        QCOMPARE(result.value(QStringLiteral("updatedCount")).toInt(), 1);
        QCOMPARE(result.value(QStringLiteral("removedCount")).toInt(), 1);
        QVERIFY(logHas(server.log, QStringLiteral("UID FETCH 126:130 (UID FLAGS)")));
        QVERIFY(logHas(server.log, QStringLiteral("UID FETCH 131:* (UID RFC822.SIZE)")));
        for (const QString &line : std::as_const(server.log)) QVERIFY2(!line.contains(QStringLiteral("FETCH 1:")), qPrintable(line));
        QVERIFY(!message(store, QStringLiteral("imap:work:INBOX:1000:127")).value(QStringLiteral("unread")).toBool());
        QVERIFY(message(store, QStringLiteral("imap:work:INBOX:1000:126")).isEmpty());
        QVERIFY(!message(store, QStringLiteral("imap:work:INBOX:1000:131")).isEmpty());
    }

    void largeBatchesSplitAtTheMessageCount()
    {
        // D-353: 30 new messages come in two FETCH commands (25 + 5).
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ImapTestServer server;
        server.user = QStringLiteral("ada");
        server.secret = kSecret;
        for (qint64 uid = 1; uid <= 30; ++uid) server.add(uid, kPlainMessage);
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("mail.json")));
        ImapMailProvider provider;
        configure(provider, server.port(), &store);
        const QVariantMap result = sync(provider, QStringLiteral("batch-1"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("addedCount")).toInt(), 30);
        QVERIFY(logHas(server.log, QStringLiteral("UID FETCH 1:25 (UID FLAGS BODY.PEEK[])")));
        QVERIFY(logHas(server.log, QStringLiteral("UID FETCH 26:30 (UID FLAGS BODY.PEEK[])")));
        QCOMPARE(store.messages().size(), 30);
    }

    void loginQuotingOfflineAndConflicts()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("mail.json"));
        ImapTestServer server;
        server.user = QStringLiteral("ada");
        server.secret = QStringLiteral("pa\"ss\\word");
        server.capabilities = {QStringLiteral("IMAP4rev1")};           // LOGIN only
        server.add(21, kPlainMessage);
        LocalProductivityStore store;
        store.setFilePath(path);
        ImapMailProvider provider;
        configure(provider, server.port(), &store);
        provider.setSecret(QStringLiteral("pa\"ss\\word"));
        QVariantMap result = sync(provider, QStringLiteral("sync-a"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        const QString transcript = result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n'));
        QVERIFY(transcript.contains(QStringLiteral("LOGIN <redacted>")));
        QVERIFY(!transcript.contains(QStringLiteral("ss\\\\word")));

        // The server rebuilt the mailbox since this record was stored.
        const QString id = QStringLiteral("imap:work:INBOX:1000:21");
        QVariantMap stale = message(store, id);
        stale.insert(QStringLiteral("remoteUidValidity"), QStringLiteral("999"));
        QVERIFY(store.upsert(QStringLiteral("messages"), stale).value(QStringLiteral("ok")).toBool());
        result = act(provider, id, QStringLiteral("delete"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QVERIFY(server.find(21));

        // Another writer saved the same store file: the sync is not applied on disk.
        LocalProductivityStore other;
        other.setFilePath(path);
        QVERIFY(store.save());
        QVERIFY(other.load());
        QVERIFY(other.upsert(QStringLiteral("messages"), {{QStringLiteral("id"), QStringLiteral("local-note")},
                                                          {QStringLiteral("messageId"), QStringLiteral("local-note")}})
                    .value(QStringLiteral("ok")).toBool());
        QVERIFY(other.save());
        server.add(22, kLateMessage);
        result = sync(provider, QStringLiteral("sync-b"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QVERIFY(store.load());
        QVERIFY(!message(store, QStringLiteral("local-note")).isEmpty());
        result = sync(provider, QStringLiteral("sync-c"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QVERIFY(!message(store, QStringLiteral("imap:work:INBOX:1000:22")).isEmpty());

        ImapMailProvider offline;
        configure(offline, closedPort(), &store);
        result = sync(offline, QStringLiteral("sync-d"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());
    }
};

QTEST_MAIN(ImapMailProviderTest)
#include "tst_imap_mail_provider.moc"
