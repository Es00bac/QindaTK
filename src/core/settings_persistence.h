// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "settings_model.h"
#include "state_store.h"
#include <QJsonObject>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// The file owns preferences only. Document content and workspace topology use
// independent stores and never enter this schema.
class SettingsPersistence : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit SettingsPersistence(QObject *parent = nullptr);
    QString path() const { return m_path; }
    void setPath(const QString &path);
    QString lastError() const { return m_lastError; }
    void setStore(StateStorePtr store);
    Q_INVOKABLE bool load(SettingsModel *model);
    Q_INVOKABLE bool save(const SettingsModel *model);
    static QByteArray encode(const QVariantMap &values, const QJsonObject &extensions = {},
                             OperationError *error = nullptr);
    static std::optional<QVariantMap> decode(const QByteArray &bytes,
                                             QJsonObject *extensions = nullptr,
                                             OperationError *error = nullptr);
signals:
    void pathChanged();
    void lastErrorChanged();
private:
    void fail(const OperationError &error);
    QString m_path;
    QString m_lastError;
    QJsonObject m_extensions;
    StateStorePtr m_store;
};

} // namespace QindaTK
