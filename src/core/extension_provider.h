// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QString>
#include <QVariantMap>
#include <QtPlugin>

namespace QindaTK {

// AGENT-CONTRACT: ExtensionRegistry owns a trusted in-process plugin loader;
// ExtensionProvider owns extension resources and releases them in stop().
// Both sides must rebuild together across C++/Qt ABI changes.
class ExtensionProvider {
public:
    virtual ~ExtensionProvider() = default;
    virtual QString extensionId() const = 0;
    virtual bool start(const QVariantMap &savedState, QString *error) = 0;
    virtual QVariantMap saveState() const = 0;
    virtual QVariantMap invoke(const QString &operationId,
                               const QVariantMap &arguments) = 0;
    virtual void stop() = 0;
};

} // namespace QindaTK

#define QindaTK_ExtensionProvider_IID "org.qindatk.ExtensionProvider/1.0"
Q_DECLARE_INTERFACE(QindaTK::ExtensionProvider, QindaTK_ExtensionProvider_IID)
