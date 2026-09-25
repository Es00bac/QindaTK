// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ics_calendar_provider.h"

#include "ical_codec.h"
#include "ics_calendar_jobs.h"
#include "protocol_client.h"

#include <QSet>
#include <QTimeZone>

namespace QindaTK {
namespace {
QVariantMap rejection(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("state"), QStringLiteral("rejected")},
            {QStringLiteral("error"), error}, {QStringLiteral("retryable"), false}};
}

QString uidOf(const QVariantMap &event)
{
    const QString uid = event.value(QStringLiteral("remoteUid")).toString();
    return uid.isEmpty() ? event.value(QStringLiteral("id")).toString() : uid;
}

// fields of one parsed item merged over `row` (keeps host-added fields).
QVariantMap mergedRecord(QVariantMap row, const QVariantMap &item)
{
    const QVariantMap fields = item.value(QStringLiteral("fields")).toMap();
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) row.insert(it.key(), it.value());
    row.insert(QStringLiteral("remoteUid"), item.value(QStringLiteral("uid")));
    row.insert(QStringLiteral("remoteIcs"), item.value(QStringLiteral("ics")));
    return row;
}
} // namespace

IcsCalendarProvider::IcsCalendarProvider(QObject *parent) : QObject(parent) {}

void IcsCalendarProvider::setStore(LocalProductivityStore *store)
{
    if (m_store == store) return;
    m_store = store;
    emit configChanged();
}

void IcsCalendarProvider::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret = secret;
    m_mechanism = mechanism;
    emit secretChanged();
}

void IcsCalendarProvider::clearSecret()
{
    m_secret.fill(QLatin1Char('\0'));
    m_secret.clear();
    emit secretChanged();
}

QString IcsCalendarProvider::zone() const
{
    // Floating times belong to the user's wall clock, so the default is the
    // system zone rather than UTC.
    if (!m_defaultZone.isEmpty()) return m_defaultZone;
    const QByteArray system = QTimeZone::systemTimeZoneId();
    return QTimeZone(system).isValid() ? QString::fromUtf8(system) : QStringLiteral("UTC");
}

QVariantList IcsCalendarProvider::feedEvents(const QString &feed, const QString &calendarId) const
{
    QVariantList out;
    if (!m_store) return out;
    for (const QVariant &value : m_store->events()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("icsFeed")).toString() == feed
            && row.value(QStringLiteral("calendarId")).toString() == calendarId)
            out.append(row);
    }
    return out;
}

bool IcsCalendarProvider::sync(const QString &requestId)
{
    QString error;
    const QUrl url = IcsJobs::normalizedFeedUrl(m_feedUrl, &error);
    if (!m_store) error = QStringLiteral("No local store is configured for the calendar feed");
    const bool https = url.scheme() == QLatin1String("https");
    if (error.isEmpty() && !m_secret.isEmpty() && !Protocol::plaintextAuthAllowed(url.host(), https, m_allowInsecureAuth))
        error = QStringLiteral("Refusing to send feed credentials over plain HTTP");
    if (!error.isEmpty()) {
        deliver(Operation::Sync, requestId, rejection(error));
        return false;
    }
    const QString feed = url.toString(QUrl::FullyEncoded);
    const QString calendarId = m_calendarId;
    IcsJobs::FeedJob job;
    job.url = url;
    job.timeoutMs = m_timeoutMs;
    job.allowInsecureAuth = m_allowInsecureAuth;
    job.maximumBytes = qMax(1, m_maximumBytes);
    job.defaultZone = zone();
    if (!m_secret.isEmpty()) {
        const QByteArray basic = (m_userName + QLatin1Char(':') + m_secret).toUtf8().toBase64();
        job.authorization = m_mechanism == QLatin1String("bearer") ? "Bearer " + m_secret.toUtf8() : "Basic " + basic;
        job.secrets = {m_secret, QString::fromLatin1(basic)};
    }
    // AGENT-NOTE: validators come from this session's last 200 or, after a
    // restart, from the stored replicas. They are only sent while replicas
    // exist (or the feed was empty), so a 304 never leaves the calendar blank.
    const QVariantList existing = feedEvents(feed, calendarId);
    const QString key = calendarId + QLatin1Char('\n') + feed;
    if (m_validators.contains(key) && (!existing.isEmpty() || m_validators.value(key).eventCount == 0)) {
        job.etag = m_validators.value(key).etag;
        job.lastModified = m_validators.value(key).lastModified;
    } else if (!existing.isEmpty()) {
        job.etag = existing.first().toMap().value(QStringLiteral("feedEtag")).toString();
        job.lastModified = existing.first().toMap().value(QStringLiteral("feedLastModified")).toString();
    }
    launch(Operation::Sync, requestId, [job] { return IcsJobs::fetchFeed(job); },
           [this, feed, calendarId](QVariantMap result) { return applyFeed(result, feed, calendarId); });
    return true;
}

QVariantMap IcsCalendarProvider::applyFeed(QVariantMap result, const QString &feed, const QString &calendarId)
{
    result.insert(QStringLiteral("feedUrl"), feed);
    result.insert(QStringLiteral("calendarId"), calendarId);
    const QVariantList existing = feedEvents(feed, calendarId);
    if (result.value(QStringLiteral("unchanged")).toBool()) {
        result.insert(QStringLiteral("eventCount"), existing.size());
        return result;
    }
    const QString etag = result.value(QStringLiteral("feedEtag")).toString();
    const QString modified = result.value(QStringLiteral("feedLastModified")).toString();
    QHash<QString, QVariantMap> byId;
    for (const QVariant &value : existing) byId.insert(value.toMap().value(QStringLiteral("id")).toString(), value.toMap());
    QSet<QString> kept;
    int added = 0, updated = 0, skipped = 0;
    QStringList warnings = result.value(QStringLiteral("warnings")).toStringList();
    for (const QVariant &value : result.value(QStringLiteral("items")).toList()) {
        const QVariantMap item = value.toMap();
        if (!item.value(QStringLiteral("ok")).toBool()) { ++skipped; continue; }
        if (item.value(QStringLiteral("component")).toString() != QLatin1String("VEVENT")) {
            ++skipped;
            warnings.append(QStringLiteral("%1: tasks in a feed are not replicated").arg(item.value(QStringLiteral("uid")).toString()));
            continue;
        }
        const QString id = QStringLiteral("ics:%1:%2:%3").arg(m_accountId, calendarId, item.value(QStringLiteral("uid")).toString());
        if (kept.contains(id)) continue;
        kept.insert(id);
        const QVariantMap old = byId.value(id);
        QVariantMap row = mergedRecord(old, item);
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("eventId"), id);
        row.insert(QStringLiteral("calendarId"), calendarId);
        row.insert(QStringLiteral("readOnly"), true);
        row.insert(QStringLiteral("remoteAccount"), m_accountId);
        row.insert(QStringLiteral("icsFeed"), feed);
        row.insert(QStringLiteral("feedEtag"), etag);
        row.insert(QStringLiteral("feedLastModified"), modified);
        row.insert(QStringLiteral("syncState"), QStringLiteral("synced"));
        if (row == old) continue;
        if (!m_store->upsert(QStringLiteral("events"), row).value(QStringLiteral("ok")).toBool()) { ++skipped; continue; }
        // A new validator alone is not a content change.
        QVariantMap before = old, after = row;
        for (const QString &field : {QStringLiteral("feedEtag"), QStringLiteral("feedLastModified")}) {
            before.remove(field);
            after.remove(field);
        }
        if (old.isEmpty()) ++added;
        else if (before != after) ++updated;
    }
    int removed = 0;
    for (auto it = byId.cbegin(); it != byId.cend(); ++it)
        if (!kept.contains(it.key())) removed += m_store->remove(QStringLiteral("events"), it.key()).value(QStringLiteral("ok")).toBool();
    m_validators.insert(calendarId + QLatin1Char('\n') + feed, {etag, modified, int(kept.size())});
    result.remove(QStringLiteral("items"));
    result.insert(QStringLiteral("warnings"), warnings);
    result.insert(QStringLiteral("added"), added);
    result.insert(QStringLiteral("updated"), updated);
    result.insert(QStringLiteral("removed"), removed);
    result.insert(QStringLiteral("skipped"), skipped);
    result.insert(QStringLiteral("eventCount"), kept.size());
    return persist(result);
}

bool IcsCalendarProvider::importFile(const QString &requestId, const QString &path, const QString &calendarId)
{
    const QString local = IcsJobs::localPath(path);
    const QString target = calendarId.isEmpty() ? m_calendarId : calendarId;
    QString error;
    if (!m_store) error = QStringLiteral("No local store is configured for the import");
    else if (local.isEmpty()) error = QStringLiteral("Import needs a local file path or file: URL");
    else if (target.isEmpty()) error = QStringLiteral("Import needs a calendar ID");
    if (!error.isEmpty()) {
        deliver(Operation::Import, requestId, rejection(error));
        return false;
    }
    const qint64 limit = qMax(1, m_maximumBytes);
    const QString defaultZone = zone();
    launch(Operation::Import, requestId, [local, limit, defaultZone] { return IcsJobs::readFile(local, limit, defaultZone); },
           [this, target](QVariantMap result) { return applyImport(result, target); });
    return true;
}

QVariantMap IcsCalendarProvider::applyImport(QVariantMap result, const QString &calendarId)
{
    int imported = 0, updated = 0, unchanged = 0, skipped = 0;
    QStringList warnings = result.value(QStringLiteral("warnings")).toStringList(), touched;
    for (const QVariant &value : result.value(QStringLiteral("items")).toList()) {
        const QVariantMap item = value.toMap();
        const QString uid = item.value(QStringLiteral("uid")).toString();
        if (!item.value(QStringLiteral("ok")).toBool()) { ++skipped; continue; }
        if (item.value(QStringLiteral("component")).toString() != QLatin1String("VEVENT")) {
            ++skipped;
            warnings.append(QStringLiteral("%1: tasks are not imported into a calendar").arg(uid));
            continue;
        }
        // AGENT-NOTE: the UID is the identity: a second import of the same
        // file updates the events it created instead of duplicating them.
        QVariantMap existing;
        bool foreign = false;
        QSet<QString> ids;
        for (const QVariant &row : m_store->events()) {
            const QVariantMap event = row.toMap();
            ids.insert(event.value(QStringLiteral("id")).toString());
            if (event.value(QStringLiteral("calendarId")).toString() != calendarId || uidOf(event) != uid) continue;
            if (!event.value(QStringLiteral("remoteAccount")).toString().isEmpty() || event.value(QStringLiteral("readOnly")).toBool())
                foreign = true;
            else if (existing.isEmpty())
                existing = event;
        }
        if (existing.isEmpty() && foreign) {
            ++skipped;
            warnings.append(QStringLiteral("%1: an account event with this UID already exists in %2").arg(uid, calendarId));
            continue;
        }
        QVariantMap row = mergedRecord(existing, item);
        if (existing.isEmpty()) {
            QString id = QStringLiteral("import:%1:%2").arg(calendarId, uid);
            for (int n = 2; ids.contains(id); ++n) id = QStringLiteral("import:%1:%2~%3").arg(calendarId, uid).arg(n);
            row.insert(QStringLiteral("id"), id);
            row.insert(QStringLiteral("eventId"), id);
            row.insert(QStringLiteral("calendarId"), calendarId);
        }
        touched.append(row.value(QStringLiteral("id")).toString());
        if (row == existing) { ++unchanged; continue; }
        if (!m_store->upsert(QStringLiteral("events"), row).value(QStringLiteral("ok")).toBool()) { ++skipped; continue; }
        existing.isEmpty() ? ++imported : ++updated;
    }
    result.remove(QStringLiteral("items"));
    result.insert(QStringLiteral("calendarId"), calendarId);
    result.insert(QStringLiteral("eventIds"), touched);
    result.insert(QStringLiteral("imported"), imported);
    result.insert(QStringLiteral("updated"), updated);
    result.insert(QStringLiteral("unchanged"), unchanged);
    result.insert(QStringLiteral("skipped"), skipped);
    result.insert(QStringLiteral("warnings"), warnings);
    return imported + updated > 0 ? persist(result) : result;
}

bool IcsCalendarProvider::exportFile(const QString &requestId, const QString &path, const QString &calendarId,
                                     const QStringList &eventIds)
{
    const QString local = IcsJobs::localPath(path);
    const QString source = calendarId.isEmpty() ? m_calendarId : calendarId;
    QVariantList events;
    if (m_store) {
        for (const QVariant &value : m_store->events()) {
            const QVariantMap event = value.toMap();
            if (event.value(QStringLiteral("calendarId")).toString() != source) continue;
            if (!eventIds.isEmpty() && !eventIds.contains(event.value(QStringLiteral("id")).toString())) continue;
            events.append(event);
        }
    }
    QVariantMap serialized = serializeEvents(events);
    QString error;
    if (!m_store) error = QStringLiteral("No local store is configured for the export");
    else if (local.isEmpty()) error = QStringLiteral("Export needs a local file path or file: URL");
    else if (serialized.value(QStringLiteral("count")).toInt() == 0)
        error = QStringLiteral("Calendar %1 has no events to export").arg(source);   // RFC 5545 needs one component
    if (!error.isEmpty()) {
        deliver(Operation::Export, requestId, rejection(error));
        return false;
    }
    const QByteArray bytes = serialized.value(QStringLiteral("text")).toString().toUtf8();
    const int count = serialized.value(QStringLiteral("count")).toInt();
    const QStringList warnings = serialized.value(QStringLiteral("warnings")).toStringList();
    launch(Operation::Export, requestId, [local, bytes] { return IcsJobs::writeFile(local, bytes); },
           [source, count, warnings](QVariantMap result) {
               result.insert(QStringLiteral("calendarId"), source);
               result.insert(QStringLiteral("exported"), count);
               result.insert(QStringLiteral("warnings"), warnings);
               return result;
           });
    return true;
}

QVariantMap IcsCalendarProvider::parseText(const QString &text) const
{
    QVariantMap parsed = IcsJobs::parse(text.toUtf8(), zone());
    QVariantList events, tasks;
    for (const QVariant &value : parsed.value(QStringLiteral("items")).toList()) {
        const QVariantMap item = value.toMap();
        if (!item.value(QStringLiteral("ok")).toBool()) continue;
        QVariantMap row = mergedRecord({}, item);
        row.insert(QStringLiteral("id"), item.value(QStringLiteral("uid")));
        (item.value(QStringLiteral("component")).toString() == QLatin1String("VTODO") ? tasks : events).append(row);
    }
    parsed.remove(QStringLiteral("items"));
    parsed.remove(QStringLiteral("truncated"));
    parsed.insert(QStringLiteral("events"), events);
    parsed.insert(QStringLiteral("tasks"), tasks);
    return parsed;
}

QVariantMap IcsCalendarProvider::serializeEvents(const QVariantList &events) const
{
    QList<QByteArray> calendars;
    QStringList warnings;
    for (const QVariant &value : events) {
        const QVariantMap event = value.toMap();
        const QString uid = uidOf(event);
        if (uid.isEmpty() || event.value(QStringLiteral("startLocal")).toString().isEmpty()) {
            warnings.append(QStringLiteral("%1: an event needs an id and a start").arg(uid));
            continue;
        }
        // The same writer as CalDAV: overrides become RECURRENCE-ID components
        // and the source text's other properties (alarms, attendees) survive.
        calendars.append(ICal::fromRecord(QStringLiteral("VEVENT"), uid, event,
                                          event.value(QStringLiteral("remoteIcs")).toString().toUtf8()));
    }
    const QByteArray text = calendars.isEmpty() ? QByteArray() : ICal::mergeCalendars(calendars);
    return {{QStringLiteral("ok"), !calendars.isEmpty()},
            {QStringLiteral("error"), calendars.isEmpty() ? QStringLiteral("No event could be written") : QString()},
            {QStringLiteral("text"), QString::fromUtf8(text)}, {QStringLiteral("count"), calendars.size()},
            {QStringLiteral("warnings"), warnings}};
}

void IcsCalendarProvider::launch(Operation operation, const QString &requestId, std::function<QVariantMap()> work,
                                 std::function<QVariantMap(QVariantMap)> apply)
{
    ++m_pending;
    m_state = QStringLiteral("working");
    emit stateChanged();
    Protocol::startWorker(this, std::move(work), [this, operation, requestId, apply](const QVariantMap &result) {
        --m_pending;
        const bool applicable = result.value(QStringLiteral("ok")).toBool() && m_store;
        deliver(operation, requestId, applicable ? apply(result) : result);
    });
}

QVariantMap IcsCalendarProvider::persist(QVariantMap result)
{
    if (!m_autoSave || !m_store || m_store->save()) return result;
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("state"), m_store->status() == QLatin1String("conflict")
                                               ? QStringLiteral("conflict") : QStringLiteral("rejected"));
    result.insert(QStringLiteral("error"), m_store->lastError());
    return result;
}

void IcsCalendarProvider::deliver(Operation operation, const QString &requestId, QVariantMap result)
{
    result.remove(QStringLiteral("items"));
    result.insert(QStringLiteral("requestId"), requestId);
    result.insert(QStringLiteral("error"), Protocol::redact(result.value(QStringLiteral("error")).toString(), {m_secret}));
    if (!result.contains(QStringLiteral("transcript"))) result.insert(QStringLiteral("transcript"), QStringList());
    m_state = result.value(QStringLiteral("state")).toString();
    m_lastError = result.value(QStringLiteral("error")).toString();
    emit stateChanged();
    // Synchronous rejections arrive on the next event-loop turn, like results.
    QMetaObject::invokeMethod(this, [this, operation, requestId, result] {
        switch (operation) {
        case Operation::Sync: emit syncFinished(requestId, result); break;
        case Operation::Import: emit importFinished(requestId, result); break;
        case Operation::Export: emit exportFinished(requestId, result); break;
        }
    }, Qt::QueuedConnection);
}

} // namespace QindaTK
