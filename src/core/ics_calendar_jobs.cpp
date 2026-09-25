// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ics_calendar_jobs.h"

#include "caldav_discovery.h"
#include "ical_codec.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace QindaTK::IcsJobs {
namespace {
QVariantMap finished(const Protocol::Channel &channel, QVariantMap result)
{
    const Protocol::Failure failure = channel.failure();
    const bool ok = failure == Protocol::Failure::None && result.value(QStringLiteral("error")).toString().isEmpty();
    if (failure != Protocol::Failure::None) result.insert(QStringLiteral("error"), channel.error());
    result.insert(QStringLiteral("ok"), ok);
    if (!result.contains(QStringLiteral("state")) || failure != Protocol::Failure::None)
        result.insert(QStringLiteral("state"), ok ? QStringLiteral("accepted") : Protocol::failureState(
                                                        failure == Protocol::Failure::None ? Protocol::Failure::Rejected : failure));
    result.insert(QStringLiteral("retryable"), failure == Protocol::Failure::Offline
                                                   || result.value(QStringLiteral("retryable")).toBool());
    result.insert(QStringLiteral("transcript"), channel.transcript());
    return result;
}

QVariantMap failedResult(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("state"), QStringLiteral("rejected")},
            {QStringLiteral("error"), error}, {QStringLiteral("retryable"), false},
            {QStringLiteral("transcript"), QStringList()}};
}
} // namespace

QUrl normalizedFeedUrl(const QString &text, QString *error)
{
    QUrl url(text.trimmed());
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("webcal") || scheme == QLatin1String("webcals")) url.setScheme(QStringLiteral("https"));
    else url.setScheme(scheme);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))) {
        *error = QStringLiteral("The feed URL must be an https:, http: or webcal: URL");
        return {};
    }
    if (!url.userInfo().isEmpty()) {
        *error = QStringLiteral("Put feed credentials in setSecret, not in the URL");
        return {};
    }
    url.setFragment(QString());
    return url;
}

QString localPath(const QString &pathOrUrl)
{
    const QString trimmed = pathOrUrl.trimmed();
    if (trimmed.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) return QUrl(trimmed).toLocalFile();
    return QUrl(trimmed).scheme().size() > 1 ? QString() : trimmed;
}

QVariantMap parse(const QByteArray &text, const QString &defaultZone)
{
    const ICal::ParsedCalendar parsed = ICal::parseCalendar(text, defaultZone);
    QVariantList items;
    for (const ICal::CalendarItem &item : parsed.items)
        items.append(QVariantMap{{QStringLiteral("ok"), item.mapped.ok}, {QStringLiteral("error"), item.mapped.error},
                                 {QStringLiteral("component"), item.mapped.component},
                                 {QStringLiteral("uid"), item.mapped.uid}, {QStringLiteral("fields"), item.mapped.fields},
                                 {QStringLiteral("ics"), QString::fromUtf8(item.ics)}});
    return {{QStringLiteral("ok"), parsed.ok}, {QStringLiteral("error"), parsed.error},
            {QStringLiteral("calendarName"), parsed.name}, {QStringLiteral("items"), items},
            {QStringLiteral("warnings"), parsed.warnings}, {QStringLiteral("truncated"), parsed.truncated}};
}

QVariantMap fetchFeed(const FeedJob &job)
{
    Protocol::Channel channel;
    for (const QString &secret : job.secrets) channel.addSecret(secret);
    QVariantMap result;
    QUrl url = job.url;
    Protocol::HttpResponse response;
    for (int hop = 0;; ++hop) {
        const bool https = url.scheme() == QLatin1String("https");
        // AGENT-GUARD: credentials follow a redirect only within the feed's
        // site and never onto plain HTTP; a public CDN copy gets none (D-187).
        const bool credentials = !job.authorization.isEmpty()
            && CalDavDiscovery::sameSite(job.url.host(), url.host())
            && Protocol::plaintextAuthAllowed(url.host(), https, job.allowInsecureAuth);
        QMap<QByteArray, QByteArray> headers{{"Accept", "text/calendar, */*;q=0.5"},
                                             {"User-Agent", "QindaTK-IcsCalendarProvider/1"}};
        if (credentials) headers.insert("Authorization", job.authorization);
        if (!job.etag.isEmpty()) headers.insert("If-None-Match", job.etag.toUtf8());
        if (!job.lastModified.isEmpty()) headers.insert("If-Modified-Since", job.lastModified.toUtf8());
        QByteArray target = url.path(QUrl::FullyEncoded).toUtf8();
        if (target.isEmpty()) target = "/";
        if (url.hasQuery()) target += '?' + url.query(QUrl::FullyEncoded).toUtf8();
        const Protocol::Endpoint endpoint{url.host(), static_cast<quint16>(url.port(https ? 443 : 80)),
                                          https ? QStringLiteral("tls") : QStringLiteral("none"),
                                          job.timeoutMs, job.allowInsecureAuth};
        if (!Protocol::httpRequest(channel, endpoint, "GET", target, headers, {}, &response, job.maximumBytes))
            return finished(channel, result);
        static const QList<int> redirects{301, 302, 303, 307, 308};
        if (!redirects.contains(response.status) || !response.headers.contains("location")) break;
        if (hop >= MaximumRedirects) {
            channel.fail(Protocol::Failure::Rejected, QStringLiteral("The feed redirected more than %1 times").arg(MaximumRedirects));
            return finished(channel, result);
        }
        const QUrl next = url.resolved(QUrl(QString::fromUtf8(response.headers.value("location"))));
        if (next.scheme() != QLatin1String("https") && next.scheme() != QLatin1String("http")) {
            channel.fail(Protocol::Failure::Rejected, QStringLiteral("The feed redirected to an unsupported URL"));
            return finished(channel, result);
        }
        url = next;
        channel.note(QStringLiteral("* redirected to ") + url.toString(QUrl::FullyEncoded));
    }
    result.insert(QStringLiteral("status"), response.status);
    result.insert(QStringLiteral("finalUrl"), url.toString(QUrl::FullyEncoded));
    if (response.status == 304) {
        result.insert(QStringLiteral("unchanged"), true);
        return finished(channel, result);
    }
    if (response.status == 401 || response.status == 403) {
        channel.fail(Protocol::Failure::Rejected, QStringLiteral("The feed server refused access (HTTP %1)").arg(response.status));
        return finished(channel, result);
    }
    if (response.status == 429 || response.status == 502 || response.status == 503 || response.status == 504) {
        channel.fail(Protocol::Failure::Offline, QStringLiteral("The feed server is unavailable (HTTP %1)").arg(response.status));
        return finished(channel, result);
    }
    if (response.status != 200) {
        channel.fail(Protocol::Failure::Rejected, QStringLiteral("The feed request failed (HTTP %1)").arg(response.status));
        return finished(channel, result);
    }
    const QVariantMap parsed = parse(response.body, job.defaultZone);
    if (!parsed.value(QStringLiteral("ok")).toBool()) {
        channel.fail(Protocol::Failure::Rejected, parsed.value(QStringLiteral("error")).toString());
        return finished(channel, result);
    }
    result.insert(parsed);
    result.remove(QStringLiteral("ok"));
    result.insert(QStringLiteral("error"), QString());
    result.insert(QStringLiteral("unchanged"), false);
    result.insert(QStringLiteral("feedEtag"), QString::fromUtf8(response.headers.value("etag")));
    result.insert(QStringLiteral("feedLastModified"), QString::fromUtf8(response.headers.value("last-modified")));
    result.insert(QStringLiteral("bytes"), response.body.size());
    return finished(channel, result);
}

QVariantMap readFile(const QString &path, qint64 maximumBytes, const QString &defaultZone)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        return failedResult(QStringLiteral("Cannot open %1: %2").arg(path, file.errorString()));
    if (file.size() > maximumBytes)
        return failedResult(QStringLiteral("%1 is larger than %2 bytes").arg(path).arg(maximumBytes));
    const QByteArray bytes = file.read(maximumBytes + 1);
    if (bytes.size() > maximumBytes)
        return failedResult(QStringLiteral("%1 is larger than %2 bytes").arg(path).arg(maximumBytes));
    QVariantMap result = parse(bytes, defaultZone);
    if (!result.value(QStringLiteral("ok")).toBool()) return failedResult(result.value(QStringLiteral("error")).toString());
    result.insert(QStringLiteral("state"), QStringLiteral("accepted"));
    result.insert(QStringLiteral("retryable"), false);
    result.insert(QStringLiteral("bytes"), bytes.size());
    result.insert(QStringLiteral("transcript"), QStringList{QStringLiteral("* read %1 (%2 bytes)").arg(path).arg(bytes.size())});
    return result;
}

QVariantMap writeFile(const QString &path, const QByteArray &bytes)
{
    // AGENT-NOTE: QSaveFile replaces the target only after a complete write,
    // so a failed export never leaves a truncated calendar behind.
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (path.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return failedResult(QStringLiteral("Cannot write %1: %2").arg(path, file.errorString()));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("state"), QStringLiteral("accepted")},
            {QStringLiteral("error"), QString()}, {QStringLiteral("retryable"), false},
            {QStringLiteral("path"), QFileInfo(path).absoluteFilePath()}, {QStringLiteral("bytes"), bytes.size()},
            {QStringLiteral("transcript"), QStringList{QStringLiteral("* wrote %1 (%2 bytes)").arg(path).arg(bytes.size())}}};
}

} // namespace QindaTK::IcsJobs
