// SPDX-License-Identifier: LGPL-3.0-or-later
#include "caldav_provider.h"

#include "caldav_jobs.h"
#include "ical_codec.h"

#include <QUrl>

namespace QindaTK {
namespace {
QString componentFor(const QString &collection)
{
    return collection == QLatin1String("tasks") ? QStringLiteral("VTODO") : QStringLiteral("VEVENT");
}

QString conflictKey(const QString &collection, const QString &id) { return collection + QLatin1Char('/') + id; }

QVariantMap rejection(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("state"), QStringLiteral("rejected")},
            {QStringLiteral("error"), error}, {QStringLiteral("retryable"), false}};
}

CalDavJobs::LocalItem localItem(const QString &collection, const QVariantMap &row)
{
    CalDavJobs::LocalItem item;
    item.collection = collection;
    item.component = componentFor(collection);
    item.id = row.value(QStringLiteral("id")).toString();
    item.uid = row.value(QStringLiteral("remoteUid")).toString();
    if (item.uid.isEmpty()) item.uid = item.id;
    item.href = row.value(QStringLiteral("remoteHref")).toString();
    item.etag = row.value(QStringLiteral("remoteEtag")).toString();
    item.remoteIcs = row.value(QStringLiteral("remoteIcs")).toString().toUtf8();
    item.syncDigest = row.value(QStringLiteral("syncDigest")).toString();
    item.dirty = item.href.isEmpty() || ICal::digest(item.component, row) != item.syncDigest;
    item.record = row;
    return item;
}
} // namespace

struct CalDavProvider::Prepared {
    CalDavJobs::Job job;
};

CalDavProvider::CalDavProvider(QObject *parent) : QObject(parent) {}

void CalDavProvider::setStore(LocalProductivityStore *store)
{
    if (m_store == store) return;
    m_store = store;
    emit configChanged();
}

void CalDavProvider::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret = secret;
    m_mechanism = mechanism;
    emit secretChanged();
}

void CalDavProvider::clearSecret()
{
    m_secret.fill(QLatin1Char('\0'));
    m_secret.clear();
    emit secretChanged();
}

QVariantList CalDavProvider::conflicts() const
{
    QVariantList out;
    for (const QVariantMap &entry : m_conflicts) out.append(entry);
    return out;
}

QVariantMap CalDavProvider::findRecord(const QString &collection, const QString &id) const
{
    if (!m_store) return {};
    for (const QVariant &value : collection == QLatin1String("tasks") ? m_store->tasks() : m_store->events()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("id")).toString() == id) return row;
    }
    return {};
}

bool CalDavProvider::prepare(Prepared *prepared, QString *error) const
{
    if (!m_store) { *error = QStringLiteral("No local store is configured for calendar sync"); return false; }
    const QUrl url(m_collectionUrl);
    const bool https = url.scheme() == QLatin1String("https");
    if (!url.isValid() || url.host().isEmpty() || (!https && url.scheme() != QLatin1String("http"))) {
        *error = QStringLiteral("The collection URL must be an http or https URL");
        return false;
    }
    CalDavJobs::Job &job = prepared->job;
    job.endpoint = {url.host(), static_cast<quint16>(url.port(https ? 443 : 80)),
                    https ? QStringLiteral("tls") : QStringLiteral("none"), m_timeoutMs, m_allowInsecureAuth};
    job.collectionPath = url.path(QUrl::FullyEncoded);
    if (!job.collectionPath.endsWith(QLatin1Char('/'))) job.collectionPath += QLatin1Char('/');
    job.accountId = m_accountId;
    job.defaultZone = m_defaultZone;
    if (!m_secret.isEmpty()) {
        if (!Protocol::plaintextAuthAllowed(url.host(), https, m_allowInsecureAuth)) {
            *error = QStringLiteral("Refusing to send credentials over plain HTTP");
            return false;
        }
        const QByteArray basic = (m_userName + QLatin1Char(':') + m_secret).toUtf8().toBase64();
        job.authorization = m_mechanism == QLatin1String("bearer") ? "Bearer " + m_secret.toUtf8() : "Basic " + basic;
        job.secrets = {m_secret, QString::fromLatin1(basic)};
    }
    for (const QString &collection : {QStringLiteral("events"), QStringLiteral("tasks")}) {
        for (const QVariant &value : collection == QLatin1String("tasks") ? m_store->tasks() : m_store->events()) {
            const QVariantMap row = value.toMap();
            // AGENT-GUARD: IcsCalendarProvider replicas (icsFeed) are read-only
            // copies of a feed; pushing them would publish someone else's feed.
            if (row.value(QStringLiteral("remoteAccount")).toString() == m_accountId
                && row.value(QStringLiteral("icsFeed")).toString().isEmpty())
                job.items.append(localItem(collection, row));
        }
    }
    return true;
}

bool CalDavProvider::sync(const QString &requestId)
{
    Prepared prepared;
    QString error;
    if (!prepare(&prepared, &error)) { deliver(true, requestId, rejection(error)); return false; }
    prepared.job.mode = CalDavJobs::Mode::Sync;
    launch(true, requestId, prepared);
    return true;
}

bool CalDavProvider::removeRemote(const QString &requestId, const QString &collection, const QString &recordId)
{
    Prepared prepared;
    QString error;
    const QVariantMap row = findRecord(collection, recordId);
    if (prepare(&prepared, &error) && row.value(QStringLiteral("remoteHref")).toString().isEmpty())
        error = QStringLiteral("Record %1 is not synced with this calendar").arg(recordId);
    if (!error.isEmpty()) { deliver(false, requestId, rejection(error)); return false; }
    prepared.job.mode = CalDavJobs::Mode::Delete;
    prepared.job.target = localItem(collection, row);
    prepared.job.ifMatch = prepared.job.target.etag;
    launch(false, requestId, prepared);
    return true;
}

bool CalDavProvider::resolveConflict(const QString &requestId, const QString &collection,
                                     const QString &recordId, const QString &choice)
{
    const QString key = conflictKey(collection, recordId);
    const QVariantMap entry = m_conflicts.value(key);
    const QVariantMap row = findRecord(collection, recordId);
    if (entry.isEmpty() || row.isEmpty()) {
        deliver(false, requestId, rejection(QStringLiteral("No conflict is recorded for %1").arg(recordId)));
        return false;
    }
    if (choice == QLatin1String("remote")) {
        if (entry.value(QStringLiteral("remoteDeleted")).toBool()) {
            m_store->remove(collection, recordId);
        } else {
            m_store->upsert(collection, remoteMerged(row, collection, recordId,
                entry.value(QStringLiteral("remoteFields")).toMap(),
                {{QStringLiteral("href"), entry.value(QStringLiteral("href"))},
                 {QStringLiteral("etag"), entry.value(QStringLiteral("remoteEtag"))},
                 {QStringLiteral("ics"), entry.value(QStringLiteral("remoteIcs"))},
                 {QStringLiteral("uid"), row.value(QStringLiteral("remoteUid"), recordId)}}));
        }
        m_conflicts.remove(key);
        emit conflictsChanged();
        deliver(false, requestId, persist({{QStringLiteral("ok"), true}, {QStringLiteral("state"), QStringLiteral("accepted")},
                                           {QStringLiteral("resolved"), choice}}));
        return true;
    }
    Prepared prepared;
    QString error;
    if (choice != QLatin1String("local")) error = QStringLiteral("Choose \"local\" or \"remote\"");
    else prepare(&prepared, &error);
    if (!error.isEmpty()) { deliver(false, requestId, rejection(error)); return false; }
    prepared.job.mode = CalDavJobs::Mode::Put;
    prepared.job.target = localItem(collection, row);
    prepared.job.target.href = entry.value(QStringLiteral("href")).toString();
    prepared.job.target.remoteIcs = entry.value(QStringLiteral("remoteIcs")).toString().toUtf8();
    prepared.job.create = entry.value(QStringLiteral("remoteDeleted")).toBool();
    prepared.job.ifMatch = entry.value(QStringLiteral("remoteEtag")).toString();
    launch(false, requestId, prepared);
    return true;
}

void CalDavProvider::launch(bool syncSignal, const QString &requestId, const Prepared &prepared)
{
    ++m_pending;
    m_state = QStringLiteral("working");
    emit stateChanged();
    const CalDavJobs::Job job = prepared.job;
    Protocol::startWorker(this, [job] { return CalDavJobs::run(job); },
                          [this, syncSignal, requestId](const QVariantMap &result) {
                              QVariantMap completed = result;
                              if (result.value(QStringLiteral("applicable")).toBool() && m_store) {
                                  applyResult(result);
                                  completed = persist(completed);
                              }
                              --m_pending;
                              deliver(syncSignal, requestId, completed);
                          });
}

QVariantMap CalDavProvider::remoteMerged(QVariantMap row, const QString &collection, const QString &id,
                                         const QVariantMap &fields, const QVariantMap &remote) const
{
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) row.insert(it.key(), it.value());
    row.insert(QStringLiteral("id"), id);
    row.insert(QStringLiteral("remoteAccount"), m_accountId);
    row.insert(QStringLiteral("remoteHref"), remote.value(QStringLiteral("href")));
    row.insert(QStringLiteral("remoteEtag"), remote.value(QStringLiteral("etag")));
    row.insert(QStringLiteral("remoteUid"), remote.value(QStringLiteral("uid")));
    row.insert(QStringLiteral("remoteIcs"), remote.value(QStringLiteral("ics")));
    row.insert(QStringLiteral("syncState"), QStringLiteral("synced"));
    if (collection == QLatin1String("events")) {
        row.insert(QStringLiteral("eventId"), id);
        if (row.value(QStringLiteral("calendarId")).toString().isEmpty()) row.insert(QStringLiteral("calendarId"), m_calendarId);
    }
    row.insert(QStringLiteral("syncDigest"), ICal::digest(componentFor(collection), row));
    return row;
}

void CalDavProvider::applyResult(const QVariantMap &result)
{
    const qsizetype conflictCount = m_conflicts.size();
    for (const QVariant &value : result.value(QStringLiteral("fetched")).toList()) {
        const QVariantMap entry = value.toMap();
        const QString collection = entry.value(QStringLiteral("collection")).toString();
        const QString id = entry.value(QStringLiteral("id")).toString();
        m_store->upsert(collection, remoteMerged(findRecord(collection, id), collection, id,
                                                 entry.value(QStringLiteral("fields")).toMap(), entry));
        m_conflicts.remove(conflictKey(collection, id));
    }
    for (const QVariant &value : result.value(QStringLiteral("removed")).toList()) {
        const QVariantMap entry = value.toMap();
        const QString collection = entry.value(QStringLiteral("collection")).toString();
        m_store->remove(collection, entry.value(QStringLiteral("id")).toString());
        m_conflicts.remove(conflictKey(collection, entry.value(QStringLiteral("id")).toString()));
    }
    for (const QVariant &value : result.value(QStringLiteral("pushed")).toList()) {
        const QVariantMap entry = value.toMap();
        const QString collection = entry.value(QStringLiteral("collection")).toString();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QVariantMap row = findRecord(collection, id);
        if (row.isEmpty()) continue;
        const QString component = componentFor(collection);
        // AGENT-NOTE: adopt the canonical mapping of what was sent (e.g. a
        // "2026-09-24T10:00:00" start becomes "2026-09-24 10:00") so the next
        // digest matches; a record edited during the upload keeps its edit
        // and stays dirty for the next sync.
        const ICal::Mapped sent = ICal::toRecord(entry.value(QStringLiteral("ics")).toString().toUtf8(), m_defaultZone);
        const QVariantMap normalized = remoteMerged(row, collection, id, sent.fields, entry);
        if (ICal::digest(component, row) == entry.value(QStringLiteral("digest")).toString()) {
            m_store->upsert(collection, normalized);
        } else {
            QVariantMap kept = row;
            for (const QString &key : {QStringLiteral("remoteAccount"), QStringLiteral("remoteHref"), QStringLiteral("remoteEtag"),
                                       QStringLiteral("remoteUid"), QStringLiteral("remoteIcs"), QStringLiteral("syncDigest")})
                kept.insert(key, normalized.value(key));
            m_store->upsert(collection, kept);
        }
        m_conflicts.remove(conflictKey(collection, id));
    }
    for (const QVariant &value : result.value(QStringLiteral("conflicts")).toList()) {
        const QVariantMap entry = value.toMap();
        const QString collection = entry.value(QStringLiteral("collection")).toString();
        const QString id = entry.value(QStringLiteral("id")).toString();
        QVariantMap row = findRecord(collection, id);
        if (row.isEmpty()) continue;
        row.insert(QStringLiteral("syncState"), QStringLiteral("conflict"));
        m_store->upsert(collection, row);
        m_conflicts.insert(conflictKey(collection, id), entry);
    }
    if (conflictCount != m_conflicts.size() || !result.value(QStringLiteral("conflicts")).toList().isEmpty())
        emit conflictsChanged();
}

QVariantMap CalDavProvider::persist(QVariantMap result)
{
    if (!m_autoSave || !m_store || m_store->save()) return result;
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("state"), m_store->status() == QLatin1String("conflict")
                                               ? QStringLiteral("conflict") : QStringLiteral("rejected"));
    result.insert(QStringLiteral("error"), m_store->lastError());
    return result;
}

void CalDavProvider::deliver(bool syncSignal, const QString &requestId, QVariantMap result)
{
    result.insert(QStringLiteral("requestId"), requestId);
    result.insert(QStringLiteral("error"), Protocol::redact(result.value(QStringLiteral("error")).toString(), {m_secret}));
    result.insert(QStringLiteral("conflictCount"), m_conflicts.size());
    m_state = result.value(QStringLiteral("state")).toString();
    m_lastError = result.value(QStringLiteral("error")).toString();
    emit stateChanged();
    // Synchronous rejections are delivered on the next event-loop turn, like
    // network results, so callers can connect after calling.
    QMetaObject::invokeMethod(this, [this, syncSignal, requestId, result] {
        if (syncSignal) emit syncFinished(requestId, result);
        else emit changeFinished(requestId, result);
    }, Qt::QueuedConnection);
}

} // namespace QindaTK
