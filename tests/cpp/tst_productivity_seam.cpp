// SPDX-License-Identifier: LGPL-3.0-or-later
// Req 4 at the view level: examples/local-productivity/Main.qml (the five
// views plus ProductivityHost) runs against in-process SMTP, IMAP and CalDAV
// servers. Each case drives a view request (as a click would) and checks
// that it travels through the provider to the server and back into the
// view's acknowledgment, and that the local store holds the result.
#include "caldav_test_server.h"
#include "mail_test_servers.h"

#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString kSmtpSecret = QStringLiteral("smtp-Seam-1"), kImapSecret = QStringLiteral("imap-Seam-2"),
              kDavSecret = QStringLiteral("dav-Seam-3");
constexpr int kWait = 15000;

const QByteArray kRoot = "From: Bob <bob@example.test>\r\nTo: ada@example.test\r\nSubject: Totals\r\n"
                         "Date: Thu, 24 Sep 2026 09:15:00 -0600\r\nMessage-ID: <root-1@example.test>\r\n\r\nTotals attached soon.\r\n";
const QByteArray kReply = "From: Carol <carol@example.test>\r\nTo: ada@example.test\r\nSubject: Re: Totals\r\n"
                          "Date: Thu, 24 Sep 2026 10:15:00 -0600\r\nMessage-ID: <reply-2@example.test>\r\n"
                          "In-Reply-To: <root-1@example.test>\r\nReferences: <root-1@example.test>\r\nMIME-Version: 1.0\r\n"
                          "Content-Type: multipart/mixed; boundary=\"b1\"\r\n\r\n--b1\r\nContent-Type: text/plain\r\n\r\n"
                          "See the attached file.\r\n--b1\r\nContent-Type: text/csv; name=\"report.csv\"\r\n"
                          "Content-Disposition: attachment; filename=\"report.csv\"\r\nContent-Transfer-Encoding: base64\r\n\r\n"
                          "aWQsdG90YWwKMSw0Mgo=\r\n--b1--\r\n";

QByteArray taskIcs(const QByteArray &summary)
{
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VTODO\r\nUID:task-remote\r\nSUMMARY:" + summary
        + "\r\nSTATUS:NEEDS-ACTION\r\nX-QINDATK-ORDER:0\r\nEND:VTODO\r\nEND:VCALENDAR\r\n";
}
const QByteArray kStandup = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:standup\r\nSUMMARY:Standup\r\n"
                            "DTSTART;TZID=America/Denver:20260922T090000\r\nDTEND;TZID=America/Denver:20260922T091500\r\n"
                            "RRULE:FREQ=WEEKLY\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

QVariant call(QObject *object, const char *method, const QVariantList &arguments = {})
{
    QVariant result;
    bool ok = false;
    switch (arguments.size()) {
    case 0: ok = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result)); break;
    case 1: ok = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, arguments.at(0))); break;
    case 2: ok = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, arguments.at(0)),
                                           Q_ARG(QVariant, arguments.at(1))); break;
    case 3: ok = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, arguments.at(0)),
                                           Q_ARG(QVariant, arguments.at(1)), Q_ARG(QVariant, arguments.at(2))); break;
    default: ok = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, arguments.at(0)),
                                            Q_ARG(QVariant, arguments.at(1)), Q_ARG(QVariant, arguments.at(2)),
                                            Q_ARG(QVariant, arguments.at(3))); break;
    }
    if (!ok) qFatal("invokeMethod %s failed", method);
    return result.metaType().id() == qMetaTypeId<QJSValue>() ? result.value<QJSValue>().toVariant() : result;
}

QVariantMap map(const QVariant &value)
{
    return value.metaType().id() == qMetaTypeId<QJSValue>() ? value.value<QJSValue>().toVariant().toMap() : value.toMap();
}
QVariantList list(const QVariant &value)
{
    return value.metaType().id() == qMetaTypeId<QJSValue>() ? value.value<QJSValue>().toVariant().toList() : value.toList();
}
QVariantMap row(const QVariant &rows, const QString &field, const QString &value)
{
    for (const QVariant &entry : list(rows))
        if (map(entry).value(field).toString() == value) return map(entry);
    return {};
}
} // namespace

class ProductivitySeamTest : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QVERIFY(m_directory.isValid());
        m_smtp = std::make_unique<SmtpTestServer>();
        m_smtp->user = QStringLiteral("ada");
        m_smtp->secret = kSmtpSecret;
        m_imap = std::make_unique<ImapTestServer>();
        m_imap->user = QStringLiteral("ada");
        m_imap->secret = kImapSecret;
        m_imap->add(21, kRoot);
        m_imap->add(22, kReply, {QStringLiteral("\\Seen")});
        m_dav = std::make_unique<CalDavTestServer>();
        m_dav->authorization = "Basic " + QByteArray("ada:" + kDavSecret.toUtf8()).toBase64();
        m_dav->put(m_dav->path(QStringLiteral("task-remote.ics")), taskIcs("Pack cables"));
        m_dav->put(m_dav->path(QStringLiteral("standup.ics")), kStandup);

        m_engine = std::make_unique<QQmlEngine>();
        m_engine->addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(QINDATK_LOCAL_PRODUCTIVITY_FIXTURE)));
        const QString run = QString::number(++m_run);
        m_root.reset(component.createWithInitialProperties({
            {QStringLiteral("storePath"), m_directory.filePath(run + QStringLiteral("/store.json"))},
            {QStringLiteral("attachmentPath"), m_directory.filePath(run + QStringLiteral("/attachments"))},
            {QStringLiteral("smtpSettings"), QVariantMap{{QStringLiteral("host"), QStringLiteral("127.0.0.1")},
                {QStringLiteral("port"), m_smtp->port()}, {QStringLiteral("security"), QStringLiteral("none")},
                {QStringLiteral("userName"), QStringLiteral("ada")}, {QStringLiteral("fromAddress"), QStringLiteral("ada@example.test")},
                {QStringLiteral("timeoutMs"), 10000}}},
            {QStringLiteral("imapSettings"), QVariantMap{{QStringLiteral("host"), QStringLiteral("127.0.0.1")},
                {QStringLiteral("port"), m_imap->port()}, {QStringLiteral("security"), QStringLiteral("none")},
                {QStringLiteral("userName"), QStringLiteral("ada")}, {QStringLiteral("accountId"), QStringLiteral("work-mail")},
                {QStringLiteral("timeoutMs"), 10000}}},
            {QStringLiteral("caldavSettings"), QVariantMap{{QStringLiteral("collectionUrl"), m_dav->url()},
                {QStringLiteral("userName"), QStringLiteral("ada")}, {QStringLiteral("accountId"), QStringLiteral("work")},
                {QStringLiteral("calendarId"), QStringLiteral("work")}, {QStringLiteral("defaultZone"), QStringLiteral("America/Denver")},
                {QStringLiteral("timeoutMs"), 10000}}}}));
        QVERIFY2(m_root, qPrintable(component.errorString()));
        m_host = qvariant_cast<QObject *>(m_root->property("host"));
        QVERIFY(m_host);
        call(m_host, "setSecrets", {kSmtpSecret, kImapSecret, kDavSecret, QVariantMap()});
        for (const char *name : {"mailView", "richMailComposer", "calendarView", "taskBoard", "recurrenceEditor"})
            QVERIFY2(m_root->findChild<QObject *>(QString::fromLatin1(name)), name);
    }
    void cleanup()
    {
        m_root.reset();
        m_engine.reset();
        // Credentials must not reach the saved store in any case.
        QFile saved(m_directory.filePath(QString::number(m_run) + QStringLiteral("/store.json")));
        if (saved.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = saved.readAll();
            for (const QString &secret : {kSmtpSecret, kImapSecret, kDavSecret}) QVERIFY(!bytes.contains(secret.toUtf8()));
        }
    }

    // OFF-28 through ImapMailProvider: refresh, a flag action and a lazy
    // attachment download, each acknowledged by the server's result.
    void mailViewRefreshFlagAndAttachmentThroughImap()
    {
        QObject *mail = m_root->findChild<QObject *>(QStringLiteral("mailView"));
        QVERIFY(call(mail, "requestRefresh").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(mail->property("requestState").toString(), QStringLiteral("accepted"), kWait);
        const QString rootId = QStringLiteral("imap:work-mail:INBOX:1000:21"), replyId = QStringLiteral("imap:work-mail:INBOX:1000:22");
        const QVariantMap thread = row(mail->property("threads"), QStringLiteral("threadId"), QStringLiteral("root-1@example.test"));
        QCOMPARE(list(thread.value(QStringLiteral("messages"))).size(), 2);
        QVERIFY(call(mail, "selectMessage", {replyId}).toBool());

        QVERIFY(call(mail, "requestAction", {rootId, QStringLiteral("flag")}).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(mail->property("requestState").toString(), QStringLiteral("accepted"), kWait);
        QVERIFY(m_imap->find(21)->flags.contains(QStringLiteral("\\Flagged")));
        QVERIFY(row(mail->property("messages"), QStringLiteral("messageId"), rootId).value(QStringLiteral("flagged")).toBool());

        const QString attachmentId = replyId + QStringLiteral("/2");
        const QString requestId = call(mail, "requestAttachment", {replyId, attachmentId}).toString();
        QVERIFY(requestId.startsWith(QStringLiteral("mail-attachment")));
        QTRY_COMPARE_WITH_TIMEOUT(map(mail->property("attachmentJobs")).value(requestId).toMap().value(QStringLiteral("state")).toString(),
                                  QStringLiteral("local"), kWait);
        QVERIFY(m_imap->log.join(QLatin1Char('\n')).contains(QStringLiteral("BODY.PEEK[2]")));
        const QVariantMap stored = row(mail->property("messages"), QStringLiteral("messageId"), replyId);
        QCOMPARE(list(stored.value(QStringLiteral("attachments"))).value(0).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("local"));

        // Offline: the provider's result reaches the view as "offline".
        m_imap.reset();
        QVERIFY(call(mail, "requestRefresh").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(mail->property("requestState").toString(), QStringLiteral("offline"), kWait);
        QVERIFY(!row(mail->property("messages"), QStringLiteral("messageId"), rootId).isEmpty());
    }

    // OFF-29 through SmtpMailProvider: tokens, an attachment job and a send.
    void composerSendsThroughSmtpAndPreservesDraftOnFailure()
    {
        QObject *composer = m_root->findChild<QObject *>(QStringLiteral("richMailComposer"));
        call(composer, "editDraft", {QStringLiteral("to"), QStringLiteral("not an address")});
        QVERIFY(!composer->property("validDraft").toBool());
        QVERIFY(!call(composer, "requestSend").toBool());
        QVERIFY(m_smtp->messages.isEmpty());

        call(composer, "editDraft", {QStringLiteral("to"), QStringLiteral("Bob <bob@example.test>")});
        call(composer, "editDraft", {QStringLiteral("subject"), QStringLiteral("Seam test")});
        const QString addId = call(composer, "requestAttachmentAdd").toString();
        QVERIFY(list(composer->property("attachmentJobs")).isEmpty());          // acknowledged by the host
        QCOMPARE(list(map(call(composer, "snapshot")).value(QStringLiteral("attachments"))).size(), 1);
        QVERIFY(!addId.isEmpty());

        // Unreachable server: "offline", the draft and its attachment stay.
        const QVariantMap settings = map(m_root->property("smtpSettings"));
        QObject *smtp = m_root->findChild<QObject *>(QStringLiteral("smtpProvider"));
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        const quint16 closed = probe.serverPort();
        probe.close();
        smtp->setProperty("port", closed);
        QVERIFY(call(composer, "requestSend").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(composer->property("status").toString(), QStringLiteral("offline"), kWait);
        QCOMPARE(map(call(composer, "snapshot")).value(QStringLiteral("subject")).toString(), QStringLiteral("Seam test"));
        QCOMPARE(list(map(call(composer, "snapshot")).value(QStringLiteral("attachments"))).size(), 1);

        smtp->setProperty("port", settings.value(QStringLiteral("port")));
        QVERIFY(call(composer, "requestSend").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(composer->property("status").toString(), QStringLiteral("accepted"), kWait);
        QCOMPARE(m_smtp->messages.size(), 1);
        const QByteArray sent = m_smtp->messages.first();
        QVERIFY(sent.contains("Subject: Seam test"));
        QVERIFY(sent.contains("agenda-notes.txt"));
        QCOMPARE(m_smtp->envelopeRecipients, QStringList{QStringLiteral("bob@example.test")});
        QVERIFY(composer->property("statusMessage").toString().startsWith(QStringLiteral("Sent")));
        QObject *store = m_root->findChild<QObject *>(QStringLiteral("productivityStore"));
        QVERIFY(row(store->property("drafts"), QStringLiteral("subject"), QStringLiteral("Seam test")).value(QStringLiteral("sent")).toBool());
    }

    // OFF-30/31/32 through CalDavProvider: a board move, a conflict and its
    // resolution, a single-occurrence move and a rule edit, each written to
    // the server before the view is acknowledged.
    void boardCalendarAndRecurrenceThroughCalDav()
    {
        QObject *caldav = m_root->findChild<QObject *>(QStringLiteral("caldavProvider"));
        QSignalSpy synced(caldav, SIGNAL(syncFinished(QString,QVariantMap)));
        call(m_host, "syncCalendar", {QStringLiteral("sync-initial")});
        QVERIFY(synced.wait(kWait));
        QVERIFY(map(synced.takeFirst().at(1)).value(QStringLiteral("ok")).toBool());

        QObject *board = m_root->findChild<QObject *>(QStringLiteral("taskBoard"));
        const QString taskId = QStringLiteral("caldav:work:task-remote");
        const QString taskPath = m_dav->path(QStringLiteral("task-remote.ics"));
        QVERIFY(call(board, "move", {taskId, QStringLiteral("todo"), QStringLiteral("doing"), 0}).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(board->property("requestState").toString(), QStringLiteral("accepted"), kWait);
        QVERIFY(m_dav->body(taskPath).contains("STATUS:IN-PROCESS"));

        m_dav->put(taskPath, taskIcs("Pack cables (server)"));
        QVERIFY(call(board, "move", {taskId, QStringLiteral("doing"), QStringLiteral("done"), 0}).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(board->property("requestState").toString(), QStringLiteral("conflict"), kWait);
        QCOMPARE(list(board->property("taskConflicts")).size(), 1);
        QVERIFY(m_dav->body(taskPath).contains("SUMMARY:Pack cables (server)"));
        QVERIFY(call(board, "resolveConflict", {taskId, QStringLiteral("local")}).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(board->property("requestState").toString(), QStringLiteral("accepted"), kWait);
        QVERIFY(m_dav->body(taskPath).contains("STATUS:COMPLETED"));
        QCOMPARE(list(board->property("taskConflicts")).size(), 0);

        QObject *calendar = m_root->findChild<QObject *>(QStringLiteral("calendarView"));
        const QString eventId = QStringLiteral("caldav:work:standup");
        const QVariantMap occurrence = row(calendar->property("events"), QStringLiteral("occurrenceId"),
                                           eventId + QStringLiteral("/2026-09-29T09:00:00"));
        QVERIFY(!occurrence.isEmpty());
        calendar->setProperty("moveScope", QStringLiteral("occurrence"));
        QVERIFY(call(calendar, "proposeMove", {occurrence, 1}).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(calendar->property("requestState").toString(), QStringLiteral("accepted"), kWait);
        const QByteArray standup = m_dav->body(m_dav->path(QStringLiteral("standup.ics")));
        QVERIFY(standup.contains("RECURRENCE-ID;TZID=America/Denver:20260929T090000\r\n"));
        QVERIFY(standup.contains("DTSTART;TZID=America/Denver:20260929T100000\r\n"));
        QCOMPARE(row(calendar->property("events"), QStringLiteral("occurrenceId"), eventId + QStringLiteral("/2026-09-29T09:00:00"))
                     .value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-09-29 10:00"));

        QObject *recurrence = m_root->findChild<QObject *>(QStringLiteral("recurrenceEditor"));
        QVERIFY(call(m_host, "selectRecurrence", {eventId}).toBool());
        call(recurrence, "propose", {QStringLiteral("interval"), 2});
        QTRY_COMPARE_WITH_TIMEOUT(recurrence->property("requestState").toString(), QStringLiteral("accepted"), kWait);
        QVERIFY(m_dav->body(m_dav->path(QStringLiteral("standup.ics"))).contains("RRULE:FREQ=WEEKLY;INTERVAL=2\r\n"));
        // The projection re-expanded: 29 Sep is no longer an occurrence.
        QVERIFY(row(calendar->property("events"), QStringLiteral("occurrenceId"), eventId + QStringLiteral("/2026-09-29T09:00:00")).isEmpty());
    }

private:
    QTemporaryDir m_directory;
    int m_run = 0;
    std::unique_ptr<SmtpTestServer> m_smtp;
    std::unique_ptr<ImapTestServer> m_imap;
    std::unique_ptr<CalDavTestServer> m_dav;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QObject> m_root;
    QObject *m_host = nullptr;
};

QTEST_MAIN(ProductivitySeamTest)
#include "tst_productivity_seam.moc"
