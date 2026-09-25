// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "protocol_client.h"

#include <QList>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

// Worker-thread CalDAV calendar discovery behind
// CalDavProvider::discoverCalendars: RFC 6764 (/.well-known/caldav and its
// redirect), RFC 5397 current-user-principal, RFC 4791 calendar-home-set and
// a PROPFIND Depth 1 of the home set. The start URL may be a server root, a
// principal, a home set or a calendar collection.
// AGENT-GUARD: credentials only travel over https (or to loopback, or with
// allowInsecureAuth) and only to the start host's site (sameSite); a
// redirect or href elsewhere fails the request instead of forwarding them.
namespace QindaTK::CalDavDiscovery {

constexpr int MaximumRedirects = 5;
constexpr int MaximumCalendars = 500;

struct Job {
    QUrl start;
    QByteArray authorization;     // full Authorization header value, or empty
    QStringList secrets;          // redacted from transcripts and errors
    int timeoutMs = 15000;
    bool allowInsecureAuth = false;
};

// One multistatus <response> with the discovery properties of its 200 propstat.
struct Resource {
    QString href;
    QStringList types;            // resourcetype children (local names)
    QString principal, homeSet, displayName, color, description;
    QStringList components, privileges;
    bool componentsKnown = false, privilegesKnown = false;
};

QList<Resource> parseMultistatus(const QByteArray &xml, QString *error);
// "#RRGGBB" / "#RRGGBBAA" (Apple calendar-color) -> "#rrggbb"; "" if invalid.
QString normalizedColor(const QString &color);
// True when credentials for `origin` may be sent to `target`: the same host,
// or a host under the origin's parent domain (caldav.icloud.com ->
// p42-caldav.icloud.com); both loopback also counts.
bool sameSite(const QString &origin, const QString &target);

QVariantMap run(const Job &job);

} // namespace QindaTK::CalDavDiscovery
