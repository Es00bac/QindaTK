// SPDX-License-Identifier: LGPL-3.0-or-later
#include "caldav_discovery.h"

#include "caldav_provider.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace QindaTK::CalDavDiscovery {
namespace {
const QByteArray kPropfind =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:a=\"http://apple.com/ns/ical/\">"
    "<d:prop><d:resourcetype/><d:displayname/><d:current-user-principal/><d:current-user-privilege-set/>"
    "<c:calendar-home-set/><c:calendar-description/><c:supported-calendar-component-set/><a:calendar-color/>"
    "</d:prop></d:propfind>";

QString readHref(QXmlStreamReader &r)
{
    QString href;
    while (r.readNextStartElement()) {
        if (r.name() == u"href" && href.isEmpty()) href = r.readElementText().trimmed();
        else r.skipCurrentElement();
    }
    return href;
}

void readProps(QXmlStreamReader &r, Resource *res)
{
    while (r.readNextStartElement()) {
        const QString name = r.name().toString();
        if (name == QLatin1String("resourcetype")) {
            while (r.readNextStartElement()) { res->types.append(r.name().toString()); r.skipCurrentElement(); }
        } else if (name == QLatin1String("current-user-principal")) {
            res->principal = readHref(r);
        } else if (name == QLatin1String("calendar-home-set")) {
            res->homeSet = readHref(r);
        } else if (name == QLatin1String("displayname")) {
            res->displayName = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
        } else if (name == QLatin1String("calendar-description")) {
            res->description = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
        } else if (name == QLatin1String("calendar-color")) {
            res->color = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
        } else if (name == QLatin1String("supported-calendar-component-set")) {
            res->componentsKnown = true;
            while (r.readNextStartElement()) {
                if (r.name() == u"comp") res->components.append(r.attributes().value(QLatin1String("name")).toString().toUpper());
                r.skipCurrentElement();
            }
        } else if (name == QLatin1String("current-user-privilege-set")) {
            res->privilegesKnown = true;
            while (r.readNextStartElement()) {
                if (r.name() == u"privilege")
                    while (r.readNextStartElement()) { res->privileges.append(r.name().toString()); r.skipCurrentElement(); }
                else
                    r.skipCurrentElement();
            }
        } else {
            r.skipCurrentElement();
        }
    }
}

void merge(Resource *into, const Resource &from)
{
    into->types += from.types;
    if (!from.principal.isEmpty()) into->principal = from.principal;
    if (!from.homeSet.isEmpty()) into->homeSet = from.homeSet;
    if (!from.displayName.isEmpty()) into->displayName = from.displayName;
    if (!from.color.isEmpty()) into->color = from.color;
    if (!from.description.isEmpty()) into->description = from.description;
    into->components += from.components;
    into->privileges += from.privileges;
    into->componentsKnown = into->componentsKnown || from.componentsKnown;
    into->privilegesKnown = into->privilegesKnown || from.privilegesKnown;
}

QString pathKey(const QUrl &url)
{
    QString key = url.path(QUrl::FullyDecoded);
    while (key.endsWith(QLatin1Char('/'))) key.chop(1);
    return key;
}

QString text(const QUrl &url) { return url.toString(QUrl::FullyEncoded); }

class Discovery {
public:
    explicit Discovery(const Job &job) : m_job(job)
    {
        for (const QString &secret : job.secrets) m_channel.addSecret(secret);
    }

    // PROPFIND at *url (updated to the final location after redirects).
    // Returns false on a failure recorded on the channel; *status is the
    // final HTTP status otherwise.
    bool propfind(QUrl *url, int depth, QList<Resource> *out, int *status)
    {
        for (int hop = 0;; ++hop) {
            if (!allowed(*url)) return false;
            const bool https = url->scheme() == QLatin1String("https");
            const Protocol::Endpoint endpoint{url->host(), static_cast<quint16>(url->port(https ? 443 : 80)),
                                              https ? QStringLiteral("tls") : QStringLiteral("none"),
                                              m_job.timeoutMs, m_job.allowInsecureAuth};
            QMap<QByteArray, QByteArray> headers{{"Depth", QByteArray::number(depth)},
                                                 {"Content-Type", "application/xml; charset=utf-8"}};
            if (!m_job.authorization.isEmpty()) headers.insert("Authorization", m_job.authorization);
            QByteArray target = url->path(QUrl::FullyEncoded).toUtf8();
            if (target.isEmpty()) target = "/";
            if (url->hasQuery()) target += '?' + url->query(QUrl::FullyEncoded).toUtf8();
            Protocol::HttpResponse response;
            if (!Protocol::httpRequest(m_channel, endpoint, "PROPFIND", target, headers, kPropfind, &response))
                return false;
            *status = response.status;
            if (response.status == 401 || response.status == 403) {
                m_channel.fail(Protocol::Failure::Rejected,
                               QStringLiteral("The calendar server refused the credentials (HTTP %1)").arg(response.status));
                return false;
            }
            static const QList<int> redirects{301, 302, 303, 307, 308};
            if (redirects.contains(response.status) && response.headers.contains("location")) {
                if (hop >= MaximumRedirects) {
                    m_channel.fail(Protocol::Failure::Rejected, QStringLiteral("Too many redirects during calendar discovery"));
                    return false;
                }
                *url = url->resolved(QUrl(QString::fromUtf8(response.headers.value("location"))));
                m_channel.note(QStringLiteral("* redirected to ") + text(*url));
                continue;
            }
            if (response.status != 207) return true;
            QString error;
            *out = parseMultistatus(response.body, &error);
            if (!error.isEmpty()) {
                m_channel.fail(Protocol::Failure::Rejected, QStringLiteral("Malformed WebDAV reply: ") + error);
                return false;
            }
            return true;
        }
    }

    QVariantMap discover()
    {
        QUrl url = m_job.start;
        if (url.path().isEmpty()) url.setPath(QStringLiteral("/"));
        QList<Resource> found;
        int status = 0;
        if (!propfind(&url, 0, &found, &status)) return finish();
        Resource self = selfOf(found, url);
        const bool nothing = status != 207 || (self.principal.isEmpty() && self.homeSet.isEmpty()
                                               && !self.types.contains(QLatin1String("calendar")));
        if (nothing && url.path() != QLatin1String("/.well-known/caldav")) {
            // RFC 6764 §5: the well-known URI on the same origin names the context path.
            QUrl known = m_job.start;
            known.setPath(QStringLiteral("/.well-known/caldav"));
            known.setQuery(QString());
            QList<Resource> viaKnown;
            int knownStatus = 0;
            if (!propfind(&known, 0, &viaKnown, &knownStatus)) return finish();
            if (knownStatus == 207) {
                url = known;
                status = knownStatus;
                self = selfOf(viaKnown, known);
            }
        }
        if (status != 207) {
            m_channel.fail(Protocol::Failure::Rejected,
                           QStringLiteral("No CalDAV service answered at %1 (HTTP %2)").arg(text(url)).arg(status));
            return finish();
        }
        if (!self.principal.isEmpty()) m_principal = url.resolved(QUrl(self.principal));
        if (self.types.contains(QLatin1String("calendar"))) {
            addCalendar(self, url);                 // the URL is a calendar collection
            return finish();
        }
        QUrl home = self.homeSet.isEmpty() ? QUrl() : url.resolved(QUrl(self.homeSet));
        if (home.isEmpty() && m_principal.isValid() && pathKey(m_principal) != pathKey(url)) {
            QUrl principal = m_principal;
            QList<Resource> props;
            if (!propfind(&principal, 0, &props, &status)) return finish();
            const Resource p = selfOf(props, principal);
            if (status == 207 && !p.homeSet.isEmpty()) home = principal.resolved(QUrl(p.homeSet));
        }
        // A plain collection without principal data may itself be the home set.
        if (home.isEmpty() && self.types.contains(QLatin1String("collection"))) home = url;
        if (home.isEmpty()) {
            m_channel.fail(Protocol::Failure::Rejected, QStringLiteral("The server did not name a calendar home set"));
            return finish();
        }
        QList<Resource> members;
        if (!propfind(&home, 1, &members, &status)) return finish();
        if (status != 207) {
            m_channel.fail(Protocol::Failure::Rejected,
                           QStringLiteral("Listing the calendar home set failed (HTTP %1)").arg(status));
            return finish();
        }
        m_home = home;
        for (const Resource &member : std::as_const(members)) {
            const QUrl memberUrl = home.resolved(QUrl(member.href));
            if (pathKey(memberUrl) == pathKey(home) || !member.types.contains(QLatin1String("calendar"))) continue;
            if (m_calendars.size() >= MaximumCalendars) break;
            addCalendar(member, memberUrl);
        }
        return finish();
    }

private:
    bool allowed(const QUrl &url)
    {
        const bool https = url.scheme() == QLatin1String("https");
        if (!url.isValid() || url.host().isEmpty() || (!https && url.scheme() != QLatin1String("http"))) {
            m_channel.fail(Protocol::Failure::Rejected, QStringLiteral("Discovery needs an https URL"));
            return false;
        }
        if (!Protocol::plaintextAuthAllowed(url.host(), https, m_job.allowInsecureAuth)) {
            m_channel.fail(Protocol::Failure::Rejected,
                           QStringLiteral("Refusing calendar discovery over plain HTTP (%1)").arg(url.host()));
            return false;
        }
        if (!m_job.authorization.isEmpty() && !sameSite(m_job.start.host(), url.host())) {
            m_channel.fail(Protocol::Failure::Rejected,
                           QStringLiteral("Discovery was sent to another site (%1); credentials were not sent").arg(url.host()));
            return false;
        }
        return true;
    }

    static Resource selfOf(const QList<Resource> &found, const QUrl &url)
    {
        for (const Resource &res : found)
            if (pathKey(url.resolved(QUrl(res.href))) == pathKey(url)) return res;
        return found.isEmpty() ? Resource() : found.first();
    }

    void addCalendar(const Resource &res, QUrl url)
    {
        if (!url.path().endsWith(QLatin1Char('/'))) url.setPath(url.path() + QLatin1Char('/'));
        QString name = res.displayName;
        if (name.isEmpty()) name = pathKey(url).section(QLatin1Char('/'), -1);
        const bool writable = res.privileges.contains(QLatin1String("write"))
            || res.privileges.contains(QLatin1String("write-content")) || res.privileges.contains(QLatin1String("all"));
        m_calendars.append(QVariantMap{
            {QStringLiteral("url"), text(url)}, {QStringLiteral("displayName"), name},
            {QStringLiteral("color"), normalizedColor(res.color)}, {QStringLiteral("description"), res.description},
            {QStringLiteral("components"), res.componentsKnown ? res.components
                                                               : QStringList{QStringLiteral("VEVENT"), QStringLiteral("VTODO")}},
            {QStringLiteral("readOnly"), res.privilegesKnown && !writable}});
    }

    QVariantMap finish()
    {
        const Protocol::Failure failure = m_channel.failure();
        const bool ok = failure == Protocol::Failure::None;
        return {{QStringLiteral("ok"), ok}, {QStringLiteral("state"), Protocol::failureState(failure)},
                {QStringLiteral("error"), ok ? QString() : Protocol::redact(m_channel.error(), m_job.secrets)},
                {QStringLiteral("retryable"), failure == Protocol::Failure::Offline},
                {QStringLiteral("url"), text(m_job.start)},
                {QStringLiteral("principalUrl"), m_principal.isValid() ? text(m_principal) : QString()},
                {QStringLiteral("homeSetUrl"), m_home.isValid() ? text(m_home) : QString()},
                {QStringLiteral("calendars"), ok ? m_calendars : QVariantList()},
                {QStringLiteral("transcript"), m_channel.transcript()}};
    }

    const Job &m_job;
    Protocol::Channel m_channel;
    QUrl m_principal, m_home;
    QVariantList m_calendars;
};
} // namespace

QList<Resource> parseMultistatus(const QByteArray &xml, QString *error)
{
    QXmlStreamReader r(xml);
    QList<Resource> out;
    Resource current, pending;
    QString status;
    bool inResponse = false, inPropstat = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const QStringView name = r.name();
            if (name == u"response") { current = {}; inResponse = true; }
            else if (name == u"href" && inResponse && !inPropstat) current.href = r.readElementText().trimmed();
            else if (name == u"propstat") { inPropstat = true; pending = {}; status.clear(); }
            else if (name == u"status" && inPropstat) status = r.readElementText();
            else if (name == u"prop" && inPropstat) readProps(r, &pending);
        } else if (r.isEndElement()) {
            const QStringView name = r.name();
            if (name == u"propstat") {
                inPropstat = false;
                if (status.contains(QLatin1String(" 200"))) merge(&current, pending);
            } else if (name == u"response" && inResponse) {
                if (!current.href.isEmpty()) out.append(current);
                inResponse = false;
            }
        }
    }
    if (r.hasError()) *error = r.errorString();
    return out;
}

QString normalizedColor(const QString &color)
{
    static const QRegularExpression hex(QStringLiteral("^#([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})?$"));
    const QRegularExpressionMatch match = hex.match(color.trimmed());
    return match.hasMatch() ? QLatin1Char('#') + match.captured(1).toLower() : QString();
}

bool sameSite(const QString &origin, const QString &target)
{
    if (origin.compare(target, Qt::CaseInsensitive) == 0) return true;
    if (Protocol::isLoopbackHost(origin) && Protocol::isLoopbackHost(target)) return true;
    if (!QHostAddress(origin).isNull() || !QHostAddress(target).isNull()) return false;
    const QStringList labels = origin.toLower().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    // AGENT-NOTE: without a public-suffix list the parent of a three-label
    // host is trusted as the site (icloud.com); a two-label host trusts only
    // its own subdomains. A three-label registrable domain (a.co.uk) is the
    // known gap and would need the PSL to close.
    const QString parent = labels.size() >= 3 ? labels.mid(1).join(QLatin1Char('.')) : labels.join(QLatin1Char('.'));
    const QString host = target.toLower();
    return !parent.isEmpty() && (host == parent || host.endsWith(QLatin1Char('.') + parent));
}

QVariantMap run(const Job &job)
{
    Discovery discovery(job);
    return discovery.discover();
}

} // namespace QindaTK::CalDavDiscovery

namespace QindaTK {

bool CalDavProvider::discoverCalendars(const QString &requestId, const QString &url)
{
    CalDavDiscovery::Job job;
    job.start = QUrl(url.trimmed().isEmpty() ? m_collectionUrl : url.trimmed());
    job.timeoutMs = m_timeoutMs;
    job.allowInsecureAuth = m_allowInsecureAuth;
    if (!m_secret.isEmpty()) {
        const QByteArray basic = (m_userName + QLatin1Char(':') + m_secret).toUtf8().toBase64();
        job.authorization = m_mechanism == QLatin1String("bearer") ? "Bearer " + m_secret.toUtf8() : "Basic " + basic;
        job.secrets = {m_secret, QString::fromLatin1(basic)};
    }
    ++m_pending;
    m_state = QStringLiteral("working");
    emit stateChanged();
    auto done = [this, requestId](const QVariantMap &result) {
        QVariantMap completed = result;
        completed.insert(QStringLiteral("requestId"), requestId);
        completed.insert(QStringLiteral("error"), Protocol::redact(result.value(QStringLiteral("error")).toString(), {m_secret}));
        m_pending = qMax(0, m_pending - 1);
        m_state = completed.value(QStringLiteral("state")).toString();
        m_lastError = completed.value(QStringLiteral("error")).toString();
        emit stateChanged();
        emit discoveryFinished(requestId, completed);
    };
    const QUrl start = job.start;
    if (!start.isValid() || start.host().isEmpty()
        || (start.scheme() != QLatin1String("https") && start.scheme() != QLatin1String("http"))) {
        const QVariantMap rejected{{QStringLiteral("ok"), false}, {QStringLiteral("state"), QStringLiteral("rejected")},
                                   {QStringLiteral("error"), QStringLiteral("Calendar discovery needs an https URL")},
                                   {QStringLiteral("retryable"), false}, {QStringLiteral("calendars"), QVariantList()}};
        QMetaObject::invokeMethod(this, [done, rejected] { done(rejected); }, Qt::QueuedConnection);
        return false;
    }
    Protocol::startWorker(this, [job] { return CalDavDiscovery::run(job); }, done);
    return true;
}

} // namespace QindaTK
