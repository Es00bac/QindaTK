// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_attachment_store.h"
#include "local_productivity_store.h"
#include "mail_mime.h"
#include "mail_test_servers.h"
#include "protocol_client.h"
#include "rich_text_codec.h"
#include "rich_text_document.h"
#include "smtp_mail_provider.h"

#include <QFile>
#include <QMetaProperty>
#include <QMutex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
const QString kSecret = QStringLiteral("s3cret-Pa55!");
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

void configure(SmtpMailProvider &provider, quint16 port, const QString &security = QStringLiteral("none"))
{
    provider.setProperty("host", QStringLiteral("127.0.0.1"));
    provider.setProperty("port", port);
    provider.setProperty("security", security);
    provider.setProperty("userName", QStringLiteral("ada"));
    provider.setProperty("fromAddress", QStringLiteral("ada@example.test"));
    provider.setProperty("fromName", QStringLiteral("Ada Lovelace"));
    provider.setProperty("timeoutMs", 10000);
}

QVariantMap sendAndWait(SmtpMailProvider &provider, const QString &requestId, const QVariantMap &draft)
{
    QSignalSpy spy(&provider, &SmtpMailProvider::sendFinished);
    provider.send(requestId, draft);
    if (!spy.wait(15000)) return {};
    return spy.takeFirst().at(1).toMap();
}

quint16 closedPort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

QString joined(const QVariantMap &result)
{
    return result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n'))
        + result.value(QStringLiteral("error")).toString() + result.value(QStringLiteral("serverReply")).toString();
}
} // namespace

class SmtpMailProviderTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { previousHandler = qInstallMessageHandler(captureMessages); }
    void cleanupTestCase() { qInstallMessageHandler(previousHandler); }

    void mimeHeadersAddressesAndTransferEncodings()
    {
        QString error;
        const QList<Mime::Address> list = Mime::parseAddressList(
            QStringLiteral("\"Lovelace, Ada\" <ada@example.test>; bob@example.test"), &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).name, QStringLiteral("Lovelace, Ada"));
        QCOMPARE(list.at(1).address, QStringLiteral("bob@example.test"));
        QVERIFY(Mime::parseAddressList(QStringLiteral("not an address"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!Mime::validAddress(QStringLiteral("a@b\r\nBcc: x@y")));

        const QByteArray encoded = Mime::encodeHeaderText(QStringLiteral("Plan für Q3"));
        QVERIFY(encoded.startsWith("=?UTF-8?B?"));
        QCOMPARE(Mime::decodeHeaderText(encoded), QStringLiteral("Plan für Q3"));
        QCOMPARE(Mime::decodeHeaderText("=?iso-8859-1?Q?caf=E9_ouvert?= today"), QStringLiteral("café ouvert today"));
        QCOMPARE(Mime::decodeTransfer("soft =\r\nbreak =3D ok", QStringLiteral("quoted-printable")),
                 QByteArray("soft break = ok"));
        // A line break in the subject cannot inject a header.
        Mime::Outgoing message;
        message.from = {QString(), QStringLiteral("ada@example.test")};
        message.to = {{QString(), QStringLiteral("bob@example.test")}};
        message.subject = QStringLiteral("Hello\r\nBcc: eve@example.test");
        message.plain = QStringLiteral("Body");
        const QByteArray built = Mime::build(message);
        QVERIFY(!built.contains("\r\nBcc:"));
        QCOMPARE(Mime::parse(built).subject, QStringLiteral("Hello Bcc: eve@example.test"));
    }

    void deliversRichDraftWithAttachmentAndKeepsSecretPrivate()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalAttachmentStore attachments;
        attachments.setRootPath(directory.filePath(QStringLiteral("attachments")));
        const QByteArray payload("id,total\r\n1,42\r\n\x00\xff", 18);
        QVERIFY(attachments.importBytes(QStringLiteral("att-1"), QStringLiteral("totals.csv"),
                                        QStringLiteral("text/csv"), payload).value(QStringLiteral("ok")).toBool());
        RichTextDocument rich;
        QVERIFY(rich.insertText(rich.startPosition(), QStringLiteral("Weekly plan")));
        SmtpTestServer server;
        server.user = QStringLiteral("ada");
        server.secret = kSecret;
        SmtpMailProvider provider;
        configure(provider, server.port());
        provider.setAttachmentStore(&attachments);
        provider.setSecret(kSecret);
        const QVariantMap draft{{QStringLiteral("id"), QStringLiteral("draft-7")},
            {QStringLiteral("to"), QStringLiteral("Bob <bob@example.test>")},
            {QStringLiteral("cc"), QStringLiteral("carol@example.test")},
            {QStringLiteral("bcc"), QStringLiteral("dave@example.test")},
            {QStringLiteral("subject"), QStringLiteral("Plan für Q3")},
            {QStringLiteral("body"), QStringLiteral("Hello\n.leading dot line\nBye")},
            {QStringLiteral("richDocumentJson"), QString::fromUtf8(RichTextCodec::encode(rich))},
            {QStringLiteral("attachments"), QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("att-1")},
                {QStringLiteral("name"), QStringLiteral("totals.csv")}, {QStringLiteral("mimeType"), QStringLiteral("text/csv")}}}}};
        const QVariantMap result = sendAndWait(provider, QStringLiteral("send-1"), draft);
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("accepted"));
        QCOMPARE(result.value(QStringLiteral("requestId")).toString(), QStringLiteral("send-1"));
        QCOMPARE(result.value(QStringLiteral("draftId")).toString(), QStringLiteral("draft-7"));
        QCOMPARE(result.value(QStringLiteral("smtpCode")).toInt(), 250);
        QCOMPARE(server.envelopeFrom, QStringLiteral("ada@example.test"));
        QCOMPARE(server.envelopeRecipients, (QStringList{QStringLiteral("bob@example.test"),
                 QStringLiteral("carol@example.test"), QStringLiteral("dave@example.test")}));
        QCOMPARE(server.messages.size(), 1);

        const QByteArray delivered = server.messages.first();
        QVERIFY(!delivered.contains("dave@example.test"));          // Bcc stays off the headers
        const Mime::Parsed parsed = Mime::parse(delivered);
        QCOMPARE(parsed.subject, QStringLiteral("Plan für Q3"));
        QCOMPARE(parsed.fromAddress, QStringLiteral("ada@example.test"));
        QCOMPARE(parsed.plain, QStringLiteral("Hello\n.leading dot line\nBye"));
        QVERIFY(parsed.html.contains(QStringLiteral("Weekly plan")));
        QCOMPARE(parsed.messageId, result.value(QStringLiteral("messageId")).toString());
        QCOMPARE(parsed.attachments.size(), 1);
        QCOMPARE(parsed.attachments.first().name, QStringLiteral("totals.csv"));
        QCOMPARE(parsed.attachments.first().size, qint64(payload.size()));

        // The secret never leaves the provider except on the wire.
        QVERIFY(server.log.join(QLatin1Char('\n')).contains(QStringLiteral("AUTH PLAIN ")));
        QVERIFY(!joined(result).contains(kSecret));
        QVERIFY(!joined(result).contains(QString::fromLatin1((QByteArray(1, '\0') + "ada" + QByteArray(1, '\0') + kSecret.toUtf8()).toBase64())));
        QVERIFY(joined(result).contains(QStringLiteral("AUTH PLAIN <redacted>")));
        for (int i = 0; i < provider.metaObject()->propertyCount(); ++i)
            QVERIFY(provider.metaObject()->property(i).read(&provider).toString() != kSecret);
        QMutexLocker lock(&logMutex);
        for (const QString &line : std::as_const(capturedLog)) QVERIFY(!line.contains(kSecret));
    }

    void refusedRecipientSendsNothingAndPreservesDraft()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalAttachmentStore attachments;
        attachments.setRootPath(directory.filePath(QStringLiteral("attachments")));
        QVERIFY(attachments.importBytes(QStringLiteral("att-2"), QStringLiteral("a.txt"), QStringLiteral("text/plain"),
                                        QByteArray("keep me")).value(QStringLiteral("ok")).toBool());
        LocalProductivityStore store;
        store.setFilePath(directory.filePath(QStringLiteral("store.json")));
        const QVariantMap draft{{QStringLiteral("id"), QStringLiteral("draft-9")},
            {QStringLiteral("to"), QStringLiteral("bob@example.test, nobody@example.test")},
            {QStringLiteral("subject"), QStringLiteral("Numbers")}, {QStringLiteral("body"), QStringLiteral("See attached")},
            {QStringLiteral("attachments"), QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("att-2")},
                {QStringLiteral("name"), QStringLiteral("a.txt")}, {QStringLiteral("mimeType"), QStringLiteral("text/plain")}}}}};
        QVERIFY(store.upsert(QStringLiteral("drafts"), draft).value(QStringLiteral("ok")).toBool());
        QVERIFY(store.save());

        SmtpTestServer server;
        server.user = QStringLiteral("ada");
        server.secret = kSecret;
        server.refuse = {QStringLiteral("nobody@example.test")};
        SmtpMailProvider provider;
        configure(provider, server.port());
        provider.setAttachmentStore(&attachments);
        provider.setSecret(kSecret, QStringLiteral("login"));
        const QVariantMap result = sendAndWait(provider, QStringLiteral("send-2"), store.drafts().first().toMap());
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QCOMPARE(result.value(QStringLiteral("rejectedRecipients")).toStringList(),
                 QStringList{QStringLiteral("nobody@example.test (550)")});
        QVERIFY(server.messages.isEmpty());
        QVERIFY(server.log.contains(QStringLiteral("RSET")));
        QVERIFY(!server.log.contains(QStringLiteral("DATA")));
        QVERIFY(joined(result).contains(QStringLiteral("<redacted>")));
        QVERIFY(!joined(result).contains(QString::fromLatin1(kSecret.toUtf8().toBase64())));

        LocalProductivityStore reopened;
        reopened.setFilePath(store.filePath());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.drafts().first().toMap(), draft);
        QCOMPARE(attachments.read(QStringLiteral("att-2")).value(QStringLiteral("bytes")).toByteArray(), QByteArray("keep me"));
        QFile saved(store.filePath());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(!saved.readAll().contains(kSecret.toUtf8()));
    }

    void offlineStartTlsAndAuthenticationFailures()
    {
        SmtpMailProvider offline;
        configure(offline, closedPort());
        const QVariantMap draft{{QStringLiteral("to"), QStringLiteral("bob@example.test")},
                                {QStringLiteral("subject"), QStringLiteral("Hi")}, {QStringLiteral("body"), QStringLiteral("x")}};
        QVariantMap result = sendAndWait(offline, QStringLiteral("send-3"), draft);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());

        SmtpTestServer plain;                  // offers no STARTTLS
        plain.user = QStringLiteral("ada");
        plain.secret = kSecret;
        SmtpMailProvider tls;
        configure(tls, plain.port(), QStringLiteral("starttls"));
        tls.setSecret(kSecret);
        result = sendAndWait(tls, QStringLiteral("send-4"), draft);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("STARTTLS")));
        for (const QString &line : std::as_const(plain.log)) QVERIFY(!line.startsWith(QStringLiteral("AUTH")));

        SmtpMailProvider wrong;
        configure(wrong, plain.port());
        wrong.setSecret(QStringLiteral("not-the-password"));
        result = sendAndWait(wrong, QStringLiteral("send-5"), draft);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QCOMPARE(result.value(QStringLiteral("smtpCode")).toInt(), 535);
        QVERIFY(!joined(result).contains(QStringLiteral("not-the-password")));
        QVERIFY(plain.messages.isEmpty());

        const QVariantMap invalid = sendAndWait(wrong, QStringLiteral("send-6"),
            {{QStringLiteral("to"), QStringLiteral("no address")}, {QStringLiteral("subject"), QStringLiteral("x")}});
        QCOMPARE(invalid.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));

        QVERIFY(!Protocol::plaintextAuthAllowed(QStringLiteral("mail.example.test"), false, false));
        QVERIFY(Protocol::plaintextAuthAllowed(QStringLiteral("mail.example.test"), true, false));
        QVERIFY(Protocol::plaintextAuthAllowed(QStringLiteral("mail.example.test"), false, true));
        QVERIFY(Protocol::plaintextAuthAllowed(QStringLiteral("[::1]"), false, false));
        QVERIFY(Protocol::plaintextAuthAllowed(QStringLiteral("localhost"), false, false));
    }

    void xoauth2TokenFromHostAdapter()
    {
        SmtpTestServer server;
        server.user = QStringLiteral("ada@example.test");
        server.secret = QStringLiteral("ya29.host-token");
        server.mechanisms = {QStringLiteral("XOAUTH2")};
        SmtpMailProvider provider;
        configure(provider, server.port());
        provider.setProperty("userName", QStringLiteral("ada@example.test"));
        provider.setSecret(QStringLiteral("ya29.host-token"), QStringLiteral("xoauth2"));
        const QVariantMap draft{{QStringLiteral("to"), QStringLiteral("bob@example.test")},
                                {QStringLiteral("subject"), QStringLiteral("Token")}, {QStringLiteral("body"), QStringLiteral("x")}};
        QVariantMap result = sendAndWait(provider, QStringLiteral("send-7"), draft);
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QVERIFY(!joined(result).contains(QStringLiteral("ya29.host-token")));

        provider.setSecret(QStringLiteral("expired-token"), QStringLiteral("xoauth2"));
        result = sendAndWait(provider, QStringLiteral("send-8"), draft);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QCOMPARE(result.value(QStringLiteral("smtpCode")).toInt(), 535);
        QCOMPARE(server.messages.size(), 1);
    }
};

QTEST_MAIN(SmtpMailProviderTest)
#include "tst_smtp_mail_provider.moc"
