// SPDX-License-Identifier: LGPL-3.0-or-later
#include "imap_folder_test_server.h"
#include "imap_keywords.h"
#include "imap_messages.h"
#include "imap_mail_provider.h"
#include "imap_mutf7.h"
#include "mail_record.h"
#include "pop3_mail_jobs.h"
#include "smtp_mail_provider.h"
#include "ical_codec.h"
#include "calendar_mail.h"
#include <QSignalSpy>
#include <QtTest>
using namespace QindaTK;

namespace {
class ExtensionServer : public ImapFolderTestServer {
public:
    QByteArray permanent = "\\Seen \\Flagged \\Answered \\Deleted \\*";
protected:
    void line(Connection &c, const QByteArray &text) override
    {
        const QByteArray tag = text.left(text.indexOf(' '));
        if (text.contains(" SELECT ") && c.authenticated)
            send(c, "* OK [PERMANENTFLAGS (" + permanent + ")] flags\r\n");
        if (text.contains("BODY.PEEK[HEADER.FIELDS") && c.authenticated) {
            const QByteArray body = "Subject: prepared\r\n\r\n";
            send(c, "* 1 FETCH (UID 7 BODY[HEADER.FIELDS (SUBJECT)] {" + QByteArray::number(body.size())
                 + "}\r\n" + body + ")\r\n" + tag + " OK fetched\r\n");
            return;
        }
        ImapFolderTestServer::line(c, text);
    }
};
template<typename Provider, typename Signal, typename Start>
QVariantMap waitFor(Provider &provider, Signal signal, Start start)
{
    QSignalSpy spy(&provider, signal);
    start();
    if (spy.isEmpty() && !spy.wait(10000)) return {};
    return spy.takeFirst().at(1).toMap();
}
}
class MailExtensionsTest : public QObject {
    Q_OBJECT
private slots:
    void keywordsAndNoncanonicalNames()
    {
        for (const QString &category : {QStringLiteral("North America"), QStringLiteral("a_b&c"),
                                       QStringLiteral("$Finance"), QString::fromUtf8("Grün;日本")}) {
            const QString keyword = ImapKeywords::keywordForCategory(category);
            QVERIFY(ImapKeywords::validFlag(keyword));
            QCOMPARE(ImapKeywords::categoryForKeyword(keyword), category);
        }
        QVERIFY(ImapKeywords::validFlag(QStringLiteral("$Label1")));
        QVERIFY(ImapKeywords::validFlag(QStringLiteral("a[+/:,;=@b")));
        QVERIFY(!ImapKeywords::validFlag(QStringLiteral("\\Recent")));
        QVERIFY(!ImapKeywords::validFlag(QStringLiteral("bad\r\nLOGOUT")));
        QVERIFY(!ImapKeywords::validFlag(QStringLiteral("bad]flag")));
        QCOMPARE(Imap::decodeMailboxName("&AEE-"), QStringLiteral("A"));
        QCOMPARE(ImapMessages::uidSet({7, 1, 3, 2, 7}), QByteArray("1:3,7"));
        QCOMPARE(ImapMessages::expandUidSet(QStringLiteral("1:3,7")), QList<qint64>({1,2,3,7}));
        QVERIFY(ImapMessages::expandUidSet(QStringLiteral("1:999999999999999"), 10).isEmpty());
    }
    void preparedSmtpAndDraftMetadata()
    {
        SmtpTestServer server;
        SmtpMailProvider provider;
        provider.setProperty("host", "127.0.0.1"); provider.setProperty("port", server.port());
        provider.setProperty("security", "none"); provider.setProperty("fromAddress", "from@example.test");
        const QByteArray prepared = "From: from@example.test\r\nTo: to@example.test\r\nSubject: prepared\r\n"
                                    "X-Preserved: exact  spacing\r\n\r\n.leading dot\r\n..two dots\r\n";
        const auto sent = waitFor(provider, &SmtpMailProvider::sendFinished, [&] {
            QVERIFY(provider.sendPrepared("raw", prepared, "from@example.test", {"to@example.test"}));
        });
        QVERIFY2(sent.value("ok").toBool(), qPrintable(sent.value("error").toString()));
        QCOMPARE(server.messages.size(), 1);
        QCOMPARE(server.messages.first(), prepared);
        const auto composed = provider.composeMessage({{"to", "to@example.test"}, {"replyTo", "Reply <reply@example.test>"},
                                                        {"importance", "high"}, {"body", "hello"}});
        QVERIFY(composed.value("ok").toBool());
        const auto parsed = Mime::parse(composed.value("message").toByteArray());
        QVERIFY(parsed.replyTo.contains("reply@example.test"));
        QCOMPARE(parsed.importance, QStringLiteral("high"));
        const auto invalid = waitFor(provider, &SmtpMailProvider::sendFinished, [&] {
            QVERIFY(!provider.sendPrepared("bad", "Subject: bare\n\ntext\n", "from@example.test", {"to@example.test"}));
        });
        QVERIFY(!invalid.value("ok").toBool());
        QCOMPARE(server.messages.size(), 1);
    }
    void imapFlagsHeadersAndCopies()
    {
        ExtensionServer server;
        server.user = "ada"; server.secret = "test-secret";
        server.add(7, "Subject: prepared\r\n\r\nhello\r\n", {"$Label1"});
        server.addFolder("Copies");
        LocalProductivityStore store;
        ImapMailProvider provider;
        provider.setStore(&store);
        provider.setProperty("host", "127.0.0.1"); provider.setProperty("port", server.port());
        provider.setProperty("security", "none"); provider.setProperty("userName", server.user);
        provider.setProperty("autoSave", false); provider.setProperty("keywordCategories", true);
        provider.setSecret(server.secret);
        auto result = waitFor(provider, &ImapMailProvider::syncFinished, [&] { provider.sync("sync"); });
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        const QString id = store.messages().first().toMap().value("id").toString();
        QCOMPARE(store.messages().first().toMap().value("keywords").toStringList(), QStringList{"$Label1"});
        result = waitFor(provider, &ImapMailProvider::flagsFinished, [&] { provider.updateFlags("flag", id, {"Project"}, {"$Label1"}); });
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QVERIFY(server.find(7)->flags.contains("Project"));
        QVERIFY(!server.find(7)->flags.contains("$Label1"));
        QCOMPARE(store.messages().first().toMap().value("categories").toStringList(), QStringList{"Project"});
        server.permanent = "\\Seen";
        result = waitFor(provider, &ImapMailProvider::flagsFinished, [&] { provider.updateFlags("denied", id, {"Blocked"}); });
        QVERIFY(!result.value("ok").toBool());
        QVERIFY(!server.find(7)->flags.contains("Blocked"));
        result = waitFor(provider, &ImapMailProvider::headersFinished, [&] { provider.fetchHeaders("headers", id, {"Subject"}); });
        QVERIFY(result.value("ok").toBool());
        QCOMPARE(result.value("headers").toMap().value("7").toByteArray(), QByteArray("Subject: prepared\r\n\r\n"));
        result = waitFor(provider, &ImapMailProvider::headersFinished, [&] {
            QVERIFY(!provider.fetchHeaders("bad", id, {"Subject) UID STORE 7"}));
        });
        QVERIFY(!result.value("ok").toBool());
        result = waitFor(provider, &ImapMailProvider::actionFinished, [&] { provider.copyMessage("copy", id, "Copies"); });
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(server.contents("Copies").size(), 1);
        QCOMPARE(store.messages().size(), 1);
        QCOMPARE(server.contents("INBOX").size(), 1);
    }
    void categoriesRoundTrip()
    {
        for (const QString &component : {QStringLiteral("VEVENT"), QStringLiteral("VTODO")}) {
            const QByteArray original = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:" + component.toLatin1()
                + "\r\nUID:one\r\nDTSTART:20261010T120000Z\r\nDTEND:20261010T130000Z\r\n"
                  "SUMMARY:Meeting\r\nCATEGORIES:Sales\\, West,Personal\r\nCATEGORIES:Travel\r\nEND:"
                + component.toLatin1() + "\r\nEND:VCALENDAR\r\n";
            auto mapped = ICal::toRecord(original, "UTC");
            QVERIFY2(mapped.ok, qPrintable(mapped.error));
            QCOMPARE(mapped.fields.value("categories").toStringList(), QStringList({"Sales, West", "Personal", "Travel"}));
            const QString digest = ICal::digest(component, mapped.fields);
            mapped.fields.insert("categories", QStringList{"Changed; group", "Back\\slash"});
            QVERIFY(ICal::digest(component, mapped.fields) != digest);
            const auto encoded = ICal::fromRecord(component, "one", mapped.fields, original);
            const auto reread = ICal::toRecord(encoded, "UTC");
            QCOMPARE(reread.fields.value("categories"), mapped.fields.value("categories"));
            QCOMPARE(encoded.count("CATEGORIES:"), 1);
        }
    }
    void calendarInvitationsAndReplies()
    {
        CalendarMail codec;
        const QByteArray calendar = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nMETHOD:REQUEST\r\nBEGIN:VEVENT\r\n"
            "UID:meeting-one\r\nSEQUENCE:3\r\nDTSTART:20261010T120000Z\r\nDTEND:20261010T130000Z\r\n"
            "SUMMARY:Planning\r\nORGANIZER:mailto:organizer@example.test\r\n"
            "ATTENDEE;CN=Ada;PARTSTAT=NEEDS-ACTION;RSVP=TRUE:mailto:ada@example.test\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
        const QVariantMap draft{{"from","organizer@example.test"},{"to","ada@example.test"},{"body","Planning invitation"}};
        for (const QString &method : {QStringLiteral("REQUEST"),QStringLiteral("REPLY"),QStringLiteral("CANCEL")}) {
            const auto message = codec.compose(method,calendar,draft);
            QVERIFY2(message.value("ok").toBool(),qPrintable(message.value("error").toString()));
            const auto bytes = message.value("message").toByteArray();
            QVERIFY(bytes.contains("multipart/alternative"));
            QVERIFY(bytes.contains("text/calendar"));
            QVERIFY(bytes.contains("method=" + method.toLatin1()));
            const auto parsed = codec.parse(bytes);
            QVERIFY2(parsed.value("ok").toBool(),qPrintable(parsed.value("error").toString()));
            QCOMPARE(parsed.value("method").toString(),method);
            QCOMPARE(parsed.value("uid").toString(),QStringLiteral("meeting-one"));
            QCOMPARE(parsed.value("sequence").toInt(),3);
            QVERIFY(!parsed.value("authenticated").toBool());
        }
        for (const QString &status : {QStringLiteral("ACCEPTED"),QStringLiteral("TENTATIVE"),QStringLiteral("DECLINED")}) {
            const auto reply = codec.reply(calendar,"ada@example.test",status);
            QVERIFY(reply.value("ok").toBool());
            const auto bytes=reply.value("calendar").toByteArray();
            QVERIFY(bytes.contains("METHOD:REPLY"));
            QVERIFY(bytes.contains("PARTSTAT="+status.toLatin1()));
            QVERIFY(bytes.contains("SEQUENCE:3"));
            QCOMPARE(bytes.count("ATTENDEE"),1);
        }
        QByteArray occurrence = calendar;
        occurrence.replace("SEQUENCE:3", "SEQUENCE:3\r\nRECURRENCE-ID:20261010T120000Z");
        const auto occurrenceReply = codec.reply(occurrence, "ada@example.test", "ACCEPTED");
        QVERIFY(occurrenceReply.value("ok").toBool());
        QVERIFY(occurrenceReply.value("calendar").toByteArray().contains("RECURRENCE-ID:20261010T120000Z"));
        QVERIFY(!codec.reply(calendar,"stranger@example.test","ACCEPTED").value("ok").toBool());
        QVERIFY(!codec.compose("REQUEST\r\nInjected:yes",calendar,draft).value("ok").toBool());
    }
    void popHtmlAndInlineImages()
    {
        const QByteArray raw = "From: a@example.test\r\nContent-Type: multipart/related; boundary=r\r\n\r\n"
            "--r\r\nContent-Type: text/html\r\n\r\n<p>Hello<img src=\"cid:picture\"></p>\r\n"
            "--r\r\nContent-Type: image/png\r\nContent-ID: <picture>\r\nContent-Disposition: inline\r\n"
            "Content-Transfer-Encoding: base64\r\n\r\naGVsbG8=\r\n--r--\r\n";
        Pop3Jobs::Job job;
        const auto safe = Pop3Jobs::messageRecord(job, "one", raw.size(), raw, false);
        QVERIFY(!safe.contains("bodyHtml"));
        job.keepHtml = true;
        const auto html = Pop3Jobs::messageRecord(job, "one", raw.size(), raw, false);
        QVERIFY(html.value("bodyHtml").toString().contains("cid:picture"));
        QCOMPARE(html.value("bodyTrustMode").toString(), QStringLiteral("untrusted-html"));
        QVERIFY(html.value("attachments").toList().first().toMap().value("inline").toBool());
    }
};
QTEST_MAIN(MailExtensionsTest)
#include "tst_mail_extensions.moc"
