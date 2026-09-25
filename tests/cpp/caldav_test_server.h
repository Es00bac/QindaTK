// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// In-process CalDAV collection for tst_caldav_provider: HTTP/1.1 over real TCP
// on 127.0.0.1, answering PROPFIND Depth 1 (207 multistatus with getetag),
// GET, and conditional PUT/DELETE (If-Match / If-None-Match: *, 412 on a
// stale ETag). Each response closes the connection, as the client requests.
// AGENT-CONTRACT (caldav_jobs.cpp): ETags are opaque quoted strings and every
// accepted write returns a new one in the ETag header.
// Discovery (caldav_discovery.cpp): with `principal` set, PROPFIND for the
// discovery properties answers at the context path (current-user-principal),
// the principal (calendar-home-set), the home set (Depth 1: its calendars and
// one non-calendar child) and each calendar; /.well-known/caldav redirects to
// the context path when `wellKnown` is set (RFC 6764).

#include <QHostAddress>
#include <QMap>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <memory>

#include "tls_test_support.h"

class CalDavTestServer {
public:
    struct Resource {
        QByteArray ics;
        int etag = 0;
    };
    QString collection = QStringLiteral("/dav/calendars/ada/work/");
    QByteArray authorization;              // required Authorization value; empty = none
    QMap<QString, Resource> resources;     // path -> resource
    QStringList requests;                  // "METHOD path [If-Match: x] [If-None-Match: y]"
    bool chunked = false;                  // GET bodies use chunked transfer coding
    struct Calendar {
        QString path, displayName, color, description;
        QStringList components{QStringLiteral("VEVENT"), QStringLiteral("VTODO")};
        bool readOnly = false;
    };
    QString principal;                     // e.g. "/dav/principals/ada/"; empty = no discovery
    QString homeSet;                       // e.g. "/dav/calendars/ada/"
    QString contextPath = QStringLiteral("/dav/");
    bool absoluteHomeSet = false;          // calendar-home-set as an absolute URL
    bool wellKnown = false;                // /.well-known/caldav -> 301 contextPath
    QByteArray wellKnownTarget;            // overrides the Location of that redirect
    QList<Calendar> calendars;             // members of homeSet

    CalDavTestServer()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) accept(socket);
        });
    }
    quint16 port() const { return m_server.serverPort(); }
    void enableTls() { m_server.tls = true; m_server.implicit = true; }
    QString url() const { return root() + collection; }
    QString root() const
    {
        return QStringLiteral("%1://127.0.0.1:%2").arg(m_server.tls ? QStringLiteral("https") : QStringLiteral("http")).arg(port());
    }
    QString path(const QString &name) const { return collection + name; }
    QByteArray etagOf(const QString &path) const
    {
        return resources.contains(path) ? "\"e" + QByteArray::number(resources.value(path).etag) + "\"" : QByteArray();
    }
    void put(const QString &path, const QByteArray &ics) { resources.insert(path, {ics, m_nextEtag++}); }
    QByteArray body(const QString &path) const { return resources.value(path).ics; }
    int count(const QString &prefix) const
    {
        int n = 0;
        for (const QString &request : requests) n += request.startsWith(prefix);
        return n;
    }

private:
    void accept(QTcpSocket *socket)
    {
        if (m_server.implicit)
            if (QSslSocket *secure = TestTls::ssl(socket)) secure->startServerEncryption();
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            *buffer += socket->readAll();
            const qsizetype end = buffer->indexOf("\r\n\r\n");
            if (end < 0) return;
            QMap<QByteArray, QByteArray> headers;
            const QList<QByteArray> lines = buffer->left(end).split('\n');
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const QByteArray line = lines.at(i).trimmed();
                const qsizetype colon = line.indexOf(':');
                if (colon > 0) headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
            }
            const qsizetype length = headers.value("content-length").toLongLong();
            if (buffer->size() < end + 4 + length) return;
            const QList<QByteArray> request = lines.first().trimmed().split(' ');
            respond(socket, request.value(0), QString::fromUtf8(request.value(1)), headers, buffer->mid(end + 4, length));
            buffer->clear();
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

    void reply(QTcpSocket *socket, int status, const QByteArray &body = {},
               const QByteArray &etag = {}, const QByteArray &type = {}, bool chunk = false)
    {
        QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " Test\r\nConnection: close\r\n";
        if (!etag.isEmpty()) out += "ETag: " + etag + "\r\n";
        if (!type.isEmpty()) out += "Content-Type: " + type + "\r\n";
        if (chunk) {
            const qsizetype half = body.size() / 2;
            out += "Transfer-Encoding: chunked\r\n\r\n";
            for (const QByteArray &piece : {body.left(half), body.mid(half)})
                if (!piece.isEmpty()) out += QByteArray::number(piece.size(), 16) + "\r\n" + piece + "\r\n";
            out += "0\r\n\r\n";
        } else {
            out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        }
        socket->write(out);
        socket->flush();
        socket->disconnectFromHost();
    }

    void respond(QTcpSocket *socket, const QByteArray &method, const QString &target,
                 const QMap<QByteArray, QByteArray> &headers, const QByteArray &body)
    {
        const QByteArray ifMatch = headers.value("if-match"), ifNoneMatch = headers.value("if-none-match");
        requests.append(QString::fromLatin1(method) + QLatin1Char(' ') + target
                        + (ifMatch.isEmpty() ? QString() : QStringLiteral(" If-Match: ") + QString::fromLatin1(ifMatch))
                        + (ifNoneMatch.isEmpty() ? QString() : QStringLiteral(" If-None-Match: ") + QString::fromLatin1(ifNoneMatch)));
        if (!authorization.isEmpty() && headers.value("authorization") != authorization) return reply(socket, 401);
        const bool exists = resources.contains(target);
        if (target == QLatin1String("/.well-known/caldav")) {
            if (!wellKnown) return reply(socket, 404);
            return redirect(socket, wellKnownTarget.isEmpty() ? contextPath.toUtf8() : wellKnownTarget);
        }
        if (method == "PROPFIND" && body.contains("current-user-principal"))
            return discovery(socket, target, headers.value("depth"));
        if (method == "PROPFIND") {
            if (target != collection || headers.value("depth") != "1") return reply(socket, 400);
            QByteArray xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<d:multistatus xmlns:d=\"DAV:\">"
                             "<d:response><d:href>" + collection.toUtf8() + "</d:href><d:propstat><d:prop><d:getetag/>"
                             "</d:prop><d:status>HTTP/1.1 404 Not Found</d:status></d:propstat></d:response>";
            for (auto it = resources.cbegin(); it != resources.cend(); ++it)
                xml += "<d:response><d:href>" + it.key().toUtf8() + "</d:href><d:propstat><d:prop><d:getetag>"
                       + etagOf(it.key()) + "</d:getetag></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>";
            return reply(socket, 207, xml + "</d:multistatus>", {}, "application/xml; charset=utf-8");
        }
        if (method == "GET")
            return exists ? reply(socket, 200, resources.value(target).ics, etagOf(target), "text/calendar", chunked) : reply(socket, 404);
        if (method == "PUT") {
            if ((ifNoneMatch == "*" && exists) || (!ifMatch.isEmpty() && (!exists || etagOf(target) != ifMatch)))
                return reply(socket, 412);
            put(target, body);
            return reply(socket, exists ? 204 : 201, {}, etagOf(target));
        }
        if (method == "DELETE") {
            if (!exists) return reply(socket, 404);
            if (!ifMatch.isEmpty() && etagOf(target) != ifMatch) return reply(socket, 412);
            resources.remove(target);
            return reply(socket, 204);
        }
        reply(socket, 405);
    }

    void redirect(QTcpSocket *socket, const QByteArray &location)
    {
        socket->write("HTTP/1.1 301 Moved\r\nConnection: close\r\nLocation: " + location + "\r\nContent-Length: 0\r\n\r\n");
        socket->flush();
        socket->disconnectFromHost();
    }

    static QByteArray response(const QString &href, const QByteArray &props, const QByteArray &missing = {})
    {
        QByteArray out = "<d:response><d:href>" + href.toUtf8() + "</d:href><d:propstat><d:prop>" + props
            + "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>";
        if (!missing.isEmpty())
            out += "<d:propstat><d:prop>" + missing + "</d:prop><d:status>HTTP/1.1 404 Not Found</d:status></d:propstat>";
        return out + "</d:response>";
    }

    QByteArray calendarProps(const Calendar &calendar) const
    {
        QByteArray comps;
        for (const QString &name : calendar.components) comps += "<c:comp name=\"" + name.toUtf8() + "\"/>";
        QByteArray privileges = "<d:privilege><d:read/></d:privilege>";
        if (!calendar.readOnly) privileges += "<d:privilege><d:write-content/></d:privilege>";
        QByteArray props = "<d:resourcetype><d:collection/><c:calendar/></d:resourcetype>"
                           "<d:current-user-principal><d:href>" + principal.toUtf8() + "</d:href></d:current-user-principal>"
                           "<c:supported-calendar-component-set>" + comps + "</c:supported-calendar-component-set>"
                           "<d:current-user-privilege-set>" + privileges + "</d:current-user-privilege-set>";
        if (!calendar.displayName.isEmpty()) props += "<d:displayname>" + calendar.displayName.toUtf8() + "</d:displayname>";
        if (!calendar.color.isEmpty()) props += "<a:calendar-color>" + calendar.color.toUtf8() + "</a:calendar-color>";
        if (!calendar.description.isEmpty())
            props += "<c:calendar-description>" + calendar.description.toUtf8() + "</c:calendar-description>";
        return props;
    }

    void discovery(QTcpSocket *socket, const QString &target, const QByteArray &depth)
    {
        if (principal.isEmpty()) return reply(socket, 404);
        const QByteArray me = "<d:current-user-principal><d:href>" + principal.toUtf8() + "</d:href></d:current-user-principal>";
        QByteArray xml;
        if (target == contextPath) {
            xml = response(target, "<d:resourcetype><d:collection/></d:resourcetype>" + me, "<c:calendar-home-set/>");
        } else if (target == principal) {
            const QString home = absoluteHomeSet ? root() + homeSet : homeSet;
            xml = response(target, "<d:resourcetype><d:principal/><d:collection/></d:resourcetype>" + me
                                       + "<c:calendar-home-set><d:href>" + home.toUtf8() + "</d:href></c:calendar-home-set>");
        } else if (target == homeSet) {
            xml = response(target, "<d:resourcetype><d:collection/></d:resourcetype>" + me);
            if (depth == "1") {
                xml += response(homeSet + QStringLiteral("inbox/"),
                                "<d:resourcetype><d:collection/><c:schedule-inbox/></d:resourcetype>");
                for (const Calendar &calendar : std::as_const(calendars))
                    xml += response(calendar.path, calendarProps(calendar), "<d:getctag/>");
            }
        } else {
            for (const Calendar &calendar : std::as_const(calendars))
                if (calendar.path == target) xml = response(target, calendarProps(calendar));
            if (xml.isEmpty()) return reply(socket, 404);
        }
        reply(socket, 207, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<d:multistatus xmlns:d=\"DAV:\" "
                           "xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:a=\"http://apple.com/ns/ical/\">"
                               + xml + "</d:multistatus>", {}, "application/xml; charset=utf-8");
    }

    TestTls::Server m_server;
    int m_nextEtag = 1;
};
