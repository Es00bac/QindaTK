// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// A manifest entry describes one operation, not a whole application family.
// Publishing metadata never creates or loads its optional backend.
struct CapabilityDescriptor {
    QString featureId;
    QString operationId;
    QString backendId;
    QString backendVersion;
    bool available = false;
    QString unavailableReason;
    int priority = 0;
    friend bool operator==(const CapabilityDescriptor &,
                           const CapabilityDescriptor &) = default;
};

struct CapabilityResult {
    QString featureId;
    QString operationId;
    QString backendId;
    QString backendVersion;
    bool available = false;
    QString reason;
    QVariantMap toQml() const;
    friend bool operator==(const CapabilityResult &,
                           const CapabilityResult &) = default;
};

class CapabilityRegistry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString generation READ generation NOTIFY generationChanged)
public:
    explicit CapabilityRegistry(QObject *parent = nullptr) : QObject(parent) {}

    // Backend IDs are process-unique. One backend may publish many operations.
    // Re-publishing the same triple replaces its entry and notifies bindings.
    bool publish(const CapabilityDescriptor &entry);
    int removeBackend(const QString &backendId);
    CapabilityResult resolve(const QString &featureId,
                             const QString &operationId) const;
    QString generation() const { return QString::number(m_generation); }

    Q_INVOKABLE QVariantMap query(const QString &featureId,
                                  const QString &operationId) const;
    Q_INVOKABLE bool isAvailable(const QString &featureId,
                                 const QString &operationId) const;
    Q_INVOKABLE QString disabledReason(const QString &featureId,
                                       const QString &operationId) const;
    Q_INVOKABLE QStringList operations(const QString &featureId) const;
signals:
    void capabilityChanged(const QString &featureId, const QString &operationId);
    void generationChanged();
private:
    QVector<CapabilityDescriptor> m_entries;
    quint64 m_generation = 0;
};

// A reactive QML view of one operation. The application or adapter owns the
// registry and publishes metadata from the GUI thread; bindings receive a
// fresh result when a backend appears, disappears or changes state.
class CapabilityBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(CapabilityRegistry *registry READ registry WRITE setRegistry NOTIFY registryChanged)
    Q_PROPERTY(QString featureId READ featureId WRITE setFeatureId NOTIFY featureIdChanged)
    Q_PROPERTY(QString operationId READ operationId WRITE setOperationId NOTIFY operationIdChanged)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString reason READ reason NOTIFY changed)
    Q_PROPERTY(QString backendId READ backendId NOTIFY changed)
    Q_PROPERTY(QString backendVersion READ backendVersion NOTIFY changed)
public:
    explicit CapabilityBinding(QObject *parent = nullptr) : QObject(parent) {}
    CapabilityRegistry *registry() const { return m_registry; }
    void setRegistry(CapabilityRegistry *registry);
    QString featureId() const { return m_featureId; }
    void setFeatureId(const QString &featureId);
    QString operationId() const { return m_operationId; }
    void setOperationId(const QString &operationId);
    bool available() const { return m_result.available; }
    QString reason() const { return m_result.reason; }
    QString backendId() const { return m_result.backendId; }
    QString backendVersion() const { return m_result.backendVersion; }
signals:
    void registryChanged();
    void featureIdChanged();
    void operationIdChanged();
    void changed();
private:
    void refresh();
    QPointer<CapabilityRegistry> m_registry;
    QString m_featureId;
    QString m_operationId;
    CapabilityResult m_result;
    QMetaObject::Connection m_changeConnection;
    QMetaObject::Connection m_destroyConnection;
};

} // namespace QindaTK
