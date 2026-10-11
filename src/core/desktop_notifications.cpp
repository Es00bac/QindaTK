// SPDX-License-Identifier: LGPL-3.0-or-later
#include "desktop_notifications.h"
#include <QCoreApplication>
#ifdef QINDATK_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#endif

namespace QindaTK {
namespace {
#ifdef QINDATK_HAVE_DBUS
const QString service = QStringLiteral("org.freedesktop.Notifications");
const QString path = QStringLiteral("/org/freedesktop/Notifications");
QDBusMessage call(const QString &method)
{ return QDBusMessage::createMethodCall(service, path, service, method); }
#endif
}
DesktopNotifications::DesktopNotifications(QObject *parent)
    : QObject(parent), m_applicationName(QCoreApplication::applicationName())
{
#ifdef QINDATK_HAVE_DBUS
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    auto *watcher = new QDBusServiceWatcher(service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &) { refreshAvailability(); });
    bus.connect(service, path, service, QStringLiteral("ActionInvoked"), this, SIGNAL(actionInvoked(quint32,QString)));
    bus.connect(service, path, service, QStringLiteral("NotificationClosed"), this, SIGNAL(notificationClosed(quint32,quint32)));
    refreshAvailability();
#endif
}
void DesktopNotifications::refreshAvailability()
{
#ifdef QINDATK_HAVE_DBUS
    auto query = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                              QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameHasOwner"));
    query << service;
    auto *pending = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(query), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending] {
        const QDBusPendingReply<bool> reply = *pending;
        const bool available = !reply.isError() && reply.value();
        pending->deleteLater();
        if (m_available == available) return;
        m_available = available;
        emit availableChanged();
    });
#endif
}
bool DesktopNotifications::post(const QString &requestId, const QString &summary, const QString &body,
                                 const QString &icon, const QStringList &actions, const QVariantMap &hints,
                                 int timeoutMs, quint32 replacesId)
{
    if (!m_available) return false;
    if (summary.size() > 8192 || body.size() > 65536 || actions.size() > 64 || actions.size() % 2 != 0) {
        emit failed(requestId, QStringLiteral("Invalid or oversized notification"));
        return false;
    }
#ifdef QINDATK_HAVE_DBUS
    auto request = call(QStringLiteral("Notify"));
    // No auto-start even if the server disappears between availability and send.
    request.setAutoStartService(false);
    request << m_applicationName << replacesId << icon << summary << body << actions << hints << timeoutMs;
    auto *pending = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, requestId] {
        const QDBusPendingReply<quint32> reply = *pending;
        if (reply.isError()) emit failed(requestId, reply.error().message());
        else emit delivered(requestId, reply.value());
        pending->deleteLater();
    });
    return true;
#else
    Q_UNUSED(requestId) Q_UNUSED(summary) Q_UNUSED(body) Q_UNUSED(icon)
    Q_UNUSED(actions) Q_UNUSED(hints) Q_UNUSED(timeoutMs) Q_UNUSED(replacesId)
    return false;
#endif
}
void DesktopNotifications::close(quint32 nativeId)
{
#ifdef QINDATK_HAVE_DBUS
    if (!m_available || nativeId == 0) return;
    auto request = call(QStringLiteral("CloseNotification"));
    request.setAutoStartService(false);
    request << nativeId;
    QDBusConnection::sessionBus().asyncCall(request);
#else
    Q_UNUSED(nativeId)
#endif
}
}
