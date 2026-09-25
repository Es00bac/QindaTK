// SPDX-License-Identifier: LGPL-3.0-or-later
#include "caldav_jobs.h"

#include "ical_codec.h"

#include <QSet>
#include <QUrl>
#include <QXmlStreamReader>

namespace QindaTK::CalDavJobs {
namespace {
struct Listed {
    QString href;                 // request target as the server gave it
    QString etag;
};

QString pathKey(const QString &href)
{
    QString key = QUrl::fromPercentEncoding(QUrl(href).path(QUrl::FullyEncoded).toUtf8());
    while (key.endsWith(QLatin1Char('/'))) key.chop(1);
    return key;
}

class Exchange {
public:
    explicit Exchange(const Job &job) : m_job(job)
    {
        for (const QString &secret : job.secrets) m_channel.addSecret(secret);
    }
    Protocol::Channel &channel() { return m_channel; }

    bool request(const QByteArray &method, const QString &target, const QMap<QByteArray, QByteArray> &extra,
                 const QByteArray &body, Protocol::HttpResponse *response)
    {
        QMap<QByteArray, QByteArray> headers = extra;
        if (!m_job.authorization.isEmpty()) headers.insert("Authorization", m_job.authorization);
        if (!Protocol::httpRequest(m_channel, m_job.endpoint, method, target.toUtf8(), headers, body, response))
            return false;
        if (response->status == 401 || response->status == 403) {
            m_channel.fail(Protocol::Failure::Rejected,
                           QStringLiteral("The calendar server refused the credentials (HTTP %1)").arg(response->status));
            return false;
        }
        return true;
    }

    // PROPFIND Depth 1: member href -> ETag, keyed by decoded path.
    bool list(QMap<QString, Listed> *out)
    {
        const QByteArray body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                                "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:getetag/></d:prop></d:propfind>";
        Protocol::HttpResponse response;
        if (!request("PROPFIND", m_job.collectionPath,
                     {{"Depth", "1"}, {"Content-Type", "application/xml; charset=utf-8"}}, body, &response))
            return false;
        if (response.status != 207) {
            m_channel.fail(Protocol::Failure::Rejected,
                           QStringLiteral("Calendar listing failed (HTTP %1)").arg(response.status));
            return false;
        }
        const QString self = pathKey(m_job.collectionPath);
        QXmlStreamReader xml(response.body);
        QString href, etag, pending, status;
        bool inPropstat = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const QStringView name = xml.name();
                if (name == u"response") { href.clear(); etag.clear(); }
                else if (name == u"href" && !inPropstat) href = xml.readElementText().trimmed();
                else if (name == u"propstat") { inPropstat = true; pending.clear(); status.clear(); }
                else if (name == u"getetag") pending = xml.readElementText().trimmed();
                else if (name == u"status" && inPropstat) status = xml.readElementText();
            } else if (xml.isEndElement()) {
                const QStringView name = xml.name();
                if (name == u"propstat") {
                    inPropstat = false;
                    if (status.contains(QLatin1String(" 200"))) etag = pending;
                } else if (name == u"response") {
                    const QString path = QUrl(href).path(QUrl::FullyEncoded);
                    if (!href.isEmpty() && !path.endsWith(QLatin1Char('/')) && pathKey(path) != self && !etag.isEmpty())
                        out->insert(pathKey(path), {path, etag});
                }
            }
        }
        if (xml.hasError()) {
            m_channel.fail(Protocol::Failure::Rejected, QStringLiteral("Malformed calendar listing: ") + xml.errorString());
            return false;
        }
        return true;
    }

    // GET: true with status 200 (ics + etag) or 404 (gone); false on failure.
    bool fetch(const QString &href, QByteArray *ics, QString *etag, bool *gone)
    {
        Protocol::HttpResponse response;
        if (!request("GET", href, {}, {}, &response)) return false;
        *gone = response.status == 404 || response.status == 410;
        if (!*gone && response.status != 200) {
            m_errors.append(QStringLiteral("GET %1 returned HTTP %2").arg(href).arg(response.status));
            return false;
        }
        *ics = response.body;
        *etag = QString::fromUtf8(response.headers.value("etag"));
        return true;
    }

    void conflictFrom(const LocalItem &item, const QString &href, const QByteArray &ics, const QString &etag, bool gone)
    {
        QVariantMap entry{{QStringLiteral("collection"), item.collection}, {QStringLiteral("id"), item.id},
                          {QStringLiteral("href"), href}, {QStringLiteral("remoteDeleted"), gone}};
        if (!gone) {
            const ICal::Mapped mapped = ICal::toRecord(ics, m_job.defaultZone);
            entry.insert(QStringLiteral("remoteEtag"), etag);
            entry.insert(QStringLiteral("remoteIcs"), QString::fromUtf8(ics));
            entry.insert(QStringLiteral("remoteFields"), mapped.fields);
        }
        m_conflicts.append(entry);
    }

    void conflict(const LocalItem &item, const QString &href)
    {
        QByteArray ics;
        QString etag;
        bool gone = false;
        if (fetch(href, &ics, &etag, &gone)) conflictFrom(item, href, ics, etag, gone);
    }

    void put(const LocalItem &item, bool create, const QString &ifMatch)
    {
        const QString href = item.href.isEmpty()
            ? m_job.collectionPath + QString::fromLatin1(QUrl::toPercentEncoding(item.uid)) + QStringLiteral(".ics")
            : item.href;
        const QByteArray ics = ICal::fromRecord(item.component, item.uid, item.record, item.remoteIcs);
        QMap<QByteArray, QByteArray> headers{{"Content-Type", "text/calendar; charset=utf-8"}};
        if (create) headers.insert("If-None-Match", "*");
        else if (!ifMatch.isEmpty()) headers.insert("If-Match", ifMatch.toUtf8());
        Protocol::HttpResponse response;
        if (!request("PUT", href, headers, ics, &response)) return;
        if (response.status == 412) { conflict(item, href); return; }
        if (response.status < 200 || response.status > 299) {
            m_errors.append(QStringLiteral("PUT %1 returned HTTP %2").arg(href).arg(response.status));
            return;
        }
        m_pushed.append(QVariantMap{{QStringLiteral("collection"), item.collection}, {QStringLiteral("id"), item.id},
            {QStringLiteral("uid"), item.uid}, {QStringLiteral("href"), href},
            {QStringLiteral("etag"), QString::fromUtf8(response.headers.value("etag"))},
            {QStringLiteral("ics"), QString::fromUtf8(ics)},
            {QStringLiteral("digest"), ICal::digest(item.component, item.record)}});
    }

    void remove(const LocalItem &item, const QString &ifMatch)
    {
        Protocol::HttpResponse response;
        QMap<QByteArray, QByteArray> headers;
        if (!ifMatch.isEmpty()) headers.insert("If-Match", ifMatch.toUtf8());
        if (!request("DELETE", item.href, headers, {}, &response)) return;
        if (response.status == 412) { conflict(item, item.href); return; }
        if (response.status != 404 && response.status != 410 && (response.status < 200 || response.status > 299)) {
            m_errors.append(QStringLiteral("DELETE %1 returned HTTP %2").arg(item.href).arg(response.status));
            return;
        }
        m_removed.append(QVariantMap{{QStringLiteral("collection"), item.collection}, {QStringLiteral("id"), item.id}});
    }

    void sync()
    {
        QMap<QString, Listed> remote;
        if (!list(&remote)) return;
        QMap<QString, const LocalItem *> byPath;
        for (const LocalItem &item : m_job.items)
            if (!item.href.isEmpty()) byPath.insert(pathKey(item.href), &item);
        QSet<QString> settled;        // ids already fetched, conflicted or removed
        for (auto it = remote.cbegin(); it != remote.cend(); ++it) {
            const LocalItem *local = byPath.value(it.key());
            if (local && local->etag == it->etag) continue;
            if (local && local->dirty) {
                // The ETag moved, but the server may still hold exactly what we
                // last synced (re-serialized, or no ETag came back from our PUT):
                // then this is a plain local edit, otherwise a real conflict.
                settled.insert(local->id);
                QByteArray ics;
                QString etag;
                bool gone = false;
                if (!fetch(it->href, &ics, &etag, &gone)) continue;
                const QString current = etag.isEmpty() ? it->etag : etag;
                const ICal::Mapped mapped = gone ? ICal::Mapped() : ICal::toRecord(ics, m_job.defaultZone);
                if (mapped.ok && ICal::digest(mapped.component, mapped.fields) == local->syncDigest) {
                    LocalItem updated = *local;
                    updated.remoteIcs = ics;
                    put(updated, false, current);
                } else {
                    conflictFrom(*local, it->href, ics, current, gone);
                }
                continue;
            }
            QByteArray ics;
            QString etag;
            bool gone = false;
            if (!fetch(it->href, &ics, &etag, &gone) || gone) continue;
            const ICal::Mapped mapped = ICal::toRecord(ics, m_job.defaultZone);
            if (!mapped.ok) { m_errors.append(QStringLiteral("%1: %2").arg(it->href, mapped.error)); continue; }
            const QString id = local ? local->id : QStringLiteral("caldav:%1:%2").arg(m_job.accountId, mapped.uid);
            if (local) settled.insert(local->id);
            m_fetched.append(QVariantMap{{QStringLiteral("collection"), collectionFor(mapped.component)},
                {QStringLiteral("component"), mapped.component}, {QStringLiteral("id"), id},
                {QStringLiteral("uid"), mapped.uid}, {QStringLiteral("href"), it->href},
                {QStringLiteral("etag"), etag.isEmpty() ? it->etag : etag},
                {QStringLiteral("ics"), QString::fromUtf8(ics)}, {QStringLiteral("fields"), mapped.fields}});
        }
        for (const LocalItem &item : m_job.items) {
            if (item.href.isEmpty() || remote.contains(pathKey(item.href))) continue;
            settled.insert(item.id);
            if (item.dirty) {
                m_conflicts.append(QVariantMap{{QStringLiteral("collection"), item.collection},
                    {QStringLiteral("id"), item.id}, {QStringLiteral("href"), item.href},
                    {QStringLiteral("remoteDeleted"), true}});
            } else {
                m_removed.append(QVariantMap{{QStringLiteral("collection"), item.collection}, {QStringLiteral("id"), item.id}});
            }
        }
        for (const LocalItem &item : m_job.items) {
            if (settled.contains(item.id) || m_channel.failure() != Protocol::Failure::None) continue;
            if (item.href.isEmpty()) put(item, true, {});
            else if (item.dirty) put(item, false, item.etag);
        }
    }

    QVariantMap result()
    {
        const Protocol::Failure failure = m_channel.failure();
        const bool applicable = failure == Protocol::Failure::None;
        const QString state = !applicable ? Protocol::failureState(failure)
            : !m_errors.isEmpty() ? QStringLiteral("rejected")
            : !m_conflicts.isEmpty() ? QStringLiteral("conflict") : QStringLiteral("accepted");
        QString error = applicable ? m_errors.join(QStringLiteral("; ")) : m_channel.error();
        if (error.isEmpty() && !m_conflicts.isEmpty())
            error = QStringLiteral("%1 record(s) changed both here and on the server").arg(m_conflicts.size());
        return {{QStringLiteral("ok"), state == QLatin1String("accepted")}, {QStringLiteral("state"), state},
                {QStringLiteral("applicable"), applicable}, {QStringLiteral("error"), Protocol::redact(error, m_job.secrets)},
                {QStringLiteral("retryable"), failure == Protocol::Failure::Offline},
                {QStringLiteral("fetched"), m_fetched}, {QStringLiteral("removed"), m_removed},
                {QStringLiteral("pushed"), m_pushed}, {QStringLiteral("conflicts"), m_conflicts},
                {QStringLiteral("transcript"), m_channel.transcript()}};
    }

private:
    const Job &m_job;
    Protocol::Channel m_channel;
    QVariantList m_fetched, m_removed, m_pushed, m_conflicts;
    QStringList m_errors;
};
} // namespace

QString collectionFor(const QString &component)
{
    return component == QLatin1String("VTODO") ? QStringLiteral("tasks") : QStringLiteral("events");
}

QVariantMap run(const Job &job)
{
    Exchange exchange(job);
    if (job.mode == Mode::Sync) exchange.sync();
    else if (job.mode == Mode::Put) exchange.put(job.target, job.create, job.ifMatch);
    else exchange.remove(job.target, job.ifMatch);
    return exchange.result();
}

} // namespace QindaTK::CalDavJobs
