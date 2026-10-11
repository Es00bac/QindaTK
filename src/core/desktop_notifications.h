// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// An optional org.freedesktop.Notifications client. It never starts a server;
// outside a desktop session available stays false and post() is a no-op.
// All calls/replies belong to this object's thread. Destruction drops replies.
class DesktopNotifications : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString applicationName MEMBER m_applicationName NOTIFY configChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
public:
    explicit DesktopNotifications(QObject *parent = nullptr);
    bool available() const { return m_available; }
    // Actions are alternating action-id/label strings. The request ID is local
    // correlation only; nativeId comes from the notification server.
    Q_INVOKABLE bool post(const QString &requestId, const QString &summary, const QString &body = {},
                          const QString &icon = {}, const QStringList &actions = {},
                          const QVariantMap &hints = {}, int timeoutMs = -1, quint32 replacesId = 0);
    Q_INVOKABLE void close(quint32 nativeId);
signals:
    void configChanged();
    void availableChanged();
    void delivered(const QString &requestId, quint32 nativeId);
    void failed(const QString &requestId, const QString &error);
    void actionInvoked(quint32 nativeId, const QString &action);
    void notificationClosed(quint32 nativeId, quint32 reason);
private:
    void refreshAvailability();
    QString m_applicationName;
    bool m_available = false;
};
}
