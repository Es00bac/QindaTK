// SPDX-License-Identifier: LGPL-3.0-or-later
#include "manage_sieve_client.h"
#include "manage_sieve_jobs.h"
namespace QindaTK {
void ManageSieveClient::setSecret(const QString &secret) { m_secret = secret; emit secretChanged(); }
void ManageSieveClient::clearSecret() { m_secret.fill(QChar()); m_secret.clear(); emit secretChanged(); }
bool ManageSieveClient::request(const QString &requestId, const QString &command,
                                const QString &name, const QByteArray &script, const QString &newName)
{
    const SieveJobs::Job job{{m_host, quint16(m_port), m_security, m_timeoutMs, m_allowInsecureAuth},
        {m_userName, m_secret, QStringLiteral("plain")}, command.toLower(), name, newName, script};
    const QString error = SieveJobs::validate(job);
    if (!error.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, requestId, error] {
            emit finished(requestId, {{QStringLiteral("ok"), false}, {QStringLiteral("state"), QStringLiteral("rejected")},
                                       {QStringLiteral("error"), error}});
        }, Qt::QueuedConnection);
        return false;
    }
    ++m_pending; emit stateChanged();
    Protocol::startWorker(this, [job] { return SieveJobs::run(job); },
        [this, requestId](const QVariantMap &result) {
            --m_pending; emit stateChanged(); emit finished(requestId, result);
        });
    return true;
}
}
