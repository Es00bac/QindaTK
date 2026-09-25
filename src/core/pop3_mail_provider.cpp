// SPDX-License-Identifier: LGPL-3.0-or-later
#include "pop3_mail_provider.h"

#include "pop3_mail_jobs.h"
#include "protocol_client.h"

#include <QDateTime>

namespace QindaTK {
namespace {
QVariantMap rejection(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("state"), QStringLiteral("rejected")},
            {QStringLiteral("error"), error}, {QStringLiteral("retryable"), false}};
}

// A local store edit that needs no server exchange.
QVariantMap localAcceptance()
{
    return {{QStringLiteral("ok"), true}, {QStringLiteral("state"), QStringLiteral("accepted")},
            {QStringLiteral("error"), QString()}, {QStringLiteral("retryable"), false},
            {QStringLiteral("local"), true}, {QStringLiteral("transcript"), QStringList()}};
}

const QStringList &mailActions()
{
    static const QStringList actions{QStringLiteral("read"), QStringLiteral("unread"), QStringLiteral("flag"),
                                     QStringLiteral("unflag"), QStringLiteral("archive"), QStringLiteral("delete")};
    return actions;
}

bool allAttachmentsLocal(const QVariantMap &row)
{
    for (const QVariant &value : row.value(QStringLiteral("attachments")).toList())
        if (value.toMap().value(QStringLiteral("state")).toString() != QLatin1String("local")) return false;
    return true;
}

bool onServer(const QVariantMap &row)
{
    return row.value(QStringLiteral("remoteOnServer"), true).toBool();
}
} // namespace

Pop3MailProvider::Pop3MailProvider(QObject *parent) : QObject(parent) {}

void Pop3MailProvider::setStore(LocalProductivityStore *store)
{
    if (m_store == store) return;
    m_store = store;
    emit configChanged();
}

void Pop3MailProvider::setAttachmentStore(LocalAttachmentStore *store)
{
    if (m_attachmentStore == store) return;
    m_attachmentStore = store;
    emit configChanged();
}

void Pop3MailProvider::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret = secret;
    m_mechanism = mechanism;
    emit secretChanged();
}

void Pop3MailProvider::clearSecret()
{
    m_secret.fill(QLatin1Char('\0'));
    m_secret.clear();
    emit secretChanged();
}

QVariantMap Pop3MailProvider::findMessage(const QString &messageId) const
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

bool Pop3MailProvider::sync(const QString &requestId)
{
    return start(Operation::Sync, requestId, {});
}

bool Pop3MailProvider::applyAction(const QString &requestId, const QString &messageId, const QString &action)
{
    return start(Operation::Action, requestId,
                 {{QStringLiteral("messageId"), messageId}, {QStringLiteral("action"), action}});
}

bool Pop3MailProvider::fetchAttachment(const QString &requestId, const QString &messageId,
                                       const QString &attachmentId)
{
    return start(Operation::Attachment, requestId,
                 {{QStringLiteral("messageId"), messageId}, {QStringLiteral("attachmentId"), attachmentId}});
}

void Pop3MailProvider::configure(Pop3Jobs::Job *job) const
{
    job->endpoint = {m_host, static_cast<quint16>(m_port), m_security, m_timeoutMs, m_allowInsecureAuth};
    job->credentials = {m_userName, m_secret, m_mechanism};
    job->accountId = m_accountId;
    job->fetchLimit = m_fetchLimit;
    job->maximumMessageBytes = m_maximumMessageBytes;
    job->leaveOnServer = m_leaveOnServer;
    job->deleteAfterDays = qMax(0, m_deleteAfterDays);
    job->canImportAttachments = m_attachmentStore != nullptr;
    job->now = QDateTime::currentDateTimeUtc();
}

bool Pop3MailProvider::start(Operation operation, const QString &requestId, const QVariantMap &input)
{
    Pop3Jobs::Job job;
    configure(&job);
    Pop3Jobs::Kind kind = Pop3Jobs::Kind::Sync;
    QVariantMap early;
    if (!m_store) {
        early = rejection(QStringLiteral("No local store is configured for mail download"));
    } else if (operation == Operation::Sync) {
        for (const QVariant &value : m_store->messages()) {
            const QVariantMap row = value.toMap();
            const QString uidl = row.value(QStringLiteral("remoteUidl")).toString();
            if (uidl.isEmpty() || row.value(QStringLiteral("remoteAccount")).toString() != m_accountId) continue;
            Pop3Jobs::KnownMessage known;
            known.onServer = onServer(row);
            known.truncated = row.value(QStringLiteral("bodyTruncated")).toBool();
            known.remoteAttachments = !allAttachmentsLocal(row);
            known.fetchedAt = QDateTime::fromString(row.value(QStringLiteral("remoteFetchedAt")).toString(), Qt::ISODate);
            job.known.insert(uidl, known);
        }
    } else {
        const QVariantMap message = findMessage(input.value(QStringLiteral("messageId")).toString());
        job.uidl = message.value(QStringLiteral("remoteUidl")).toString();
        if (message.isEmpty() || job.uidl.isEmpty()
            || message.value(QStringLiteral("remoteAccount")).toString() != m_accountId)
            early = rejection(QStringLiteral("The message is not stored from this POP3 account"));
        const QString action = input.value(QStringLiteral("action")).toString();
        if (early.isEmpty() && operation == Operation::Action) {
            // AGENT-NOTE: POP3 has no server flags or folders (RFC 1939), so
            // everything but delete is a local store edit; delete of a copy
            // that already left the server is local too.
            if (!mailActions().contains(action))
                early = rejection(QStringLiteral("\"%1\" is a host action (for example reply opens a draft)").arg(action));
            else if (action != QLatin1String("delete") || !onServer(message))
                early = localAcceptance();
            else
                kind = Pop3Jobs::Kind::Remove;
        }
        if (early.isEmpty() && operation == Operation::Attachment) {
            const QString attachmentId = input.value(QStringLiteral("attachmentId")).toString();
            for (const QVariant &value : message.value(QStringLiteral("attachments")).toList()) {
                const QVariantMap attachment = value.toMap();
                if (attachment.value(QStringLiteral("id")).toString() == attachmentId)
                    job.section = attachment.value(QStringLiteral("section")).toString();
            }
            kind = Pop3Jobs::Kind::Attachment;
            if (job.section.isEmpty())
                early = rejection(QStringLiteral("Attachment %1 is not part of the message").arg(attachmentId));
            else if (!m_attachmentStore)
                early = rejection(QStringLiteral("No attachment store is configured"));
            else if (!onServer(message))
                early = rejection(QStringLiteral("The message is no longer on the POP3 server; "
                                                 "the attachment cannot be downloaded"));
        }
    }

    ++m_pending;
    m_state = QStringLiteral("working");
    emit stateChanged();
    if (!early.isEmpty()) {
        early.insert(input);
        queue(operation, requestId, early);
        return early.value(QStringLiteral("ok")).toBool();
    }
    Protocol::startWorker(this, [kind, job] { return Pop3Jobs::run(kind, job); },
                          [this, operation, requestId, input](const QVariantMap &result) {
                              QVariantMap merged = result;
                              merged.insert(input);
                              complete(operation, requestId, merged);
                          });
    return true;
}

void Pop3MailProvider::queue(Operation operation, const QString &requestId, const QVariantMap &result)
{
    QMetaObject::invokeMethod(this, [this, operation, requestId, result] { complete(operation, requestId, result); },
                              Qt::QueuedConnection);
}

QVariantMap Pop3MailProvider::persist(QVariantMap result)
{
    if (!m_autoSave || !m_store || m_store->save()) return result;
    // AGENT-NOTE: a stale-writer rejection keeps the download unapplied on
    // disk and blocks every server deletion; the host reloads the store and
    // syncs again (sync is idempotent by UIDL).
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("state"), m_store->status() == QLatin1String("conflict")
                                               ? QStringLiteral("conflict") : QStringLiteral("rejected"));
    result.insert(QStringLiteral("error"), m_store->lastError());
    return result;
}

void Pop3MailProvider::setOnServer(const QString &uidl, bool present, int *count)
{
    QVariantMap row = findMessage(Pop3Jobs::recordId(m_accountId, uidl));
    if (row.isEmpty() || onServer(row) == present) return;
    row.insert(QStringLiteral("remoteOnServer"), present);
    if (m_store->upsert(QStringLiteral("messages"), row).value(QStringLiteral("ok")).toBool()) ++*count;
}

bool Pop3MailProvider::importAttachments(QVariantMap *row, const QVariantMap &bytes)
{
    if (!m_attachmentStore) return false;
    QVariantList attachments = row->value(QStringLiteral("attachments")).toList();
    bool changed = false;
    for (qsizetype i = 0; i < attachments.size(); ++i) {
        QVariantMap attachment = attachments.at(i).toMap();
        const QString id = attachment.value(QStringLiteral("id")).toString();
        if (attachment.value(QStringLiteral("state")).toString() == QLatin1String("local") || !bytes.contains(id))
            continue;
        const QVariantMap imported = m_attachmentStore->importBytes(
            id, attachment.value(QStringLiteral("name")).toString(),
            attachment.value(QStringLiteral("mimeType")).toString(), bytes.value(id).toByteArray());
        if (!imported.value(QStringLiteral("ok")).toBool()) continue;   // stays remote: the server copy is kept
        attachment.insert(QStringLiteral("state"), QStringLiteral("local"));
        attachment.insert(QStringLiteral("size"), imported.value(QStringLiteral("size")));
        attachments.replace(i, attachment);
        changed = true;
    }
    if (changed) row->insert(QStringLiteral("attachments"), attachments);
    return changed;
}

QVariantMap Pop3MailProvider::applySyncResult(QVariantMap result, QStringList *deletable)
{
    QStringList addedIds;
    for (const QVariant &value : result.value(QStringLiteral("added")).toList()) {
        const QVariantMap row = value.toMap();
        if (m_store->upsert(QStringLiteral("messages"), row).value(QStringLiteral("ok")).toBool())
            addedIds.append(row.value(QStringLiteral("id")).toString());
    }
    int updatedCount = 0;
    for (const QVariant &uidl : result.value(QStringLiteral("goneUidls")).toList())
        setOnServer(uidl.toString(), false, &updatedCount);
    const QVariantMap data = result.value(QStringLiteral("attachmentData")).toMap();
    for (auto it = data.cbegin(); it != data.cend(); ++it) {
        QVariantMap row = findMessage(it.key());
        if (!row.isEmpty() && importAttachments(&row, it.value().toMap()))
            m_store->upsert(QStringLiteral("messages"), row);
    }
    // A server copy may go only when the local copy is complete: the body
    // was downloaded and every attachment's bytes are in the attachment store.
    int keptCount = 0;
    for (const QVariant &uidl : result.value(QStringLiteral("deleteCandidates")).toList()) {
        const QVariantMap row = findMessage(Pop3Jobs::recordId(m_accountId, uidl.toString()));
        if (!row.isEmpty() && !row.value(QStringLiteral("bodyTruncated")).toBool() && allAttachmentsLocal(row))
            deletable->append(uidl.toString());
        else
            ++keptCount;
    }
    result.insert(QStringLiteral("addedIds"), addedIds);
    result.insert(QStringLiteral("addedCount"), addedIds.size());
    result.insert(QStringLiteral("updatedCount"), updatedCount);
    result.insert(QStringLiteral("removedCount"), 0);         // POP3 never removes local mail on sync
    result.insert(QStringLiteral("keptOnServerCount"), keptCount);
    result.insert(QStringLiteral("deletedCount"), 0);
    result = persist(result);
    // AGENT-GUARD: deletion needs this provider's own successful save of the
    // records above. Without autoSave nothing proves the copy is on disk, so
    // the server keeps every message.
    if (!result.value(QStringLiteral("ok")).toBool() || !m_autoSave) deletable->clear();
    return result;
}

void Pop3MailProvider::startCleanup(const QString &requestId, const QVariantMap &syncResult, const QStringList &uidls)
{
    Pop3Jobs::Job job;
    configure(&job);
    job.remove = uidls;
    Protocol::startWorker(this, [job] { return Pop3Jobs::run(Pop3Jobs::Kind::Cleanup, job); },
                          [this, requestId, syncResult](const QVariantMap &cleanup) {
                              finish(Operation::Sync, requestId, applyCleanupResult(syncResult, cleanup));
                          });
}

QVariantMap Pop3MailProvider::applyCleanupResult(QVariantMap syncResult, const QVariantMap &cleanup)
{
    syncResult.insert(QStringLiteral("transcript"),
                      syncResult.value(QStringLiteral("transcript")).toStringList()
                          + cleanup.value(QStringLiteral("transcript")).toStringList());
    syncResult.insert(QStringLiteral("deleteState"), cleanup.value(QStringLiteral("state")));
    if (!cleanup.value(QStringLiteral("ok")).toBool()) {
        // The download stands; the server copies stay and the next sync
        // retries their deletion.
        syncResult.insert(QStringLiteral("deleteError"), cleanup.value(QStringLiteral("error")));
        return syncResult;
    }
    if (!m_store) return syncResult;
    int deleted = 0;
    for (const QVariant &uidl : cleanup.value(QStringLiteral("deletedUidls")).toList())
        setOnServer(uidl.toString(), false, &deleted);
    syncResult.insert(QStringLiteral("deletedCount"), deleted);
    return persist(syncResult);
}

QVariantMap Pop3MailProvider::applyActionResult(QVariantMap result)
{
    const QString action = result.value(QStringLiteral("action")).toString();
    QVariantMap row = findMessage(result.value(QStringLiteral("messageId")).toString());
    if (row.isEmpty()) return persist(result);        // removed locally meanwhile
    if (action == QLatin1String("delete")) {
        m_store->remove(QStringLiteral("messages"), row.value(QStringLiteral("id")).toString());
        return persist(result);
    }
    if (action == QLatin1String("read") || action == QLatin1String("unread"))
        row.insert(QStringLiteral("unread"), action == QLatin1String("unread"));
    else if (action == QLatin1String("flag") || action == QLatin1String("unflag"))
        row.insert(QStringLiteral("flagged"), action == QLatin1String("flag"));
    else
        row.insert(QStringLiteral("localMailbox"), QStringLiteral("Archive"));
    m_store->upsert(QStringLiteral("messages"), row);
    return persist(result);
}

QVariantMap Pop3MailProvider::applyAttachmentResult(QVariantMap result)
{
    const QString attachmentId = result.value(QStringLiteral("attachmentId")).toString();
    QVariantMap row = findMessage(result.value(QStringLiteral("messageId")).toString());
    const QVariantMap bytes{{attachmentId, result.value(QStringLiteral("bytes"))}};
    if (row.isEmpty() || !importAttachments(&row, bytes)) {
        bool local = false;
        for (const QVariant &value : row.value(QStringLiteral("attachments")).toList())
            local = local || (value.toMap().value(QStringLiteral("id")).toString() == attachmentId
                              && value.toMap().value(QStringLiteral("state")).toString() == QLatin1String("local"));
        if (local) return result;                     // already local (a sync imported it meanwhile)
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("state"), QStringLiteral("rejected"));
        result.insert(QStringLiteral("error"), row.isEmpty()
                          ? QStringLiteral("The message was removed before the attachment arrived")
                          : QStringLiteral("The attachment store refused the attachment"));
        return result;
    }
    m_store->upsert(QStringLiteral("messages"), row);
    for (const QVariant &value : row.value(QStringLiteral("attachments")).toList())
        if (value.toMap().value(QStringLiteral("id")).toString() == attachmentId)
            result.insert(QStringLiteral("size"), value.toMap().value(QStringLiteral("size")));
    return persist(result);
}

void Pop3MailProvider::complete(Operation operation, const QString &requestId, QVariantMap result)
{
    if (result.value(QStringLiteral("ok")).toBool() && m_store) {
        if (operation == Operation::Sync) {
            QStringList deletable;
            result = applySyncResult(result, &deletable);
            if (!deletable.isEmpty()) {
                startCleanup(requestId, result, deletable);
                return;
            }
        } else if (operation == Operation::Action) {
            result = applyActionResult(result);
        } else {
            result = applyAttachmentResult(result);
        }
    }
    finish(operation, requestId, result);
}

void Pop3MailProvider::finish(Operation operation, const QString &requestId, QVariantMap result)
{
    // Worker-internal payloads never reach QML (bytes, sources, UIDL lists).
    for (const char *key : {"bytes", "added", "attachmentData", "deleteCandidates", "goneUidls", "deletedUidls"})
        result.remove(QString::fromLatin1(key));
    result.insert(QStringLiteral("requestId"), requestId);
    m_lastError = Protocol::redact(result.value(QStringLiteral("error")).toString(), {m_secret});
    result.insert(QStringLiteral("error"), m_lastError);
    if (result.contains(QStringLiteral("deleteError")))
        result.insert(QStringLiteral("deleteError"),
                      Protocol::redact(result.value(QStringLiteral("deleteError")).toString(), {m_secret}));
    m_pending = qMax(0, m_pending - 1);
    m_state = result.value(QStringLiteral("state")).toString();
    emit stateChanged();
    if (operation == Operation::Sync) emit syncFinished(requestId, result);
    else if (operation == Operation::Action) emit actionFinished(requestId, result);
    else emit attachmentFinished(requestId, result);
}

} // namespace QindaTK
