// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "local_attachment_store.h"
#include "local_productivity_store.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
namespace Pop3Jobs { struct Job; }

// POP3 download into LocalProductivityStore messages (D-298): the local store
// is the mailbox. sync() downloads messages whose UIDL is not stored yet;
// read/unread/flag/unflag/archive are local edits; delete removes the local
// record and DELEs the server copy; attachments are re-read from the server
// copy on demand. Each server exchange is one worker-thread POP3 session.
// AGENT-CONTRACT (MailView.qml, docs/productivity-providers.md): message
// records keep `id == messageId == "pop3:<accountId>:<percent-encoded UIDL>"`
// plus remoteAccount/remoteMailbox ("INBOX")/remoteUidl/remoteFetchedAt/
// remoteOnServer and localMailbox, and otherwise the ImapMailProvider record
// shape (mail_record.h); result.state is "accepted", "rejected", "offline" or
// "conflict" (a store stale-writer rejection) for MailView.acknowledge().
// AGENT-GUARD: a server copy is DELEd only after the store holding its complete
// local copy (body and attachment bytes) was saved; reordering loses mail.
// The secret lives only in memory (no READ accessor) and is redacted from
// every result, error and transcript.
class Pop3MailProvider : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString host MEMBER m_host NOTIFY configChanged)
    Q_PROPERTY(int port MEMBER m_port NOTIFY configChanged)
    Q_PROPERTY(QString security MEMBER m_security NOTIFY configChanged)
    Q_PROPERTY(QString userName MEMBER m_userName NOTIFY configChanged)
    Q_PROPERTY(QString accountId MEMBER m_accountId NOTIFY configChanged)
    Q_PROPERTY(int timeoutMs MEMBER m_timeoutMs NOTIFY configChanged)
    Q_PROPERTY(int fetchLimit MEMBER m_fetchLimit NOTIFY configChanged)
    Q_PROPERTY(int maximumMessageBytes MEMBER m_maximumMessageBytes NOTIFY configChanged)
    Q_PROPERTY(bool allowInsecureAuth MEMBER m_allowInsecureAuth NOTIFY configChanged)
    Q_PROPERTY(bool autoSave MEMBER m_autoSave NOTIFY configChanged)
    Q_PROPERTY(bool leaveOnServer MEMBER m_leaveOnServer NOTIFY configChanged)
    Q_PROPERTY(int deleteAfterDays MEMBER m_deleteAfterDays NOTIFY configChanged)
    Q_PROPERTY(LocalProductivityStore *store READ store WRITE setStore NOTIFY configChanged)
    Q_PROPERTY(LocalAttachmentStore *attachmentStore READ attachmentStore WRITE setAttachmentStore NOTIFY configChanged)
    Q_PROPERTY(bool hasSecret READ hasSecret NOTIFY secretChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
public:
    explicit Pop3MailProvider(QObject *parent = nullptr);

    LocalProductivityStore *store() const { return m_store; }
    void setStore(LocalProductivityStore *store);
    LocalAttachmentStore *attachmentStore() const { return m_attachmentStore; }
    void setAttachmentStore(LocalAttachmentStore *store);
    bool hasSecret() const { return !m_secret.isEmpty(); }
    bool busy() const { return m_pending > 0; }
    QString state() const { return m_state; }
    QString lastError() const { return m_lastError; }

    // mechanism: "auto" (SASL PLAIN when offered, else USER/PASS), "user"
    // (USER/PASS; "login" is accepted as an alias), "plain", "xoauth2".
    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("auto"));
    Q_INVOKABLE void clearSecret();
    // Downloads new messages; with autoSave, then DELEs saved server copies
    // that leaveOnServer/deleteAfterDays release.
    Q_INVOKABLE bool sync(const QString &requestId);
    // action: read, unread, flag, unflag, archive (local), delete (local + DELE).
    Q_INVOKABLE bool applyAction(const QString &requestId, const QString &messageId, const QString &action);
    // Re-reads the server copy and imports one attachment's bytes by its ID.
    Q_INVOKABLE bool fetchAttachment(const QString &requestId, const QString &messageId,
                                     const QString &attachmentId);

signals:
    void configChanged();
    void secretChanged();
    void stateChanged();
    void syncFinished(const QString &requestId, const QVariantMap &result);
    void actionFinished(const QString &requestId, const QVariantMap &result);
    void attachmentFinished(const QString &requestId, const QVariantMap &result);

private:
    enum class Operation { Sync, Action, Attachment };
    bool start(Operation operation, const QString &requestId, const QVariantMap &input);
    void queue(Operation operation, const QString &requestId, const QVariantMap &result);
    void complete(Operation operation, const QString &requestId, QVariantMap result);
    void finish(Operation operation, const QString &requestId, QVariantMap result);
    void startCleanup(const QString &requestId, const QVariantMap &syncResult, const QStringList &uidls);
    QVariantMap applySyncResult(QVariantMap result, QStringList *deletable);
    QVariantMap applyCleanupResult(QVariantMap syncResult, const QVariantMap &cleanup);
    QVariantMap applyActionResult(QVariantMap result);
    QVariantMap applyAttachmentResult(QVariantMap result);
    bool importAttachments(QVariantMap *row, const QVariantMap &bytes);
    void setOnServer(const QString &uidl, bool onServer, int *count);
    QVariantMap persist(QVariantMap result);
    QVariantMap findMessage(const QString &messageId) const;
    void configure(Pop3Jobs::Job *job) const;

    QString m_host, m_security = QStringLiteral("tls"), m_userName;
    QString m_accountId = QStringLiteral("pop3");
    int m_port = 995, m_timeoutMs = 15000, m_fetchLimit = 200, m_maximumMessageBytes = 8 * 1024 * 1024;
    int m_deleteAfterDays = 0;
    bool m_allowInsecureAuth = false, m_autoSave = true, m_leaveOnServer = true;
    QPointer<LocalProductivityStore> m_store;
    QPointer<LocalAttachmentStore> m_attachmentStore;
    QString m_secret, m_mechanism = QStringLiteral("auto");
    int m_pending = 0;
    QString m_state = QStringLiteral("idle"), m_lastError;
};

} // namespace QindaTK
