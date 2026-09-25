// SPDX-License-Identifier: LGPL-3.0-or-later
#include "smtp_mail_provider.h"

#include "mail_mime.h"
#include "protocol_client.h"
#include "rich_text_codec.h"
#include "rich_text_html.h"

#include <QUuid>

namespace QindaTK {
namespace {
constexpr qsizetype MaximumMessageBytes = 32 * 1024 * 1024;

struct SmtpJob {
    Protocol::Endpoint endpoint;
    Protocol::Credentials credentials;
    QByteArray helo;
    QString from;
    QStringList recipients;
    QByteArray message;
};

QByteArray dotStuff(const QByteArray &message)
{
    QByteArray out;
    out.reserve(message.size() + message.size() / 64 + 8);
    bool lineStart = true;
    for (const char c : message) {
        if (lineStart && c == '.') out += '.';
        out += c;
        lineStart = c == '\n';
    }
    if (!out.endsWith("\r\n")) out += "\r\n";
    return out + ".\r\n";
}

QVariantMap runSmtp(const SmtpJob &job)
{
    Protocol::Channel channel;
    channel.addSecret(job.credentials.secret);
    QStringList accepted, refused;
    QString lastReply;
    int lastCode = 0;
    bool delivered = false;

    auto reply = [&](int *code, QStringList *lines) -> bool {
        lines->clear();
        for (;;) {
            QByteArray line;
            if (!channel.readLine(&line)) return false;
            bool digits = line.size() >= 3;
            for (int i = 0; digits && i < 3; ++i) digits = line.at(i) >= '0' && line.at(i) <= '9';
            if (!digits || (line.size() > 3 && line.at(3) != ' ' && line.at(3) != '-')
                || lines->size() > 100) {
                channel.fail(Protocol::Failure::Rejected, QStringLiteral("Malformed SMTP reply"));
                return false;
            }
            *code = line.left(3).toInt();
            lastCode = *code;
            lastReply = QString::fromUtf8(line);
            lines->append(QString::fromUtf8(line.mid(4)).trimmed());
            if (line.size() == 3 || line.at(3) == ' ') return true;
        }
    };
    auto expect = [&](int wanted, const QString &step) -> bool {
        int code = 0;
        QStringList lines;
        if (!reply(&code, &lines)) return false;
        if (code != wanted && !(wanted < 10 && code / 100 == wanted)) {
            channel.fail(Protocol::Failure::Rejected, QStringLiteral("%1 refused: %2 %3")
                                                          .arg(step).arg(code).arg(lines.join(QLatin1Char(' '))));
            return false;
        }
        return true;
    };
    auto finish = [&]() {
        QVariantMap result;
        const Protocol::Failure failure = delivered ? Protocol::Failure::None
            : channel.failure() == Protocol::Failure::None ? Protocol::Failure::Rejected : channel.failure();
        result.insert(QStringLiteral("ok"), delivered);
        result.insert(QStringLiteral("state"), Protocol::failureState(failure));
        result.insert(QStringLiteral("error"), delivered ? QString() : channel.error());
        result.insert(QStringLiteral("smtpCode"), lastCode);
        result.insert(QStringLiteral("serverReply"), Protocol::redact(lastReply, {job.credentials.secret}));
        result.insert(QStringLiteral("recipients"), accepted);
        result.insert(QStringLiteral("rejectedRecipients"), refused);
        result.insert(QStringLiteral("retryable"), !delivered
            && (failure == Protocol::Failure::Offline || (lastCode >= 400 && lastCode < 500)));
        result.insert(QStringLiteral("transcript"), channel.transcript());
        channel.close();
        return result;
    };

    QStringList capabilities;
    auto ehlo = [&]() -> bool {
        int code = 0;
        QStringList lines;
        if (!channel.write("EHLO " + job.helo + "\r\n") || !reply(&code, &lines)) return false;
        if (code != 250) {
            channel.fail(Protocol::Failure::Rejected, QStringLiteral("EHLO refused: %1").arg(code));
            return false;
        }
        capabilities.clear();
        for (int i = 1; i < lines.size(); ++i) capabilities.append(lines.at(i).toUpper());
        return true;
    };

    if (!channel.open(job.endpoint) || !expect(2, QStringLiteral("Greeting")) || !ehlo()) return finish();
    if (job.endpoint.security == QLatin1String("starttls")) {
        if (!capabilities.contains(QStringLiteral("STARTTLS"))) {
            // AGENT-GUARD: never fall back to plain text when TLS was requested.
            channel.fail(Protocol::Failure::Rejected,
                         QStringLiteral("Server does not offer STARTTLS; nothing was sent without encryption"));
            return finish();
        }
        if (!channel.write("STARTTLS\r\n") || !expect(2, QStringLiteral("STARTTLS"))
            || !channel.startTls() || !ehlo())
            return finish();
    }
    if (job.credentials.isSet()) {
        if (!channel.authAllowed()) {
            channel.fail(Protocol::Failure::Rejected,
                         QStringLiteral("Refusing to send credentials over an unencrypted connection"));
            return finish();
        }
        QStringList offered;
        for (const QString &capability : std::as_const(capabilities))
            if (capability.startsWith(QLatin1String("AUTH ")) || capability.startsWith(QLatin1String("AUTH=")))
                offered += capability.mid(5).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QString mechanism = job.credentials.mechanism.toUpper();
        if (mechanism.isEmpty() || mechanism == QLatin1String("AUTO"))
            mechanism = offered.contains(QStringLiteral("PLAIN")) ? QStringLiteral("PLAIN") : QStringLiteral("LOGIN");
        if (!offered.contains(mechanism)) {
            channel.fail(Protocol::Failure::Rejected,
                         QStringLiteral("Server does not offer AUTH %1").arg(mechanism));
            return finish();
        }
        const QByteArray user = job.credentials.user.toUtf8(), secret = job.credentials.secret.toUtf8();
        bool authenticated = false;
        if (mechanism == QLatin1String("PLAIN")) {
            const QByteArray payload = (QByteArray(1, '\0') + user + QByteArray(1, '\0') + secret).toBase64();
            channel.addSecret(QString::fromLatin1(payload));
            authenticated = channel.write("AUTH PLAIN " + payload + "\r\n", QStringLiteral("AUTH PLAIN <redacted>"))
                && expect(235, QStringLiteral("Authentication"));
        } else if (mechanism == QLatin1String("LOGIN")) {
            authenticated = channel.write("AUTH LOGIN\r\n") && expect(334, QStringLiteral("AUTH LOGIN"))
                && channel.write(user.toBase64() + "\r\n", QStringLiteral("<user>"))
                && expect(334, QStringLiteral("AUTH LOGIN"))
                && channel.write(secret.toBase64() + "\r\n", QStringLiteral("<redacted>"))
                && expect(235, QStringLiteral("Authentication"));
        } else if (mechanism == QLatin1String("XOAUTH2")) {
            const QByteArray payload = ("user=" + user + "\x01" "auth=Bearer " + secret + "\x01\x01").toBase64();
            channel.addSecret(QString::fromLatin1(payload));
            int code = 0;
            QStringList lines;
            if (channel.write("AUTH XOAUTH2 " + payload + "\r\n", QStringLiteral("AUTH XOAUTH2 <redacted>"))
                && reply(&code, &lines)) {
                if (code == 334 && channel.write("\r\n")) reply(&code, &lines);   // error challenge
                authenticated = code == 235;
                if (!authenticated && channel.failure() == Protocol::Failure::None)
                    channel.fail(Protocol::Failure::Rejected, QStringLiteral("Authentication refused: %1").arg(code));
            }
        } else {
            channel.fail(Protocol::Failure::Rejected, QStringLiteral("Unsupported AUTH %1").arg(mechanism));
        }
        if (!authenticated) return finish();
    }

    if (!channel.write("MAIL FROM:<" + job.from.toLatin1() + ">\r\n")
        || !expect(2, QStringLiteral("MAIL FROM")))
        return finish();
    for (const QString &recipient : job.recipients) {
        int code = 0;
        QStringList lines;
        if (!channel.write("RCPT TO:<" + recipient.toLatin1() + ">\r\n") || !reply(&code, &lines))
            return finish();
        if (code / 100 == 2) accepted.append(recipient);
        else refused.append(QStringLiteral("%1 (%2)").arg(recipient).arg(code));
    }
    if (!refused.isEmpty()) {
        // All or nothing: a partly addressed message is not delivered.
        int code = 0;
        QStringList lines;
        const int refusedCode = lastCode;
        if (channel.write("RSET\r\n")) reply(&code, &lines);
        lastCode = refusedCode;
        channel.fail(Protocol::Failure::Rejected,
                     QStringLiteral("Server refused recipients: ") + refused.join(QStringLiteral(", ")));
        return finish();
    }
    if (!channel.write("DATA\r\n") || !expect(354, QStringLiteral("DATA"))
        || !channel.write(dotStuff(job.message), QStringLiteral("<message %1 bytes>").arg(job.message.size()))
        || !expect(2, QStringLiteral("Message")))
        return finish();
    delivered = true;
    const QString deliveredReply = lastReply;
    const int deliveredCode = lastCode;
    int code = 0;
    QStringList lines;
    if (channel.write("QUIT\r\n")) reply(&code, &lines);     // a failed QUIT does not undo delivery
    lastReply = deliveredReply;
    lastCode = deliveredCode;
    return finish();
}

QVariantMap rejection(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("state"), QStringLiteral("rejected")},
            {QStringLiteral("error"), error}, {QStringLiteral("retryable"), false}};
}

QString htmlFromRichJson(const QString &json)
{
    if (json.trimmed().isEmpty()) return {};
    RichTextDocument document;
    if (!RichTextCodec::decode(json.toUtf8(), document)) return {};
    return RichTextHtml::fromFragment(document.extractFragment(document.startPosition(),
                                                               document.endPosition()));
}
} // namespace

SmtpMailProvider::SmtpMailProvider(QObject *parent) : QObject(parent) {}

void SmtpMailProvider::setAttachmentStore(LocalAttachmentStore *store)
{
    if (m_attachmentStore == store) return;
    m_attachmentStore = store;
    emit configChanged();
}

void SmtpMailProvider::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret = secret;
    m_mechanism = mechanism;
    emit secretChanged();
}

void SmtpMailProvider::clearSecret()
{
    m_secret.fill(QLatin1Char('\0'));
    m_secret.clear();
    emit secretChanged();
}

QVariantMap SmtpMailProvider::composeMessage(const QVariantMap &draft) const
{
    QString error;
    Mime::Outgoing message;
    message.from = {m_fromName, m_fromAddress.trimmed()};
    if (!Mime::validAddress(message.from.address))
        return rejection(QStringLiteral("The sender address is not configured or invalid"));
    message.to = Mime::parseAddressList(draft.value(QStringLiteral("to")).toString(), &error);
    if (error.isEmpty()) message.cc = Mime::parseAddressList(draft.value(QStringLiteral("cc")).toString(), &error);
    const QList<Mime::Address> bcc = error.isEmpty()
        ? Mime::parseAddressList(draft.value(QStringLiteral("bcc")).toString(), &error) : QList<Mime::Address>();
    if (!error.isEmpty()) return rejection(error);
    QStringList recipients;
    for (const QList<Mime::Address> *list :
         std::initializer_list<const QList<Mime::Address> *>{&message.to, &message.cc, &bcc})
        for (const Mime::Address &address : *list)
            if (!recipients.contains(address.address, Qt::CaseInsensitive)) recipients.append(address.address);
    if (recipients.isEmpty()) return rejection(QStringLiteral("The draft has no recipient"));

    message.subject = draft.value(QStringLiteral("subject")).toString();
    message.plain = draft.value(QStringLiteral("body")).toString();
    message.html = draft.value(QStringLiteral("bodyHtml")).toString();
    if (message.html.isEmpty())
        message.html = htmlFromRichJson(draft.value(QStringLiteral("richDocumentJson")).toString());
    message.inReplyTo = draft.value(QStringLiteral("inReplyTo")).toString();
    message.references = draft.value(QStringLiteral("references")).toString();
    const QString domain = message.from.address.section(QLatin1Char('@'), 1);
    message.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char('@') + domain;

    const QVariantList attachments = draft.value(QStringLiteral("attachments")).toList();
    for (const QVariant &value : attachments) {
        const QVariantMap entry = value.toMap();
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (!m_attachmentStore)
            return rejection(QStringLiteral("Attachment %1 cannot be read: no attachment store").arg(id));
        const QVariantMap payload = m_attachmentStore->read(id);
        if (!payload.value(QStringLiteral("ok")).toBool())
            return rejection(QStringLiteral("Attachment %1 is not available locally: %2")
                                 .arg(id, payload.value(QStringLiteral("error")).toString()));
        Mime::OutgoingAttachment attachment;
        attachment.name = entry.value(QStringLiteral("name"), payload.value(QStringLiteral("name"))).toString();
        attachment.mimeType = entry.value(QStringLiteral("mimeType"), payload.value(QStringLiteral("mimeType"))).toString();
        attachment.bytes = payload.value(QStringLiteral("bytes")).toByteArray();
        message.attachments.append(attachment);
    }
    const QByteArray bytes = Mime::build(message);
    if (bytes.size() > MaximumMessageBytes)
        return rejection(QStringLiteral("The message exceeds %1 bytes").arg(MaximumMessageBytes));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("message"), bytes},
            {QStringLiteral("messageId"), message.messageId}, {QStringLiteral("recipients"), recipients}};
}

bool SmtpMailProvider::send(const QString &requestId, const QVariantMap &draft)
{
    ++m_pending;
    m_state = QStringLiteral("sending");
    emit stateChanged();
    const QVariantMap composed = composeMessage(draft);
    if (!composed.value(QStringLiteral("ok")).toBool()) {
        QMetaObject::invokeMethod(this, [this, requestId, draft, composed] { finish(requestId, draft, composed); },
                                  Qt::QueuedConnection);
        return false;
    }
    SmtpJob job;
    job.endpoint = {m_host, static_cast<quint16>(m_port), m_security, m_timeoutMs, m_allowInsecureAuth};
    job.credentials = {m_userName, m_secret, m_mechanism};
    job.helo = m_heloName.toLatin1();
    job.from = m_fromAddress.trimmed();
    job.recipients = composed.value(QStringLiteral("recipients")).toStringList();
    job.message = composed.value(QStringLiteral("message")).toByteArray();
    const QString messageId = composed.value(QStringLiteral("messageId")).toString();
    Protocol::startWorker(this, [job] { return runSmtp(job); },
                          [this, requestId, draft, messageId](const QVariantMap &result) {
                              QVariantMap completed = result;
                              completed.insert(QStringLiteral("messageId"), messageId);
                              finish(requestId, draft, completed);
                          });
    return true;
}

void SmtpMailProvider::finish(const QString &requestId, const QVariantMap &draft, QVariantMap result)
{
    result.remove(QStringLiteral("message"));
    result.insert(QStringLiteral("requestId"), requestId);
    result.insert(QStringLiteral("draftId"), draft.value(QStringLiteral("id")).toString());
    m_pending = qMax(0, m_pending - 1);
    m_state = result.value(QStringLiteral("state")).toString();
    m_lastError = Protocol::redact(result.value(QStringLiteral("error")).toString(), {m_secret});
    result.insert(QStringLiteral("error"), m_lastError);
    emit stateChanged();
    emit sendFinished(requestId, result);
}

} // namespace QindaTK
