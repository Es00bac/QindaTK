// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "protocol_client.h"

#include <QUrl>
#include <QVariantMap>

// Worker-thread halves of IcsCalendarProvider: a conditional HTTP GET of an
// iCalendar feed (bounded size and redirects), reading an .ics file, and the
// atomic write of an export. Parsing happens here too (ICal::parseCalendar),
// so the GUI thread only merges the mapped items into the store.
// Each item in result.items is {ok, error, component, uid, fields, ics}.
namespace QindaTK::IcsJobs {

constexpr int MaximumRedirects = 5;

struct FeedJob {
    QUrl url;                     // http or https (webcal already mapped to https)
    QByteArray authorization;     // full Authorization header value, or empty
    QStringList secrets;          // redacted from transcripts and errors
    int timeoutMs = 15000;
    bool allowInsecureAuth = false;
    qint64 maximumBytes = 16 * 1024 * 1024;
    QString etag, lastModified;   // validators of the last 200 response
    QString defaultZone;
};

// "webcal:" / "webcals:" -> "https:"; empty QUrl for anything but http(s)
// or a URL carrying user info (credentials belong in setSecret, D-187).
QUrl normalizedFeedUrl(const QString &text, QString *error);
// file: URLs and plain paths -> a local path; empty when neither.
QString localPath(const QString &pathOrUrl);

QVariantMap fetchFeed(const FeedJob &job);
QVariantMap readFile(const QString &path, qint64 maximumBytes, const QString &defaultZone);
QVariantMap writeFile(const QString &path, const QByteArray &bytes);
// ICal::parseCalendar result as a variant map (ok, error, name, items, warnings).
QVariantMap parse(const QByteArray &text, const QString &defaultZone);

} // namespace QindaTK::IcsJobs
