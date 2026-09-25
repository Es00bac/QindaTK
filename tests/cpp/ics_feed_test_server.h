// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// A tiny HTTP/1.1 server for IcsCalendarProvider feeds: one canned route per
// path with an optional ETag / Last-Modified (answered 304 on a matching
// If-None-Match or If-Modified-Since), redirects, chunked bodies and an
// optional required Authorization. Real TCP on 127.0.0.1, optionally TLS.
// AGENT-CONTRACT (ics_calendar_jobs.cpp): each response closes the
// connection, as the client asks with "Connection: close".

#include <QHostAddress>
#include <QMap>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <memory>

#include "tls_test_support.h"

class IcsFeedTestServer {
public:
    struct Route {
        int status = 200;
        QByteArray body, etag, lastModified, location;
        bool chunked = false;
    };
    QMap<QString, Route> routes;           // path (with query) -> response
    QByteArray authorization;              // required value; empty = open feed
    QStringList requests;                  // "GET <path> [If-None-Match: x] [If-Modified-Since: y] [auth]"

    IcsFeedTestServer()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) accept(socket);
        });
    }
    void enableTls() { m_server.tls = true; m_server.implicit = true; }
    quint16 port() const { return m_server.serverPort(); }
    QString url(const QString &path) const
    {
        return QStringLiteral("%1://127.0.0.1:%2%3").arg(m_server.tls ? QStringLiteral("https") : QStringLiteral("http"))
            .arg(port()).arg(path);
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
            respond(socket, QString::fromUtf8(lines.first().trimmed().split(' ').value(1)), headers);
            buffer->clear();
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

    void respond(QTcpSocket *socket, const QString &target, const QMap<QByteArray, QByteArray> &headers)
    {
        const QByteArray ifNoneMatch = headers.value("if-none-match"), ifModified = headers.value("if-modified-since");
        requests.append(QStringLiteral("GET ") + target
                        + (ifNoneMatch.isEmpty() ? QString() : QStringLiteral(" If-None-Match: ") + QString::fromLatin1(ifNoneMatch))
                        + (ifModified.isEmpty() ? QString() : QStringLiteral(" If-Modified-Since: ") + QString::fromLatin1(ifModified))
                        + (headers.contains("authorization") ? QStringLiteral(" [auth]") : QString()));
        QByteArray out;
        const Route route = routes.value(target, {404, {}, {}, {}, {}, false});
        int status = route.status;
        if (!authorization.isEmpty() && headers.value("authorization") != authorization) status = 401;
        else if (!routes.contains(target)) status = 404;
        else if ((!route.etag.isEmpty() && ifNoneMatch == route.etag)
                 || (!route.lastModified.isEmpty() && ifModified == route.lastModified)) status = 304;
        out = "HTTP/1.1 " + QByteArray::number(status) + " Test\r\nConnection: close\r\n";
        if (!route.etag.isEmpty()) out += "ETag: " + route.etag + "\r\n";
        if (!route.lastModified.isEmpty()) out += "Last-Modified: " + route.lastModified + "\r\n";
        if (!route.location.isEmpty()) out += "Location: " + route.location + "\r\n";
        const QByteArray body = status == 200 ? route.body : QByteArray();
        if (status == 304) {
            out += "\r\n";
        } else if (route.chunked && status == 200) {
            out += "Content-Type: text/calendar\r\nTransfer-Encoding: chunked\r\n\r\n";
            for (qsizetype pos = 0; pos < body.size(); pos += 1000) {
                const QByteArray piece = body.mid(pos, 1000);
                out += QByteArray::number(piece.size(), 16) + "\r\n" + piece + "\r\n";
            }
            out += "0\r\n\r\n";
        } else {
            out += "Content-Type: text/calendar\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        }
        socket->write(out);
        socket->flush();
        socket->disconnectFromHost();
    }

    TestTls::Server m_server;
};
