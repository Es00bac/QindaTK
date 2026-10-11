// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "local_attachment_store.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// SMTP delivery (RFC 5321 subset: EHLO, STARTTLS, AUTH PLAIN/LOGIN/XOAUTH2,
// MAIL/RCPT/DATA) for RichMailComposer drafts. The exchange runs off the GUI
// thread; `sendFinished` carries the correlated acknowledgment.
// AGENT-CONTRACT (RichMailComposer.qml): result.state is one of "accepted",
// "rejected" or "offline", the values RichMailComposer.acknowledge() shows.
// AGENT-GUARD: the secret is held only in memory. It has no READ accessor, is
// never put in a result, transcript or saved record, and is redacted from any
// server text; tst_smtp_mail_provider checks results, logs and saved files.
class SmtpMailProvider : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString host MEMBER m_host NOTIFY configChanged)
    Q_PROPERTY(int port MEMBER m_port NOTIFY configChanged)
    Q_PROPERTY(QString security MEMBER m_security NOTIFY configChanged)
    Q_PROPERTY(QString userName MEMBER m_userName NOTIFY configChanged)
    Q_PROPERTY(QString fromAddress MEMBER m_fromAddress NOTIFY configChanged)
    Q_PROPERTY(QString fromName MEMBER m_fromName NOTIFY configChanged)
    Q_PROPERTY(QString heloName MEMBER m_heloName NOTIFY configChanged)
    Q_PROPERTY(int timeoutMs MEMBER m_timeoutMs NOTIFY configChanged)
    Q_PROPERTY(bool allowInsecureAuth MEMBER m_allowInsecureAuth NOTIFY configChanged)
    Q_PROPERTY(LocalAttachmentStore *attachmentStore READ attachmentStore
                   WRITE setAttachmentStore NOTIFY configChanged)
    Q_PROPERTY(bool hasSecret READ hasSecret NOTIFY secretChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
public:
    explicit SmtpMailProvider(QObject *parent = nullptr);

    LocalAttachmentStore *attachmentStore() const { return m_attachmentStore; }
    void setAttachmentStore(LocalAttachmentStore *store);
    bool hasSecret() const { return !m_secret.isEmpty(); }
    bool busy() const { return m_pending > 0; }
    QString state() const { return m_state; }
    QString lastError() const { return m_lastError; }

    // mechanism: "auto" (PLAIN, else LOGIN), "plain", "login" or "xoauth2"
    // (secret is then a host-obtained OAuth access token).
    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("auto"));
    Q_INVOKABLE void clearSecret();
    // Validates and builds the MIME message on this thread, then delivers it.
    // Returns false when the draft was rejected before connecting; the result
    // still arrives through sendFinished. The draft itself is never modified.
    Q_INVOKABLE bool send(const QString &requestId, const QVariantMap &draft);
    // Prepared RFC 5322 bytes are not rebuilt: only SMTP dot-stuffing is
    // applied. The envelope is explicit, so hidden recipients stay private.
    // Requires CRLF lines and a terminating CRLF; otherwise rejects locally.
    Q_INVOKABLE bool sendPrepared(const QString &requestId, const QByteArray &message,
                                  const QString &envelopeFrom, const QStringList &recipients);
    // The RFC 5322 bytes send() would deliver (for preview and tests).
    Q_INVOKABLE QVariantMap composeMessage(const QVariantMap &draft) const;

signals:
    void configChanged();
    void secretChanged();
    void stateChanged();
    void sendFinished(const QString &requestId, const QVariantMap &result);

private:
    void finish(const QString &requestId, const QVariantMap &draft, QVariantMap result);

    QString m_host, m_security = QStringLiteral("starttls"), m_userName, m_fromAddress, m_fromName;
    QString m_heloName = QStringLiteral("localhost");
    int m_port = 587, m_timeoutMs = 15000;
    bool m_allowInsecureAuth = false;
    QPointer<LocalAttachmentStore> m_attachmentStore;
    QString m_secret, m_mechanism = QStringLiteral("auto");
    int m_pending = 0;
    QString m_state = QStringLiteral("idle"), m_lastError;
};

} // namespace QindaTK
