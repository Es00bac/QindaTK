// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include "property_binding.h"

namespace QindaTK {
// Stable token identities and bounded paste splitting. The QML editor owns
// interaction state; host validation/completion returns token candidates.
class TokenEditorService : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit TokenEditorService(QObject *parent = nullptr);
    Q_INVOKABLE QVariantMap createToken(const QString &label, const QString &value = {},
                                        bool valid = true) const;
    Q_INVOKABLE QVariantMap splitText(const QString &text) const;
    // QVariantList keeps PropertyDocument's strict metatype contract intact;
    // direct JS Array -> QVariant update would arrive as QJSValue.
    Q_INVOKABLE bool commit(PropertyBinding *binding, const QVariantList &tokens) const;
};
} // namespace QindaTK
