// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>

class QPluginLoader;
namespace QindaTK {
class CapabilityRegistry;
class ExtensionProvider;

class ExtensionRegistry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(CapabilityRegistry *capabilities READ capabilities WRITE setCapabilities NOTIFY capabilitiesChanged)
    Q_PROPERTY(QVariantList extensions READ extensions NOTIFY extensionsChanged)
public:
    explicit ExtensionRegistry(QObject *parent = nullptr);
    ~ExtensionRegistry() override;

    CapabilityRegistry *capabilities() const { return m_capabilities; }
    void setCapabilities(CapabilityRegistry *capabilities);
    QVariantList extensions() const;

    // No directory scan or implicit execution. The caller selects a trusted
    // local descriptor, then calls load() explicitly.
    Q_INVOKABLE bool registerDescriptor(const QString &path);
    Q_INVOKABLE bool load(const QString &id);
    Q_INVOKABLE bool unload(const QString &id);
    Q_INVOKABLE QVariantMap invoke(const QString &id, const QString &operation,
                                   const QVariantMap &arguments = {});
    Q_INVOKABLE QVariantMap extension(const QString &id) const;
    Q_INVOKABLE QVariantList saveState() const;
    Q_INVOKABLE bool restoreState(const QVariantList &snapshot);
signals:
    void capabilitiesChanged();
    void extensionsChanged();
private:
    struct Record;
    QString diagnostic(const Record &record) const;
    QVariantMap view(const Record &record) const;
    void retract(Record &record);
    QMap<QString, std::shared_ptr<Record>> m_records;
    QPointer<CapabilityRegistry> m_capabilities;
    QMetaObject::Connection m_capabilityChange;
};

} // namespace QindaTK
