// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <memory>

class QObject;
// AGENT-NOTE (D-354): Qt for Android here is built without SSL, where
// QSslSocket is only a forward declaration. The channel then rides on a plain
// QTcpSocket and refuses "tls"/"starttls" with Failure::Rejected.
#include <QtNetwork/qtnetworkglobal.h>
#ifndef QT_NO_SSL
class QSslSocket;
using ChannelSocket = QSslSocket;
#else
class QTcpSocket;
using ChannelSocket = QTcpSocket;
#endif

// Blocking line/byte channel shared by the SMTP, IMAP and CalDAV providers.
// AGENT-NOTE: providers run one protocol exchange per worker-thread job
// (startWorker) with a blocking socket, so the exchange reads as the RFC
// dialogue it implements; the QML-facing provider objects stay on the GUI
// thread and only receive the finished result map (docs/productivity-providers.md).
namespace QindaTK::Protocol {

struct Endpoint {
    QString host;
    quint16 port = 0;
    QString security;          // "tls" (implicit), "starttls", "none"
    int timeoutMs = 15000;
    bool allowInsecureAuth = false;
};

struct Credentials {
    QString user;
    QString secret;
    QString mechanism;         // provider specific: auto, plain, login, xoauth2, basic, bearer
    bool isSet() const { return !secret.isEmpty(); }
};

// Failure classes map to the acknowledgment states the controls accept.
enum class Failure { None, Offline, Rejected };

bool isLoopbackHost(const QString &host);
// AGENT-GUARD: credentials are only sent over TLS, to a loopback test
// endpoint, or when the host explicitly allows insecure auth. Removing this
// check sends passwords in clear text over the network.
bool plaintextAuthAllowed(const QString &host, bool encrypted, bool allowInsecureAuth);
// Replaces every secret (and its base64 forms) with "<redacted>".
QString redact(QString text, const QStringList &secrets);
QString failureState(Failure failure);

class Channel {
public:
    Channel();
    ~Channel();
    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;

    bool open(const Endpoint &endpoint);
    bool startTls();
    // `shown` replaces the bytes in the transcript (credentials, bodies).
    bool write(const QByteArray &bytes, const QString &shown = {});
    // `record` false keeps the line out of the transcript (POP3 multi-line
    // bodies, which are summarized as a byte count by the caller).
    bool readLine(QByteArray *line, bool record = true);
    bool readBytes(qint64 count, QByteArray *out);
    bool readToEnd(QByteArray *out, qint64 limit);
    void close();

    bool encrypted() const;
    bool authAllowed() const;
    void addSecret(const QString &secret);
    void fail(Failure failure, const QString &error);
    Failure failure() const { return m_failure; }
    QString error() const { return m_error; }
    QStringList transcript() const { return m_transcript; }
    void note(const QString &line);

private:
    bool waitForData();
    std::unique_ptr<ChannelSocket> m_socket;
    Endpoint m_endpoint;
    QByteArray m_buffer;
    QStringList m_transcript, m_secrets;
    QString m_error;
    Failure m_failure = Failure::None;
};

struct HttpResponse {
    int status = 0;
    QMap<QByteArray, QByteArray> headers;   // lower-case names
    QByteArray body;
};

// One HTTP/1.1 request on a fresh connection (Connection: close). Handles
// Content-Length, chunked and read-to-close bodies up to `maximumBody`
// (16 MiB by default); a larger body fails the request as rejected.
bool httpRequest(Channel &channel, const Endpoint &endpoint, const QByteArray &method,
                 const QByteArray &target, const QMap<QByteArray, QByteArray> &headers,
                 const QByteArray &body, HttpResponse *response, qint64 maximumBody = 16 * 1024 * 1024);

// Runs `work` on the global thread pool and calls `done` on `owner`'s thread.
// `work` must not touch `owner`; if `owner` is destroyed first `done` is dropped.
void startWorker(QObject *owner, std::function<QVariantMap()> work,
                 std::function<void(const QVariantMap &)> done);

} // namespace QindaTK::Protocol
