// SPDX-License-Identifier: LGPL-3.0-or-later
// D-299: ImapMailProvider folders — modified UTF-7 names, LIST with RFC 6154
// special-use roles (and the name heuristics), STATUS counts, APPEND with
// APPENDUID, moves with and without MOVE, and sync of a non-INBOX mailbox —
// against ImapFolderTestServer over real TCP on 127.0.0.1.
#include "imap_folder_test_server.h"
#include "imap_folders.h"
#include "imap_mail_provider.h"
#include "imap_mutf7.h"
#include "smtp_mail_provider.h"

#include <QFile>
#include <QMutex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
const QString kSecret = QStringLiteral("folder-Secret-31");
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

const QByteArray kMessage = "From: Bob <bob@example.test>\r\nTo: ada@example.test\r\nSubject: Hello\r\n"
                            "Message-ID: <m1@example.test>\r\n\r\nHi Ada\r\n";

void configure(ImapMailProvider &provider, quint16 port, LocalProductivityStore *store = nullptr)
{
    provider.setProperty("host", QStringLiteral("127.0.0.1"));
    provider.setProperty("port", port);
    provider.setProperty("security", QStringLiteral("none"));
    provider.setProperty("userName", QStringLiteral("ada"));
    provider.setProperty("accountId", QStringLiteral("work"));
    provider.setProperty("timeoutMs", 10000);
    provider.setStore(store);
    provider.setSecret(kSecret);
}

void credentials(ImapTestServer &server)
{
    server.user = QStringLiteral("ada");
    server.secret = kSecret;
}

template <typename Signal>
QVariantMap waitFor(ImapMailProvider &provider, Signal signal, const std::function<void()> &start)
{
    QSignalSpy spy(&provider, signal);
    start();
    if (!spy.wait(15000)) return {};
    return spy.takeFirst().at(1).toMap();
}

QVariantMap mailbox(const QVariantList &list, const QString &name)
{
    for (const QVariant &value : list)
        if (value.toMap().value(QStringLiteral("name")).toString() == name) return value.toMap();
    return {};
}

bool logHas(const QStringList &log, const QString &suffix)
{
    for (const QString &line : log) if (line.endsWith(suffix)) return true;
    return false;
}

QString text(const QVariantMap &result)
{
    return result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n'))
        + result.value(QStringLiteral("error")).toString();
}
} // namespace

class ImapFoldersTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { previousHandler = qInstallMessageHandler(captureMessages); }
    void cleanupTestCase()
    {
        qInstallMessageHandler(previousHandler);
        for (const QString &line : std::as_const(capturedLog)) QVERIFY(!line.contains(kSecret));
    }

    void modifiedUtf7Codec()
    {
        // RFC 3501 §5.1.3's example, both directions.
        const QString peter = QStringLiteral("~peter/mail/") + QString::fromUtf8("台北/日本語");
        QCOMPARE(Imap::encodeMailboxName(peter), QByteArray("~peter/mail/&U,BTFw-/&ZeVnLIqe-"));
        bool ok = false;
        QCOMPARE(Imap::decodeMailboxName("~peter/mail/&U,BTFw-/&ZeVnLIqe-", &ok), peter);
        QVERIFY(ok);
        QCOMPARE(Imap::encodeMailboxName(QStringLiteral("R&D")), QByteArray("R&-D"));
        QCOMPARE(Imap::encodeMailboxName(QString::fromUtf8("Entwürfe")), QByteArray("Entw&APw-rfe"));
        QCOMPARE(Imap::encodeMailboxName(QStringLiteral("INBOX")), QByteArray("INBOX"));
        const QStringList samples{QStringLiteral("R&D"), QString::fromUtf8("Entwürfe"), QString::fromUtf8("📅 Termine & Co"),
                                  QString::fromUtf8("Входящие/Отправленные"), QStringLiteral("a&&b"), QString(), peter};
        for (const QString &sample : samples) {
            const QByteArray wire = Imap::encodeMailboxName(sample);
            for (const char c : wire) QVERIFY(c >= 0x20 && c <= 0x7e);
            QCOMPARE(Imap::decodeMailboxName(wire, &ok), sample);
            QVERIFY(ok);
        }
        // Malformed: unterminated run, bad base64, an encoded printable
        // character, an odd byte count, raw 8-bit from a non-conforming server.
        for (const QByteArray &bad : {QByteArray("&U,BTFw"), QByteArray("&U*BTFw-"), QByteArray("&AGE-"),
                                      QByteArray("&AA-"), QByteArray("Caf\xc3\xa9")}) {
            Imap::decodeMailboxName(bad, &ok);
            QVERIFY2(!ok, bad.constData());
        }
        QCOMPARE(Imap::decodeMailboxName("Caf\xc3\xa9", &ok), QString::fromUtf8("Café"));
    }

    void listParsingRolesAndHeuristics()
    {
        const QVariantList parsed = ImapFolders::parseList({
            "LIST (\\HasNoChildren \\Sent) \"/\" \"[Gmail]/Sent Mail\"", "LIST (\\HasNoChildren) \"/\" \"Sent Items\"",
            "LIST (\\Noselect \\HasChildren) \"/\" \"[Gmail]\"", "LIST (\\HasNoChildren) \"/\" \"Deleted Items\"",
            "LIST (\\HasNoChildren) NIL Spam", "LIST (\\HasNoChildren) \"/\" inbox", "STATUS INBOX (MESSAGES 1)"});
        QVariantList mailboxes = parsed;
        ImapFolders::applyNameHeuristics(&mailboxes);
        QCOMPARE(mailboxes.size(), 6);
        QCOMPARE(mailboxes.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("INBOX"));
        QCOMPARE(mailboxes.first().toMap().value(QStringLiteral("specialUse")).toString(), QStringLiteral("inbox"));
        QCOMPARE(mailbox(mailboxes, QStringLiteral("[Gmail]/Sent Mail")).value(QStringLiteral("specialUse")).toString(),
                 QStringLiteral("sent"));
        // An attribute claims "sent"; the look-alike name stays a plain folder.
        QCOMPARE(mailbox(mailboxes, QStringLiteral("Sent Items")).value(QStringLiteral("specialUse")).toString(), QString());
        QCOMPARE(mailbox(mailboxes, QStringLiteral("Deleted Items")).value(QStringLiteral("specialUse")).toString(),
                 QStringLiteral("trash"));
        QCOMPARE(mailbox(mailboxes, QStringLiteral("Spam")).value(QStringLiteral("specialUse")).toString(), QStringLiteral("junk"));
        QCOMPARE(mailbox(mailboxes, QStringLiteral("Spam")).value(QStringLiteral("delimiter")).toString(), QString());
        QVERIFY(!mailbox(mailboxes, QStringLiteral("[Gmail]")).value(QStringLiteral("selectable")).toBool());
        QVERIFY(ImapFolders::validFlag(QStringLiteral("\\Seen")));
        QVERIFY(ImapFolders::validFlag(QStringLiteral("$Forwarded")));
        QVERIFY(!ImapFolders::validFlag(QStringLiteral("\\Seen) (x")));
        QVERIFY(!ImapFolders::validFlag(QStringLiteral("\\Recent")));
    }

    void listMailboxesWithSpecialUseAndCounts()
    {
        ImapFolderTestServer server;
        credentials(server);
        server.capabilities.append(QStringLiteral("SPECIAL-USE"));
        server.add(1, kMessage);
        server.add(2, kMessage, {QStringLiteral("\\Seen")});
        server.addFolder("Sent", QStringLiteral("\\Sent"));
        server.addFolder("Entw&APw-rfe", QStringLiteral("\\Drafts")).messages.append({1, kMessage, {}});
        server.addFolder("[Gmail]", {}, {QStringLiteral("\\Noselect"), QStringLiteral("\\HasChildren")});
        server.addFolder("~peter/mail/&U,BTFw-/&ZeVnLIqe-").literalName = true;
        server.addFolder("Junk", QStringLiteral("\\Junk")).statusFails = true;
        ImapMailProvider provider;
        configure(provider, server.port());                   // no store needed
        const QVariantMap result = waitFor(provider, &ImapMailProvider::mailboxesFinished,
                                           [&] { provider.listMailboxes(QStringLiteral("list-1"), true); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(result.value(QStringLiteral("requestId")).toString(), QStringLiteral("list-1"));
        QVERIFY(logHas(server.log, QStringLiteral("LIST \"\" \"*\" RETURN (SPECIAL-USE)")));
        const QVariantList list = result.value(QStringLiteral("mailboxes")).toList();
        QCOMPARE(list.size(), 6);
        const QVariantMap inbox = list.first().toMap();
        QCOMPARE(inbox.value(QStringLiteral("name")).toString(), QStringLiteral("INBOX"));
        QCOMPARE(inbox.value(QStringLiteral("total")).toInt(), 2);
        QCOMPARE(inbox.value(QStringLiteral("unread")).toInt(), 1);
        QCOMPARE(mailbox(list, QStringLiteral("Sent")).value(QStringLiteral("specialUse")).toString(), QStringLiteral("sent"));
        const QVariantMap drafts = mailbox(list, QString::fromUtf8("Entwürfe"));
        QCOMPARE(drafts.value(QStringLiteral("rawName")).toString(), QStringLiteral("Entw&APw-rfe"));
        QCOMPARE(drafts.value(QStringLiteral("specialUse")).toString(), QStringLiteral("drafts"));
        QCOMPARE(drafts.value(QStringLiteral("delimiter")).toString(), QStringLiteral("/"));
        QCOMPARE(drafts.value(QStringLiteral("unread")).toInt(), 1);
        QVERIFY(logHas(server.log, QStringLiteral("STATUS \"Entw&APw-rfe\" (MESSAGES UNSEEN)")));
        const QVariantMap gmail = mailbox(list, QStringLiteral("[Gmail]"));
        QVERIFY(!gmail.value(QStringLiteral("selectable")).toBool());
        QVERIFY(gmail.value(QStringLiteral("flags")).toStringList().contains(QStringLiteral("\\Noselect")));
        QCOMPARE(gmail.value(QStringLiteral("total")).toInt(), -1);
        QVERIFY(!mailbox(list, QStringLiteral("~peter/mail/") + QString::fromUtf8("台北/日本語")).isEmpty());
        // A STATUS refused for one mailbox leaves its counts unknown, not the listing failed.
        const QVariantMap junk = mailbox(list, QStringLiteral("Junk"));
        QCOMPARE(junk.value(QStringLiteral("specialUse")).toString(), QStringLiteral("junk"));
        QCOMPARE(junk.value(QStringLiteral("unread")).toInt(), -1);
        QVERIFY(!text(result).contains(kSecret));
    }

    void plainListWithoutSpecialUseCapability()
    {
        ImapFolderTestServer server;
        credentials(server);
        server.specialUseOnlyWhenAsked = false;             // Gmail marks roles in a plain LIST
        server.addFolder("[Gmail]/Sent Mail", QStringLiteral("\\Sent"));
        server.addFolder("Sent Items");
        server.addFolder("Deleted Items");
        server.addFolder("Archive");
        ImapMailProvider provider;
        configure(provider, server.port());
        const QVariantMap result = waitFor(provider, &ImapMailProvider::mailboxesFinished,
                                           [&] { provider.listMailboxes(QStringLiteral("list-2")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QVERIFY(logHas(server.log, QStringLiteral("LIST \"\" \"*\"")));
        QVERIFY(!logHas(server.log, QStringLiteral("RETURN (SPECIAL-USE)")));
        const QVariantList list = result.value(QStringLiteral("mailboxes")).toList();
        QCOMPARE(mailbox(list, QStringLiteral("[Gmail]/Sent Mail")).value(QStringLiteral("specialUse")).toString(), QStringLiteral("sent"));
        QCOMPARE(mailbox(list, QStringLiteral("Sent Items")).value(QStringLiteral("specialUse")).toString(), QString());
        QCOMPARE(mailbox(list, QStringLiteral("Deleted Items")).value(QStringLiteral("specialUse")).toString(), QStringLiteral("trash"));
        QCOMPARE(mailbox(list, QStringLiteral("Archive")).value(QStringLiteral("specialUse")).toString(), QStringLiteral("archive"));
        QCOMPARE(mailbox(list, QStringLiteral("Archive")).value(QStringLiteral("unread")).toInt(), -1);   // no counts asked
        for (const QString &line : std::as_const(server.log)) QVERIFY(!line.contains(QStringLiteral("STATUS")));
    }

    void appendStoresACopy()
    {
        ImapFolderTestServer server;
        credentials(server);
        server.addFolder("Sent", QStringLiteral("\\Sent"));
        server.addFolder("Entw&APw-rfe", QStringLiteral("\\Drafts"));
        ImapMailProvider provider;
        configure(provider, server.port());
        // The bytes a host would store in Sent after SmtpMailProvider.send.
        SmtpMailProvider smtp;
        smtp.setProperty("fromAddress", QStringLiteral("ada@example.test"));
        const QVariantMap composed = smtp.composeMessage({{QStringLiteral("to"), QStringLiteral("bob@example.test")},
                                                          {QStringLiteral("subject"), QStringLiteral("Copy me")},
                                                          {QStringLiteral("body"), QStringLiteral("Line one\nLine two")}});
        QVERIFY(composed.value(QStringLiteral("ok")).toBool());
        const QByteArray bytes = composed.value(QStringLiteral("message")).toByteArray();
        QVariantMap result = waitFor(provider, &ImapMailProvider::appendFinished, [&] {
            provider.append(QStringLiteral("append-1"), QStringLiteral("Sent"), bytes, {QStringLiteral("\\Seen")});
        });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(result.value(QStringLiteral("appendUid")).toLongLong(), qint64(1));
        QCOMPARE(result.value(QStringLiteral("appendUidValidity")).toString(), QStringLiteral("5000"));
        QCOMPARE(result.value(QStringLiteral("mailbox")).toString(), QStringLiteral("Sent"));
        QVERIFY(!result.contains(QStringLiteral("rawMessage")));
        const auto &sent = server.contents("Sent");
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first().source, bytes);
        QCOMPARE(sent.first().flags, QStringList{QStringLiteral("\\Seen")});
        QVERIFY(text(result).contains(QStringLiteral("{%1 message bytes}").arg(bytes.size())));
        QVERIFY(!text(result).contains(QStringLiteral("Copy me")));

        // Bare LF is sent as CRLF; a UTF-8 name goes out in modified UTF-7;
        // without UIDPLUS there is no appendUid.
        server.capabilities.removeAll(QStringLiteral("UIDPLUS"));
        result = waitFor(provider, &ImapMailProvider::appendFinished, [&] {
            provider.append(QStringLiteral("append-2"), QString::fromUtf8("Entwürfe"), "Subject: Draft\nBody\n", {});
        });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QVERIFY(!result.contains(QStringLiteral("appendUid")));
        QVERIFY(logHas(server.log, QStringLiteral("APPEND \"Entw&APw-rfe\" {22}")));
        QCOMPARE(server.contents("Entw&APw-rfe").first().source, QByteArray("Subject: Draft\r\nBody\r\n"));

        result = waitFor(provider, &ImapMailProvider::appendFinished, [&] {
            provider.append(QStringLiteral("append-3"), QStringLiteral("Nowhere"), bytes, {});
        });
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("TRYCREATE")));

        // Invalid input is refused before connecting.
        const qsizetype before = server.log.size();
        for (const auto &[flags, message] : {std::pair{QStringList{QStringLiteral("\\Seen) (x")}, bytes},
                                             std::pair{QStringList{}, QByteArray()},
                                             std::pair{QStringList{}, QByteArray("a\0b", 3)}}) {
            result = waitFor(provider, &ImapMailProvider::appendFinished, [&] {
                provider.append(QStringLiteral("append-bad"), QStringLiteral("Sent"), message, flags);
            });
            QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        }
        QCOMPARE(server.log.size(), before);
    }

    void moveWithAndWithoutMoveAndSyncOfAnyMailbox()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("mail.json")));
        ImapFolderTestServer server;
        credentials(server);
        server.add(11, kMessage);
        server.add(12, kMessage);
        const QByteArray peterWire = "~peter/mail/&U,BTFw-/&ZeVnLIqe-";
        server.addFolder(peterWire).uidValidity = 7000;
        server.addFolder("Sent", QStringLiteral("\\Sent"));
        ImapMailProvider provider;
        configure(provider, server.port(), &store);
        QVariantMap result = waitFor(provider, &ImapMailProvider::syncFinished, [&] { provider.sync(QStringLiteral("s1")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(store.messages().size(), 2);

        const QString peter = QStringLiteral("~peter/mail/") + QString::fromUtf8("台北/日本語");
        result = waitFor(provider, &ImapMailProvider::actionFinished, [&] {
            provider.moveMessage(QStringLiteral("move-1"), QStringLiteral("imap:work:INBOX:1000:11"), peter);
        });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(result.value(QStringLiteral("action")).toString(), QStringLiteral("move"));
        QCOMPARE(result.value(QStringLiteral("targetMailbox")).toString(), peter);
        QCOMPARE(result.value(QStringLiteral("targetUid")).toLongLong(), qint64(1));
        QCOMPARE(result.value(QStringLiteral("targetUidValidity")).toString(), QStringLiteral("7000"));
        QVERIFY(logHas(server.log, QStringLiteral("UID MOVE 11 \"") + QString::fromLatin1(peterWire) + QLatin1Char('"')));
        QCOMPARE(server.contents(peterWire).size(), 1);
        QCOMPARE(store.messages().size(), 1);

        // Without MOVE: COPY, \Deleted and UID EXPUNGE of that UID only.
        server.capabilities.removeAll(QStringLiteral("MOVE"));
        result = waitFor(provider, &ImapMailProvider::actionFinished, [&] {
            provider.moveMessage(QStringLiteral("move-2"), QStringLiteral("imap:work:INBOX:1000:12"), QStringLiteral("Sent"));
        });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QCOMPARE(result.value(QStringLiteral("targetUid")).toLongLong(), qint64(1));
        QVERIFY(logHas(server.log, QStringLiteral("UID COPY 12 \"Sent\"")));
        QVERIFY(logHas(server.log, QStringLiteral("UID STORE 12 +FLAGS.SILENT (\\Deleted)")));
        QVERIFY(logHas(server.log, QStringLiteral("UID EXPUNGE 12")));
        QCOMPARE(server.contents("INBOX").size(), 0);
        QCOMPARE(server.contents("Sent").size(), 1);
        QVERIFY(store.messages().isEmpty());

        // A move needs a different target; "move" through applyAction has none.
        server.add(13, kMessage);
        QVERIFY(waitFor(provider, &ImapMailProvider::syncFinished, [&] { provider.sync(QStringLiteral("s2")); })
                    .value(QStringLiteral("ok")).toBool());
        for (const QString &target : {QStringLiteral("INBOX"), QString()}) {
            result = waitFor(provider, &ImapMailProvider::actionFinished, [&] {
                provider.moveMessage(QStringLiteral("move-bad"), QStringLiteral("imap:work:INBOX:1000:13"), target);
            });
            QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        }
        result = waitFor(provider, &ImapMailProvider::actionFinished, [&] {
            provider.applyAction(QStringLiteral("move-3"), QStringLiteral("imap:work:INBOX:1000:13"), QStringLiteral("move"));
        });
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));

        // Sync works for any mailbox; identities carry the decoded name.
        provider.setProperty("mailbox", peter);
        result = waitFor(provider, &ImapMailProvider::syncFinished, [&] { provider.sync(QStringLiteral("s3")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(text(result)));
        QVERIFY(logHas(server.log, QStringLiteral("SELECT \"") + QString::fromLatin1(peterWire) + QLatin1Char('"')));
        const QString movedId = QStringLiteral("imap:work:%1:7000:1").arg(peter);
        bool found = false;
        for (const QVariant &value : store.messages()) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("id")).toString() != movedId) continue;
            found = true;
            QCOMPARE(row.value(QStringLiteral("remoteMailbox")).toString(), peter);
        }
        QVERIFY(found);
        QFile saved(store.filePath());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray file = saved.readAll();
        QVERIFY(!file.contains(kSecret.toUtf8()));
        QVERIFY(file.contains(peter.toUtf8()));
    }
};

QTEST_MAIN(ImapFoldersTest)
#include "tst_imap_folders.moc"
