// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// TLS for the in-process test servers (mail_test_servers.h,
// caldav_test_server.h). Every accepted socket is a QSslSocket, so a server
// can speak plain text, start TLS on its own (implicit TLS) or upgrade after
// a STARTTLS command. Certificates come from tests/fixtures/tls.
// AGENT-CONTRACT (tests/fixtures/tls/README.md): server.pem is signed by
// ca.pem for 127.0.0.1/localhost; clients trust it only after trustTestCa().

#include <QFile>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>

#ifndef QINDATK_TLS_FIXTURES
#define QINDATK_TLS_FIXTURES ""
#endif

namespace TestTls {
inline QByteArray fixture(const char *name)
{
    QFile file(QStringLiteral(QINDATK_TLS_FIXTURES "/") + QString::fromLatin1(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Server-side configuration with the fixture certificate and key.
inline QSslConfiguration serverConfiguration()
{
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    configuration.setLocalCertificate(QSslCertificate(fixture("server.pem"), QSsl::Pem));
    configuration.setPrivateKey(QSslKey(fixture("server.key"), QSsl::Rsa, QSsl::Pem));
    configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
    return configuration;
}

// Adds (or removes) the test CA in the process-wide default configuration,
// which every provider worker's QSslSocket starts from.
inline void trustTestCa(bool trusted)
{
    static const QSslConfiguration original = QSslConfiguration::defaultConfiguration();
    QSslConfiguration configuration = original;
    if (trusted) {
        QList<QSslCertificate> roots = configuration.caCertificates();
        roots += QSslCertificate::fromData(fixture("ca.pem"), QSsl::Pem);
        configuration.setCaCertificates(roots);
    }
    QSslConfiguration::setDefaultConfiguration(configuration);
}

inline bool available()
{
    return QSslSocket::supportsSsl() && !fixture("server.pem").isEmpty() && !fixture("server.key").isEmpty();
}

class Server : public QTcpServer {
public:
    bool tls = false;                   // a certificate is configured
    bool implicit = false;              // handshake immediately after accept
protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
        if (tls) socket->setSslConfiguration(serverConfiguration());
        addPendingConnection(socket);
    }
};

inline QSslSocket *ssl(QTcpSocket *socket) { return qobject_cast<QSslSocket *>(socket); }
} // namespace TestTls
