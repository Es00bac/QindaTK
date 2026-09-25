// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "protocol_client.h"

#include <QList>
#include <QVariantMap>

// Worker-thread CalDAV (RFC 4791 subset over WebDAV, RFC 4918) exchanges
// behind CalDavProvider: PROPFIND Depth 1 listing with ETags, GET, and
// conditional PUT/DELETE (If-Match / If-None-Match) so a concurrent remote
// edit is reported as a conflict instead of being overwritten.
namespace QindaTK::CalDavJobs {

struct LocalItem {
    QString collection;           // "events" or "tasks"
    QString component;            // "VEVENT" or "VTODO"
    QString id, uid, href, etag, syncDigest;
    QByteArray remoteIcs;
    bool dirty = false;           // mapped fields differ from the last synced digest
    QVariantMap record;
};

enum class Mode { Sync, Put, Delete };

struct Job {
    Mode mode = Mode::Sync;
    Protocol::Endpoint endpoint;
    QByteArray authorization;     // full Authorization header value, or empty
    QStringList secrets;          // redacted from transcripts and errors
    QString collectionPath;       // percent-encoded, ends with '/'
    QString accountId, defaultZone;
    QList<LocalItem> items;       // Sync: this account's records
    LocalItem target;             // Put/Delete
    QString ifMatch;              // Put/Delete: expected remote ETag; empty with create
    bool create = false;          // Put: If-None-Match: *
};

QString collectionFor(const QString &component);
QVariantMap run(const Job &job);

} // namespace QindaTK::CalDavJobs
