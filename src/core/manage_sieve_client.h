// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// RFC 5804 client. One worker connection per request; no socket or credential
// access from QML. Completion arrives on the object's thread, correlated by ID.
class ManageSieveClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString host MEMBER m_host NOTIFY configChanged)
    Q_PROPERTY(int port MEMBER m_port NOTIFY configChanged)
    Q_PROPERTY(QString security MEMBER m_security NOTIFY configChanged)
    Q_PROPERTY(QString userName MEMBER m_userName NOTIFY configChanged)
    Q_PROPERTY(bool allowInsecureAuth MEMBER m_allowInsecureAuth NOTIFY configChanged)
    Q_PROPERTY(int timeoutMs MEMBER m_timeoutMs NOTIFY configChanged)
    Q_PROPERTY(bool hasSecret READ hasSecret NOTIFY secretChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
public:
    using QObject::QObject;
    bool hasSecret() const { return !m_secret.isEmpty(); }
    bool busy() const { return m_pending > 0; }
    Q_INVOKABLE void setSecret(const QString &secret);
    Q_INVOKABLE void clearSecret();
    // command: capabilities/list/get/put/check/activate/delete/rename.
    // activate with an empty name disables the active script. put/check send
    // script bytes as a non-synchronizing literal; rename takes newName.
    Q_INVOKABLE bool request(const QString &requestId, const QString &command,
                             const QString &name = {}, const QByteArray &script = {}, const QString &newName = {});
    static constexpr qsizetype MaximumScriptBytes = 4 * 1024 * 1024;
signals:
    void configChanged();
    void secretChanged();
    void stateChanged();
    void finished(const QString &requestId, const QVariantMap &result);
private:
    QString m_host, m_userName, m_secret, m_security = QStringLiteral("starttls");
    int m_port = 4190, m_timeoutMs = 15000, m_pending = 0;
    bool m_allowInsecureAuth = false;
};
}
