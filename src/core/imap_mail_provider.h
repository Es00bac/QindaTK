// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "local_attachment_store.h"
#include "local_productivity_store.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
namespace ImapJobs { struct Job; }

// IMAP mailbox sync into LocalProductivityStore messages, remote flag/archive/
// move/delete actions for MailView, on-demand attachment download into
// LocalAttachmentStore, the folder list (LIST + SPECIAL-USE + STATUS) and
// APPEND (a sent/draft copy). Each operation is one worker-thread IMAP session.
// Mailbox names are UTF-8 display names; the wire uses modified UTF-7.
// AGENT-CONTRACT (MailView.qml, docs/productivity-providers.md): message
// records keep `id == messageId == "imap:<accountId>:<mailbox>:<uidValidity>:<uid>"`
// plus remoteAccount/remoteMailbox/remoteUidValidity/remoteUid; result.state
// is "accepted", "rejected", "offline" or "conflict" (stale UIDVALIDITY or a
// store stale-writer rejection) and is passed to MailView.acknowledge().
// AGENT-GUARD: the secret lives only in memory (no READ accessor) and is
// redacted from every result, error and transcript.
class ImapMailProvider : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString host MEMBER m_host NOTIFY configChanged)
    Q_PROPERTY(int port MEMBER m_port NOTIFY configChanged)
    Q_PROPERTY(QString security MEMBER m_security NOTIFY configChanged)
    Q_PROPERTY(QString userName MEMBER m_userName NOTIFY configChanged)
    Q_PROPERTY(QString accountId MEMBER m_accountId NOTIFY configChanged)
    Q_PROPERTY(QString mailbox MEMBER m_mailbox NOTIFY configChanged)
    Q_PROPERTY(QString archiveMailbox MEMBER m_archiveMailbox NOTIFY configChanged)
    Q_PROPERTY(int timeoutMs MEMBER m_timeoutMs NOTIFY configChanged)
    Q_PROPERTY(int fetchLimit MEMBER m_fetchLimit NOTIFY configChanged)
    Q_PROPERTY(int maximumMessageBytes MEMBER m_maximumMessageBytes NOTIFY configChanged)
    Q_PROPERTY(bool allowInsecureAuth MEMBER m_allowInsecureAuth NOTIFY configChanged)
    Q_PROPERTY(bool keepHtml MEMBER m_keepHtml NOTIFY configChanged)
    Q_PROPERTY(bool keywordCategories MEMBER m_keywordCategories NOTIFY configChanged)
    Q_PROPERTY(bool autoSave MEMBER m_autoSave NOTIFY configChanged)
    Q_PROPERTY(LocalProductivityStore *store READ store WRITE setStore NOTIFY configChanged)
    Q_PROPERTY(LocalAttachmentStore *attachmentStore READ attachmentStore WRITE setAttachmentStore NOTIFY configChanged)
    Q_PROPERTY(bool hasSecret READ hasSecret NOTIFY secretChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
public:
    explicit ImapMailProvider(QObject *parent = nullptr);

    LocalProductivityStore *store() const { return m_store; }
    void setStore(LocalProductivityStore *store);
    LocalAttachmentStore *attachmentStore() const { return m_attachmentStore; }
    void setAttachmentStore(LocalAttachmentStore *store);
    bool hasSecret() const { return !m_secret.isEmpty(); }
    bool busy() const { return m_pending > 0; }
    QString state() const { return m_state; }
    QString lastError() const { return m_lastError; }

    // mechanism: "auto" (AUTHENTICATE PLAIN, else LOGIN), "login", "plain", "xoauth2".
    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("auto"));
    Q_INVOKABLE void clearSecret();
    // Pulls new messages, flag changes and remote expunges for `mailbox`.
    Q_INVOKABLE bool sync(const QString &requestId);
    // action: read, unread, flag, unflag, archive, delete.
    Q_INVOKABLE bool applyAction(const QString &requestId, const QString &messageId, const QString &action);
    // Downloads one attachment section and imports its bytes by attachment ID.
    Q_INVOKABLE bool fetchAttachment(const QString &requestId, const QString &messageId,
                                     const QString &attachmentId);
    // Lists every mailbox (INBOX first) with its special-use role; with
    // `withCounts` also STATUS (MESSAGES UNSEEN) for up to 200 of them.
    // Emits mailboxesFinished; needs no store.
    Q_INVOKABLE bool listMailboxes(const QString &requestId, bool withCounts = false);
    // Stores `rawMessage` (RFC 5322 bytes, e.g. SmtpMailProvider.composeMessage)
    // in `mailbox` with `flags` such as ["\\Seen"]; emits appendFinished
    // with appendUid when the server supports UIDPLUS. Needs no store.
    Q_INVOKABLE bool append(const QString &requestId, const QString &mailbox, const QByteArray &rawMessage,
                            const QStringList &flags = {});
    // Moves a message of `mailbox` to `targetMailbox` (UID MOVE, or COPY +
    // expunge) and removes the local record; emits actionFinished with
    // action "move".
    Q_INVOKABLE bool moveMessage(const QString &requestId, const QString &messageId, const QString &targetMailbox);

    // Flags are RFC 3501 atoms, including custom keywords. A server that
    // forbids keeping an added flag rejects the entire edit before STORE.
    Q_INVOKABLE bool updateFlags(const QString &requestId, const QString &messageId,
                                 const QStringList &add, const QStringList &remove = {});
    Q_INVOKABLE bool setCategories(const QString &requestId, const QString &messageId,
                                   const QStringList &categories);
    // Header bytes are returned in result.headers, keyed by the message UID.
    Q_INVOKABLE bool fetchHeaders(const QString &requestId, const QString &messageId,
                                  const QStringList &fields = {});
    Q_INVOKABLE bool copyMessage(const QString &requestId, const QString &messageId,
                                 const QString &targetMailbox);

signals:
    void configChanged();
    void secretChanged();
    void stateChanged();
    void syncFinished(const QString &requestId, const QVariantMap &result);
    void actionFinished(const QString &requestId, const QVariantMap &result);
    void attachmentFinished(const QString &requestId, const QVariantMap &result);
    void mailboxesFinished(const QString &requestId, const QVariantMap &result);
    void flagsFinished(const QString &requestId, const QVariantMap &result);
    void headersFinished(const QString &requestId, const QVariantMap &result);
    void appendFinished(const QString &requestId, const QVariantMap &result);

private:
    enum class Operation { Sync, Action, Attachment, Mailboxes, Append, Flags, Headers };
    bool start(Operation operation, const QString &requestId, const QVariantMap &input);
    QVariantMap prepareMessageJob(Operation operation, const QVariantMap &input, ImapJobs::Job *job) const;
    void complete(Operation operation, const QString &requestId, QVariantMap result);
    QVariantMap applySyncResult(QVariantMap result);
    QVariantMap applyActionResult(QVariantMap result);
    QVariantMap applyAttachmentResult(QVariantMap result);
    QVariantMap persist(QVariantMap result);
    QVariantMap findMessage(const QString &messageId) const;

    QString m_host, m_security = QStringLiteral("tls"), m_userName;
    QString m_accountId = QStringLiteral("imap"), m_mailbox = QStringLiteral("INBOX");
    QString m_archiveMailbox = QStringLiteral("Archive");
    int m_port = 993, m_timeoutMs = 15000, m_fetchLimit = 200, m_maximumMessageBytes = 8 * 1024 * 1024;
    bool m_allowInsecureAuth = false, m_autoSave = true;
    bool m_keepHtml = false, m_keywordCategories = false;
    QHash<QString, QByteArray> m_wireNames;
    QPointer<LocalProductivityStore> m_store;
    QPointer<LocalAttachmentStore> m_attachmentStore;
    QString m_secret, m_mechanism = QStringLiteral("auto");
    int m_pending = 0;
    QString m_state = QStringLiteral("idle"), m_lastError;
};

} // namespace QindaTK
