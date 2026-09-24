// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include "local_database_provider.h"

namespace QindaTK {

// Atomic, versioned local codec for form/report layout and stable SQL names.
class LocalDataDesignStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString defaultPath READ defaultPath CONSTANT)
public:
    explicit LocalDataDesignStore(QObject *parent = nullptr);
    QString defaultPath() const;
    Q_INVOKABLE QVariantMap saveToFile(const QString &path, const QVariantMap &design,
                                       LocalDatabaseProvider *provider = nullptr) const;
    Q_INVOKABLE QVariantMap loadFromFile(const QString &path,
                                         LocalDatabaseProvider *provider = nullptr) const;
    Q_INVOKABLE QVariantMap validate(const QVariantMap &design,
                                     LocalDatabaseProvider *provider = nullptr) const;
private:
    static QVariantMap validateStructure(const QVariantMap &design);
};
}
