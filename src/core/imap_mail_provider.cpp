// SPDX-License-Identifier: LGPL-3.0-or-later
#include "imap_mail_provider.h"

#include "imap_folders.h"
#include "imap_mail_jobs.h"
#include "protocol_client.h"

namespace QindaTK {
namespace {
QVariantMap rejection(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("state"), QStringLiteral("rejected")},
            {QStringLiteral("error"), error}, {QStringLiteral("retryable"), false}};
}

const QStringList &remoteActions()
{
    static const QStringList actions{QStringLiteral("read"), QStringLiteral("unread"), QStringLiteral("flag"),
                                     QStringLiteral("unflag"), QStringLiteral("archive"), QStringLiteral("delete")};
    return actions;
}
} // namespace

ImapMailProvider::ImapMailProvider(QObject *parent) : QObject(parent) {}

void ImapMailProvider::setStore(LocalProductivityStore *store)
{
    if (m_store == store) return;
    m_store = store;
    emit configChanged();
}

void ImapMailProvider::setAttachmentStore(LocalAttachmentStore *store)
{
    if (m_attachmentStore == store) return;
    m_attachmentStore = store;
    emit configChanged();
}

void ImapMailProvider::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret = secret;
    m_mechanism = mechanism;
    emit secretChanged();
}

void ImapMailProvider::clearSecret()
{
    m_secret.fill(QLatin1Char('\0'));
    m_secret.clear();
    emit secretChanged();
}

QVariantMap ImapMailProvider::findMessage(const QString &messageId) const
{
    if (!m_store) return {};
    for (const QVariant &value : m_store->messages()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("id")).toString() == messageId
            || row.value(QStringLiteral("messageId")).toString() == messageId)
            return row;
    }
    return {};
}

bool ImapMailProvider::sync(const QString &requestId)
{
    return start(Operation::Sync, requestId, {});
}

bool ImapMailProvider::applyAction(const QString &requestId, const QString &messageId, const QString &action)
{
    return start(Operation::Action, requestId,
                 {{QStringLiteral("messageId"), messageId}, {QStringLiteral("action"), action}});
}

bool ImapMailProvider::fetchAttachment(const QString &requestId, const QString &messageId,
                                       const QString &attachmentId)
{
    return start(Operation::Attachment, requestId,
                 {{QStringLiteral("messageId"), messageId}, {QStringLiteral("attachmentId"), attachmentId}});
}

bool ImapMailProvider::listMailboxes(const QString &requestId, bool withCounts)
{
    return start(Operation::Mailboxes, requestId, {{QStringLiteral("withCounts"), withCounts}});
}

bool ImapMailProvider::append(const QString &requestId, const QString &mailbox, const QByteArray &rawMessage,
                              const QStringList &flags)
{
    return start(Operation::Append, requestId,
                 {{QStringLiteral("mailbox"), mailbox}, {QStringLiteral("rawMessage"), rawMessage},
                  {QStringLiteral("flags"), flags}});
}

bool ImapMailProvider::moveMessage(const QString &requestId, const QString &messageId, const QString &targetMailbox)
{
    return start(Operation::Action, requestId,
                 {{QStringLiteral("messageId"), messageId}, {QStringLiteral("action"), QStringLiteral("move")},
                  {QStringLiteral("targetMailbox"), targetMailbox}});
}

// Validates a message-scoped request (action, attachment) against the stored
// record and fills the job's target; returns a rejection map on failure.
QVariantMap ImapMailProvider::prepareMessageJob(Operation operation, const QVariantMap &input, ImapJobs::Job *job) const
{
    const QVariantMap message = findMessage(input.value(QStringLiteral("messageId")).toString());
    job->uid = message.value(QStringLiteral("remoteUid")).toLongLong();
    job->uidValidity = message.value(QStringLiteral("remoteUidValidity")).toLongLong();
    if (message.isEmpty() || job->uid <= 0 || job->uidValidity <= 0
        || message.value(QStringLiteral("remoteAccount")).toString() != m_accountId
        || message.value(QStringLiteral("remoteMailbox")).toString() != m_mailbox)
        return rejection(QStringLiteral("The message is not stored from this IMAP mailbox"));
    job->action = input.value(QStringLiteral("action")).toString();
    if (operation == Operation::Action) {
        if (job->action == QLatin1String("move")) {
            job->targetMailbox = input.value(QStringLiteral("targetMailbox")).toString();
            if (job->targetMailbox.trimmed().isEmpty() || job->targetMailbox == m_mailbox)
                return rejection(QStringLiteral("Choose a different mailbox to move the message to"));
        } else if (!remoteActions().contains(job->action)) {
            return rejection(QStringLiteral("\"%1\" is a host action (for example reply opens a draft)").arg(job->action));
        } else if (job->action == QLatin1String("archive")) {
            job->targetMailbox = m_archiveMailbox;
            if (m_archiveMailbox.trimmed().isEmpty() || m_archiveMailbox == m_mailbox)
                return rejection(QStringLiteral("No archive mailbox different from the current one is configured"));
        }
        return {};
    }
    const QString attachmentId = input.value(QStringLiteral("attachmentId")).toString();
    for (const QVariant &value : message.value(QStringLiteral("attachments")).toList()) {
        const QVariantMap attachment = value.toMap();
        if (attachment.value(QStringLiteral("id")).toString() != attachmentId) continue;
        job->section = attachment.value(QStringLiteral("section")).toString();
        job->encoding = attachment.value(QStringLiteral("encoding")).toString();
    }
    if (job->section.isEmpty()) return rejection(QStringLiteral("Attachment %1 is not part of the message").arg(attachmentId));
    if (!m_attachmentStore) return rejection(QStringLiteral("No attachment store is configured"));
    return {};
}

bool ImapMailProvider::start(Operation operation, const QString &requestId, const QVariantMap &input)
{
    ImapJobs::Job job;
    job.endpoint = {m_host, static_cast<quint16>(m_port), m_security, m_timeoutMs, m_allowInsecureAuth};
    job.credentials = {m_userName, m_secret, m_mechanism};
    job.accountId = m_accountId;
    job.mailbox = m_mailbox;
    job.archiveMailbox = m_archiveMailbox;
    job.fetchLimit = m_fetchLimit;
    job.maximumMessageBytes = m_maximumMessageBytes;

    QVariantMap early;
    if (operation == Operation::Mailboxes) {
        job.withCounts = input.value(QStringLiteral("withCounts")).toBool();
    } else if (operation == Operation::Append) {
        job.targetMailbox = input.value(QStringLiteral("mailbox")).toString();
        job.appendMessage = input.value(QStringLiteral("rawMessage")).toByteArray();
        job.appendFlags = input.value(QStringLiteral("flags")).toStringList();
        if (job.targetMailbox.trimmed().isEmpty())
            early = rejection(QStringLiteral("No mailbox is given for the message copy"));
        else if (job.appendMessage.isEmpty() || job.appendMessage.size() > ImapFolders::MaximumAppendBytes)
            early = rejection(QStringLiteral("The message must be between 1 byte and 64 MiB"));
        else if (job.appendMessage.contains('\0'))
            early = rejection(QStringLiteral("The message contains NUL bytes"));
        for (const QString &flag : std::as_const(job.appendFlags))
            if (early.isEmpty() && !ImapFolders::validFlag(flag))
                early = rejection(QStringLiteral("\"%1\" is not a valid IMAP flag").arg(flag));
    } else if (!m_store) {
        early = rejection(QStringLiteral("No local store is configured for mail sync"));
    } else if (operation == Operation::Sync) {
        for (const QVariant &value : m_store->messages()) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("remoteAccount")).toString() != m_accountId
                || row.value(QStringLiteral("remoteMailbox")).toString() != m_mailbox)
                continue;
            job.knownUidValidity = row.value(QStringLiteral("remoteUidValidity")).toLongLong();
            job.known.insert(row.value(QStringLiteral("remoteUid")).toLongLong(),
                             {row.value(QStringLiteral("unread")).toBool(), row.value(QStringLiteral("flagged")).toBool()});
        }
    } else {
        early = prepareMessageJob(operation, input, &job);
    }
    // The message bytes are not echoed back in the result.
    QVariantMap echoed = input;
    echoed.remove(QStringLiteral("rawMessage"));

    ++m_pending;
    m_state = QStringLiteral("working");
    emit stateChanged();
    if (!early.isEmpty()) {
        early.insert(echoed);
        QMetaObject::invokeMethod(this, [this, operation, requestId, early] { complete(operation, requestId, early); },
                                  Qt::QueuedConnection);
        return false;
    }
    ImapJobs::Kind kind = ImapJobs::Kind::Sync;
    switch (operation) {
    case Operation::Sync: kind = ImapJobs::Kind::Sync; break;
    case Operation::Action: kind = ImapJobs::Kind::Action; break;
    case Operation::Attachment: kind = ImapJobs::Kind::Attachment; break;
    case Operation::Mailboxes: kind = ImapJobs::Kind::Mailboxes; break;
    case Operation::Append: kind = ImapJobs::Kind::Append; break;
    }
    Protocol::startWorker(this, [kind, job] { return ImapJobs::run(kind, job); },
                          [this, operation, requestId, echoed](const QVariantMap &result) {
                              QVariantMap merged = result;
                              merged.insert(echoed);
                              complete(operation, requestId, merged);
                          });
    return true;
}

QVariantMap ImapMailProvider::persist(QVariantMap result)
{
    if (!m_autoSave || !m_store || m_store->save()) return result;
    // AGENT-NOTE: a stale-writer rejection keeps the remote change unapplied
    // on disk; the host reloads the store and syncs again (sync is idempotent).
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("state"), m_store->status() == QLatin1String("conflict")
                                               ? QStringLiteral("conflict") : QStringLiteral("rejected"));
    result.insert(QStringLiteral("error"), m_store->lastError());
    return result;
}

QVariantMap ImapMailProvider::applySyncResult(QVariantMap result)
{
    const qint64 validity = result.value(QStringLiteral("uidValidity")).toLongLong();
    int removedCount = 0, updatedCount = 0;
    if (result.value(QStringLiteral("reset")).toBool()) {
        QStringList stale;
        for (const QVariant &value : m_store->messages()) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("remoteAccount")).toString() == m_accountId
                && row.value(QStringLiteral("remoteMailbox")).toString() == m_mailbox)
                stale.append(row.value(QStringLiteral("id")).toString());
        }
        for (const QString &id : std::as_const(stale))
            removedCount += m_store->remove(QStringLiteral("messages"), id).value(QStringLiteral("ok")).toBool();
    }
    for (const QVariant &uid : result.value(QStringLiteral("removedUids")).toList()) {
        const QString id = ImapJobs::recordId(m_accountId, m_mailbox, validity, uid.toLongLong());
        removedCount += m_store->remove(QStringLiteral("messages"), id).value(QStringLiteral("ok")).toBool();
    }
    for (const QVariant &value : result.value(QStringLiteral("updated")).toList()) {
        const QVariantMap change = value.toMap();
        QVariantMap row = findMessage(ImapJobs::recordId(m_accountId, m_mailbox, validity,
                                                         change.value(QStringLiteral("uid")).toLongLong()));
        if (row.isEmpty()) continue;
        row.insert(QStringLiteral("unread"), change.value(QStringLiteral("unread")));
        row.insert(QStringLiteral("flagged"), change.value(QStringLiteral("flagged")));
        updatedCount += m_store->upsert(QStringLiteral("messages"), row).value(QStringLiteral("ok")).toBool();
    }
    QStringList addedIds;
    for (const QVariant &value : result.value(QStringLiteral("added")).toList()) {
        const QVariantMap row = value.toMap();
        if (m_store->upsert(QStringLiteral("messages"), row).value(QStringLiteral("ok")).toBool())
            addedIds.append(row.value(QStringLiteral("id")).toString());
    }
    result.remove(QStringLiteral("added"));
    result.remove(QStringLiteral("updated"));
    result.insert(QStringLiteral("addedIds"), addedIds);
    result.insert(QStringLiteral("addedCount"), addedIds.size());
    result.insert(QStringLiteral("updatedCount"), updatedCount);
    result.insert(QStringLiteral("removedCount"), removedCount);
    return persist(result);
}

QVariantMap ImapMailProvider::applyActionResult(QVariantMap result)
{
    const QString messageId = result.value(QStringLiteral("messageId")).toString();
    const QString action = result.value(QStringLiteral("action")).toString();
    QVariantMap row = findMessage(messageId);
    if (row.isEmpty()) return persist(result);        // removed locally meanwhile; remote change stands
    if (action == QLatin1String("archive") || action == QLatin1String("move") || action == QLatin1String("delete")) {
        m_store->remove(QStringLiteral("messages"), row.value(QStringLiteral("id")).toString());
    } else {
        if (action == QLatin1String("read") || action == QLatin1String("unread"))
            row.insert(QStringLiteral("unread"), action == QLatin1String("unread"));
        else
            row.insert(QStringLiteral("flagged"), action == QLatin1String("flag"));
        m_store->upsert(QStringLiteral("messages"), row);
    }
    return persist(result);
}

QVariantMap ImapMailProvider::applyAttachmentResult(QVariantMap result)
{
    const QString attachmentId = result.value(QStringLiteral("attachmentId")).toString();
    QVariantMap row = findMessage(result.value(QStringLiteral("messageId")).toString());
    QVariantList attachments = row.value(QStringLiteral("attachments")).toList();
    for (qsizetype i = 0; i < attachments.size(); ++i) {
        QVariantMap attachment = attachments.at(i).toMap();
        if (attachment.value(QStringLiteral("id")).toString() != attachmentId) continue;
        if (!m_attachmentStore) return rejection(QStringLiteral("No attachment store is configured"));
        const QVariantMap imported = m_attachmentStore->importBytes(
            attachmentId, attachment.value(QStringLiteral("name")).toString(),
            attachment.value(QStringLiteral("mimeType")).toString(), result.value(QStringLiteral("bytes")).toByteArray());
        if (!imported.value(QStringLiteral("ok")).toBool()) {
            result.insert(QStringLiteral("ok"), false);
            result.insert(QStringLiteral("state"), QStringLiteral("rejected"));
            result.insert(QStringLiteral("error"), imported.value(QStringLiteral("error")));
            return result;
        }
        attachment.insert(QStringLiteral("state"), QStringLiteral("local"));
        attachment.insert(QStringLiteral("size"), imported.value(QStringLiteral("size")));
        attachments.replace(i, attachment);
        row.insert(QStringLiteral("attachments"), attachments);
        m_store->upsert(QStringLiteral("messages"), row);
        result.insert(QStringLiteral("size"), imported.value(QStringLiteral("size")));
        return persist(result);
    }
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("state"), QStringLiteral("rejected"));
    result.insert(QStringLiteral("error"), QStringLiteral("The message was removed before the attachment arrived"));
    return result;
}

void ImapMailProvider::complete(Operation operation, const QString &requestId, QVariantMap result)
{
    if (result.value(QStringLiteral("ok")).toBool() && m_store) {
        if (operation == Operation::Sync) result = applySyncResult(result);
        else if (operation == Operation::Action) result = applyActionResult(result);
        else if (operation == Operation::Attachment) result = applyAttachmentResult(result);
    }
    result.remove(QStringLiteral("bytes"));
    result.insert(QStringLiteral("requestId"), requestId);
    m_lastError = Protocol::redact(result.value(QStringLiteral("error")).toString(), {m_secret});
    result.insert(QStringLiteral("error"), m_lastError);
    m_pending = qMax(0, m_pending - 1);
    m_state = result.value(QStringLiteral("state")).toString();
    emit stateChanged();
    switch (operation) {
    case Operation::Sync: emit syncFinished(requestId, result); break;
    case Operation::Action: emit actionFinished(requestId, result); break;
    case Operation::Attachment: emit attachmentFinished(requestId, result); break;
    case Operation::Mailboxes: emit mailboxesFinished(requestId, result); break;
    case Operation::Append: emit appendFinished(requestId, result); break;
    }
}

} // namespace QindaTK
