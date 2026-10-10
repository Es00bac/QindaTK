// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include "local_database_provider.h"

namespace QindaTK {

// Atomic, versioned local codec for form/report layout and stable SQL names
// (docs/forms-reports.md#design-format). Version 1 files and the version 2
// session files LocalDataDesignDocument writes are both read here.
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
    // Structure only without a provider. With one, also checks every bound
    // field against the live schema and runs the report definition through
    // the report parser: {ok, error, design, fields: [{name, status,
    // savedType, liveType}], issues: [{field, kind, message, fatal}]}.
    // status is ok, missing, typeChanged or unsupported; missing fields and
    // report errors are fatal, a changed type alone is a warning.
    Q_INVOKABLE QVariantMap validate(const QVariantMap &design,
                                     LocalDatabaseProvider *provider = nullptr) const;
    // Empty when `design` is a well-formed version 1 or 2 design.
    static QString checkStructure(const QVariantMap &design);
    // Every field name the form and report refer to.
    static QStringList referencedFields(const QVariantMap &design);
    // Form controls (D-311, docs/forms-reports.md#form-controls), in
    // local_data_design_controls.cpp: the control types, why `controls` is
    // not a well-formed list (or empty), and a control of `type` with its
    // default size and properties.
    static QStringList formControlTypes();
    static QString checkFormControls(const QVariantList &controls);
    static QVariantMap formControlDefaults(const QString &type);
    // The same for QML (FormDesigner without a design document).
    Q_INVOKABLE QStringList controlTypes() const { return formControlTypes(); }
    Q_INVOKABLE QVariantMap controlDefaults(const QString &type) const { return formControlDefaults(type); }
    Q_INVOKABLE QString checkControls(const QVariantList &controls) const { return checkFormControls(controls); }
};
}
