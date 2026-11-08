// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
// Receives the QindaQt registrar's MenuHostedChanged signal (D-361).
// D-Bus signal delivery needs a slot, hence this small QObject.
// Built only with QINDATK_HAVE_DBUS.
#include <QDBusObjectPath>
#include <QObject>
#include <functional>

namespace QindaTK::GlobalMenu {

class HostedSignal : public QObject {
    Q_OBJECT
public:
    std::function<void(const QString &provider, const QString &path, bool hosted)> callback;
public slots:
    void menuHostedChanged(const QString &provider, const QDBusObjectPath &path, bool hosted)
    {
        if (callback)
            callback(provider, path.path(), hosted);
    }
};

} // namespace QindaTK::GlobalMenu
