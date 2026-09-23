// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"

#include <QObject>
#include <QMetaType>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>

namespace QindaTK {

enum class SettingScope { App, Document, Workspace };

struct SettingDescriptor final {
    QString key;
    SettingScope scope = SettingScope::App;
    QMetaType valueType;
    QVariant defaultValue;
    bool restartRequired = false;
    std::function<OperationError(const QVariant &)> validate;
};

// In-memory typed descriptor and staging layer. Persistence and schema
// migration belong to the application codec; import/export use scoped keys.
class SettingsModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool staging READ staging NOTIFY stateChanged)
    Q_PROPERTY(bool needsRestart READ needsRestart NOTIFY stateChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap values READ exportValues NOTIFY valuesChanged)
    Q_PROPERTY(QVariantMap orphanedValues READ orphanedValues NOTIFY valuesChanged)

public:
    explicit SettingsModel(QObject *parent = nullptr);

    [[nodiscard]] bool staging() const { return m_staging; }
    [[nodiscard]] bool needsRestart() const;
    [[nodiscard]] bool valid() const { return !m_error; }
    [[nodiscard]] QString error() const { return m_error.message; }
    [[nodiscard]] QVariantMap exportValues() const;
    [[nodiscard]] QVariantMap orphanedValues() const { return m_orphaned; }
    [[nodiscard]] QVariant value(const QString &key, SettingScope scope = SettingScope::App,
                                 const QString &instanceId = {}) const;
    [[nodiscard]] QVariant stagedValue(const QString &key, SettingScope scope = SettingScope::App,
                                       const QString &instanceId = {}) const;

    // C++ registration allows std::function validators and QMetaType checking.
    bool registerSetting(const SettingDescriptor &descriptor,
                         const QString &instanceId = {});
    Q_INVOKABLE bool beginStage();
    bool setStagedValue(const QString &key, const QVariant &value,
                        SettingScope scope = SettingScope::App,
                        const QString &instanceId = {});
    Q_INVOKABLE bool apply();
    Q_INVOKABLE void cancel();
    bool resetKey(const QString &key, SettingScope scope = SettingScope::App,
                  const QString &instanceId = {});
    bool resetScope(SettingScope scope, const QString &instanceId = {});

    // The map format is an exact stable boundary: app:<key>,
    // document:<instance>:<key>, workspace:<instance>:<key>.
    bool importValues(const QVariantMap &values);

    Q_INVOKABLE bool setValue(const QString &scopedKey, const QVariant &value);
    Q_INVOKABLE QVariant valueForKey(const QString &scopedKey) const;
    Q_INVOKABLE bool resetSetting(const QString &scopedKey);

    static QString scopedKey(const QString &key, SettingScope scope,
                             const QString &instanceId = {});

signals:
    void stateChanged();
    void valuesChanged();
    void applied();
    void validationFailed(const QString &key, const QString &message);
    void unsupportedKeysImported(const QStringList &keys);

private:
    struct Entry final {
        SettingDescriptor descriptor;
        QString instanceId;
        QVariant committed;
    };
    struct ParsedKey final {
        SettingScope scope;
        QString instanceId;
        QString key;
    };

    static std::optional<ParsedKey> parseScopedKey(const QString &text);
    QString entryId(const QString &key, SettingScope scope, const QString &instanceId) const;
    OperationError validate(const Entry &entry, const QVariant &value) const;
    void setError(const OperationError &error);
    bool onOwnerThread() const;

    QHash<QString, Entry> m_entries;
    QHash<QString, QVariant> m_staged;
    QVariantMap m_orphaned;
    OperationError m_error;
    bool m_staging = false;
};

} // namespace QindaTK

Q_DECLARE_METATYPE(QindaTK::SettingScope)
