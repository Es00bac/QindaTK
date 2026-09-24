// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "property_document.h"
#include <QJsonObject>
#include <QPointer>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <optional>

namespace QindaTK {

// A bounded operation packet. IDs are canonical UUID strings; logicalTime is
// decimal on disk and in QML to avoid JavaScript's 53-bit precision limit.
struct PropertySyncOperation {
    QString documentId;
    QString operationId;
    QString actorId;
    quint64 logicalTime = 0;
    QString entityId;
    QString propertyId;
    QVariant value;
};

// The bounded, type-preserving wire/store representation shared by local and
// interprocess transports. Rejected payloads never reach a document.
std::optional<QJsonObject> propertySyncOperationToJson(const PropertySyncOperation &operation);
std::optional<PropertySyncOperation> propertySyncOperationFromJson(const QJsonObject &object);

class LocalPropertySyncClient;
class PropertySyncSocketClient;

// GUI-thread, process-local transport with an optional QSaveFile operation
// store. Clients hold distinct PropertyDocument instances with the same base
// snapshot. The service never owns or mutates their documents.
class LocalPropertySyncService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString documentId READ documentId WRITE setDocumentId NOTIFY changed)
    Q_PROPERTY(QString storagePath READ storagePath WRITE setStoragePath NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(int operationCount READ operationCount NOTIFY changed)
public:
    explicit LocalPropertySyncService(QObject *parent = nullptr) : QObject(parent) {}
    QString documentId() const { return m_documentId; }
    void setDocumentId(const QString &id);
    QString storagePath() const { return m_storagePath; }
    void setStoragePath(const QString &path);
    QString lastError() const { return m_lastError; }
    int operationCount() const { return m_operations.size(); }
    const QVector<PropertySyncOperation> &operations() const { return m_operations; }
    Q_INVOKABLE bool open();
    bool attach(LocalPropertySyncClient *client);
    void detach(LocalPropertySyncClient *client);
    bool submit(const PropertySyncOperation &operation);
    void publishPresence(LocalPropertySyncClient *client, const QVariantMap &presence);
signals:
    void changed();
private:
    bool fail(const QString &message);
    bool persist(const QVector<PropertySyncOperation> &operations);
    QString m_documentId;
    QString m_storagePath;
    QString m_lastError;
    QVector<PropertySyncOperation> m_operations;
    QVector<QPointer<LocalPropertySyncClient>> m_clients;
    bool m_opened = false;
};

class LocalPropertySyncClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::PropertyDocument *document READ document WRITE setDocument NOTIFY changed)
    Q_PROPERTY(QindaTK::LocalPropertySyncService *service READ service WRITE setService NOTIFY changed)
    Q_PROPERTY(QString actorId READ actorId WRITE setActorId NOTIFY changed)
    Q_PROPERTY(bool connected READ isConnected NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit LocalPropertySyncClient(QObject *parent = nullptr) : QObject(parent) {}
    ~LocalPropertySyncClient() override;
    PropertyDocument *document() const { return m_document; }
    void setDocument(PropertyDocument *document);
    LocalPropertySyncService *service() const { return m_service; }
    void setService(LocalPropertySyncService *service);
    QString actorId() const { return m_actorId; }
    void setActorId(const QString &id);
    bool isConnected() const { return m_connected; }
    int pendingCount() const { return m_pending.size(); }
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE bool connectToService();
    Q_INVOKABLE void disconnectFromService();
    Q_INVOKABLE bool setValue(const QString &entityId, const QString &propertyId,
                             const QVariant &value);
    Q_INVOKABLE void setPresence(const QVariantMap &presence);
    // Public for protocol adapters and deterministic duplicate/reorder tests.
    bool receive(const PropertySyncOperation &operation);
signals:
    void changed();
    void operationLost(const QString &operationId, const QString &winningOperationId);
    void ownPresenceChanged(const QVariantMap &presence);
    void presenceChanged(const QString &actorId, const QVariantMap &presence);
    void presenceLeft(const QString &actorId);
private:
    friend class LocalPropertySyncService;
    friend class PropertySyncSocketClient;
    bool fail(const QString &message);
    bool flush();
    void receivePresence(const QString &actorId, const QVariantMap &presence);
    void receivePresenceLeft(const QString &actorId);
    QPointer<PropertyDocument> m_document;
    QPointer<LocalPropertySyncService> m_service;
    QString m_actorId;
    QString m_lastError;
    quint64 m_clock = 0;
    quint64 m_expectedRevision = 0;
    quint64 m_resetGeneration = 0;
    bool m_connected = false;
    QVector<PropertySyncOperation> m_pending;
    QHash<QString, PropertySyncOperation> m_seen;
    QHash<QString, PropertySyncOperation> m_winners;
    QVariantMap m_presence;
};

} // namespace QindaTK
