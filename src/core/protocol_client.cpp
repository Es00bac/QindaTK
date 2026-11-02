// SPDX-License-Identifier: LGPL-3.0-or-later
#include "protocol_client.h"

#include <QFutureWatcher>
#include <QHostAddress>
#include <QObject>
#ifndef QT_NO_SSL
#include <QSslSocket>
#else
#include <QTcpSocket>
#endif
#include <QtConcurrent/QtConcurrentRun>

namespace QindaTK::Protocol {
namespace {
constexpr qsizetype MaximumLine = 1 << 20;
constexpr int MaximumTranscript = 400;
#ifndef QT_NO_SSL
bool tlsSupported() { return QSslSocket::supportsSsl(); }
#else
bool tlsSupported() { return false; }
#endif
}

bool isLoopbackHost(const QString &host)
{
    QString bare = host.trimmed();
    if (bare.startsWith(QLatin1Char('[')) && bare.endsWith(QLatin1Char(']')))
        bare = bare.mid(1, bare.size() - 2);
    if (bare.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) return true;
    const QHostAddress address(bare);
    return !address.isNull() && address.isLoopback();
}

bool plaintextAuthAllowed(const QString &host, bool encrypted, bool allowInsecureAuth)
{
    return encrypted || allowInsecureAuth || isLoopbackHost(host);
}

QString redact(QString text, const QStringList &secrets)
{
    for (const QString &secret : secrets) {
        if (secret.isEmpty()) continue;
        text.replace(secret, QStringLiteral("<redacted>"));
        const QString encoded = QString::fromLatin1(secret.toUtf8().toBase64());
        if (encoded.size() >= 4) text.replace(encoded, QStringLiteral("<redacted>"));
    }
    return text;
}

QString failureState(Failure failure)
{
    switch (failure) {
    case Failure::None: return QStringLiteral("accepted");
    case Failure::Offline: return QStringLiteral("offline");
    case Failure::Rejected: break;
    }
    return QStringLiteral("rejected");
}

Channel::Channel() = default;
Channel::~Channel() { close(); }

void Channel::fail(Failure failure, const QString &error)
{
    if (m_failure != Failure::None) return;     // keep the first cause
    m_failure = failure;
    m_error = redact(error, m_secrets);
    note(QStringLiteral("! ") + m_error);
}

void Channel::note(const QString &line)
{
    if (m_transcript.size() < MaximumTranscript) m_transcript.append(redact(line, m_secrets));
}

void Channel::addSecret(const QString &secret)
{
    if (!secret.isEmpty() && !m_secrets.contains(secret)) m_secrets.append(secret);
}

bool Channel::open(const Endpoint &endpoint)
{
    if (m_failure != Failure::None) return false;     // a failed exchange stays failed
    close();
    m_endpoint = endpoint;
    m_buffer.clear();
    if (endpoint.host.trimmed().isEmpty() || endpoint.port == 0) {
        fail(Failure::Rejected, QStringLiteral("No server host and port are configured"));
        return false;
    }
    const bool tls = endpoint.security == QLatin1String("tls");
    if (tls && !tlsSupported()) {
        fail(Failure::Rejected, QStringLiteral("TLS is not available in this Qt build"));
        return false;
    }
    m_socket = std::make_unique<ChannelSocket>();
    note(QStringLiteral("* connect %1:%2 (%3)").arg(endpoint.host).arg(endpoint.port)
             .arg(endpoint.security.isEmpty() ? QStringLiteral("none") : endpoint.security));
    bool ready = false;
    if (tls) {
#ifndef QT_NO_SSL
        m_socket->connectToHostEncrypted(endpoint.host, endpoint.port);
        ready = m_socket->waitForEncrypted(endpoint.timeoutMs);
#endif
    } else {
        m_socket->connectToHost(endpoint.host, endpoint.port);
        ready = m_socket->waitForConnected(endpoint.timeoutMs);
    }
    if (!ready) {
        const bool handshake = m_socket->error() == QAbstractSocket::SslHandshakeFailedError;
        fail(handshake ? Failure::Rejected : Failure::Offline,
             (handshake ? QStringLiteral("TLS handshake failed: ") : QStringLiteral("Cannot reach server: "))
                 + m_socket->errorString());
        return false;
    }
    return true;
}

bool Channel::startTls()
{
    if (!m_socket) return false;
    // AGENT-GUARD: plaintext received before the handshake could be injected
    // by an attacker (STARTTLS command injection); it must never be read as
    // part of the encrypted session.
    if (!m_buffer.isEmpty()) {
        fail(Failure::Rejected, QStringLiteral("Server sent data before TLS started"));
        return false;
    }
    if (!tlsSupported()) {
        fail(Failure::Rejected, QStringLiteral("TLS is not available in this Qt build"));
        return false;
    }
#ifndef QT_NO_SSL
    m_socket->startClientEncryption();
    if (!m_socket->waitForEncrypted(m_endpoint.timeoutMs)) {
        fail(Failure::Rejected, QStringLiteral("TLS handshake failed: ") + m_socket->errorString());
        return false;
    }
#endif
    note(QStringLiteral("* TLS established"));
    return true;
}

#ifndef QT_NO_SSL
bool Channel::encrypted() const { return m_socket && m_socket->isEncrypted(); }
#else
bool Channel::encrypted() const { return false; }
#endif

bool Channel::authAllowed() const
{
    return plaintextAuthAllowed(m_endpoint.host, encrypted(), m_endpoint.allowInsecureAuth);
}

bool Channel::write(const QByteArray &bytes, const QString &shown)
{
    if (!m_socket || m_failure != Failure::None) return false;
    note(QStringLiteral("C: ") + (shown.isNull() ? QString::fromUtf8(bytes).trimmed() : shown));
    if (m_socket->write(bytes) != bytes.size()) {
        fail(Failure::Offline, QStringLiteral("Cannot write to server: ") + m_socket->errorString());
        return false;
    }
    while (m_socket->bytesToWrite() > 0) {
        if (!m_socket->waitForBytesWritten(m_endpoint.timeoutMs)) {
            fail(Failure::Offline, QStringLiteral("Timed out sending to server"));
            return false;
        }
    }
    return true;
}

bool Channel::waitForData()
{
    if (m_socket->bytesAvailable() > 0) {
        m_buffer += m_socket->readAll();
        return true;
    }
    if (!m_socket->waitForReadyRead(m_endpoint.timeoutMs)) {
        const bool closed = m_socket->state() != QAbstractSocket::ConnectedState
            || m_socket->error() == QAbstractSocket::RemoteHostClosedError;
        fail(Failure::Offline, closed ? QStringLiteral("Server closed the connection")
                                      : QStringLiteral("Timed out waiting for server"));
        return false;
    }
    m_buffer += m_socket->readAll();
    return true;
}

bool Channel::readLine(QByteArray *line, bool record)
{
    if (!m_socket || m_failure != Failure::None) return false;
    for (;;) {
        const qsizetype end = m_buffer.indexOf('\n');
        if (end >= 0) {
            *line = m_buffer.left(end);
            m_buffer.remove(0, end + 1);
            if (line->endsWith('\r')) line->chop(1);
            if (record) note(QStringLiteral("S: ") + QString::fromUtf8(*line));
            return true;
        }
        if (m_buffer.size() > MaximumLine) {
            fail(Failure::Rejected, QStringLiteral("Server line exceeds 1 MiB"));
            return false;
        }
        if (!waitForData()) return false;
    }
}

bool Channel::readBytes(qint64 count, QByteArray *out)
{
    if (!m_socket || m_failure != Failure::None || count < 0) return false;
    while (m_buffer.size() < count)
        if (!waitForData()) return false;
    *out = m_buffer.left(count);
    m_buffer.remove(0, count);
    note(QStringLiteral("S: {%1 bytes}").arg(count));
    return true;
}

bool Channel::readToEnd(QByteArray *out, qint64 limit)
{
    if (!m_socket || m_failure != Failure::None) return false;
    for (;;) {
        m_buffer += m_socket->readAll();
        if (m_buffer.size() > limit) {
            fail(Failure::Rejected, QStringLiteral("Server response exceeds %1 bytes").arg(limit));
            return false;
        }
        if (m_socket->state() != QAbstractSocket::ConnectedState) break;
        if (!m_socket->waitForReadyRead(m_endpoint.timeoutMs)) {
            if (m_socket->state() != QAbstractSocket::ConnectedState
                || m_socket->error() == QAbstractSocket::RemoteHostClosedError) {
                m_buffer += m_socket->readAll();
                break;
            }
            fail(Failure::Offline, QStringLiteral("Timed out waiting for server"));
            return false;
        }
    }
    *out = m_buffer;
    m_buffer.clear();
    note(QStringLiteral("S: {%1 bytes}").arg(out->size()));
    return true;
}

void Channel::close()
{
    if (!m_socket) return;
    m_socket->abort();
    m_socket.reset();
}

bool httpRequest(Channel &channel, const Endpoint &endpoint, const QByteArray &method,
                 const QByteArray &target, const QMap<QByteArray, QByteArray> &headers,
                 const QByteArray &body, HttpResponse *response, qint64 maximumBody)
{
    *response = {};
    if (!channel.open(endpoint)) return false;
    QByteArray head = method + ' ' + target + " HTTP/1.1\r\nHost: " + endpoint.host.toUtf8()
        + ':' + QByteArray::number(endpoint.port) + "\r\n";
    bool credentials = false;
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        if (it.key().contains('\n') || it.key().contains('\r') || it.value().contains('\n')
            || it.value().contains('\r')) {
            channel.fail(Failure::Rejected, QStringLiteral("HTTP header contains a line break"));
            return false;
        }
        credentials = credentials || it.key().compare("authorization", Qt::CaseInsensitive) == 0;
        head += it.key() + ": " + it.value() + "\r\n";
    }
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
    const QString shown = QString::fromUtf8(method + ' ' + target)
        + (credentials ? QStringLiteral(" [credentials redacted]") : QString())
        + QStringLiteral(" {%1 body bytes}").arg(body.size());
    if (!channel.write(head + body, shown)) return false;

    QByteArray line;
    do {
        if (!channel.readLine(&line)) return false;
        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() < 2 || !parts.first().startsWith("HTTP/1.")) {
            channel.fail(Failure::Rejected, QStringLiteral("Malformed HTTP status line"));
            return false;
        }
        response->status = parts.at(1).toInt();
        response->headers.clear();
        for (;;) {
            if (!channel.readLine(&line)) return false;
            if (line.isEmpty()) break;
            const qsizetype colon = line.indexOf(':');
            if (colon <= 0) continue;
            const QByteArray name = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            response->headers[name] = response->headers.contains(name)
                ? response->headers.value(name) + ", " + value : value;
        }
    } while (response->status >= 100 && response->status < 200);

    if (method == "HEAD" || response->status == 204 || response->status == 304) return true;
    if (response->headers.value("transfer-encoding").toLower().contains("chunked")) {
        for (;;) {
            if (!channel.readLine(&line)) return false;
            bool ok = false;
            const qint64 size = line.split(';').first().trimmed().toLongLong(&ok, 16);
            if (!ok || size < 0 || response->body.size() + size > maximumBody) {
                channel.fail(Failure::Rejected, QStringLiteral("Malformed or oversized HTTP chunk"));
                return false;
            }
            if (size == 0) {
                do { if (!channel.readLine(&line)) return false; } while (!line.isEmpty());
                break;
            }
            QByteArray chunk;
            if (!channel.readBytes(size + 2, &chunk)) return false;
            response->body += chunk.left(size);
        }
    } else if (response->headers.contains("content-length")) {
        bool ok = false;
        const qint64 size = response->headers.value("content-length").toLongLong(&ok);
        if (!ok || size < 0) {
            channel.fail(Failure::Rejected, QStringLiteral("Invalid HTTP Content-Length"));
            return false;
        }
        if (size > maximumBody) {
            channel.fail(Failure::Rejected, QStringLiteral("Server response exceeds %1 bytes").arg(maximumBody));
            return false;
        }
        if (!channel.readBytes(size, &response->body)) return false;
    } else if (!channel.readToEnd(&response->body, maximumBody)) {
        return false;
    }
    channel.close();
    return true;
}

void startWorker(QObject *owner, std::function<QVariantMap()> work,
                 std::function<void(const QVariantMap &)> done)
{
    auto *watcher = new QFutureWatcher<QVariantMap>(owner);
    QObject::connect(watcher, &QFutureWatcherBase::finished, owner, [watcher, done] {
        const QVariantMap result = watcher->result();
        watcher->deleteLater();
        done(result);
    });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}

} // namespace QindaTK::Protocol
