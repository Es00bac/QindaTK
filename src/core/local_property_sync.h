// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "property_document.h"
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <optional>

namespace QindaTK {

// A bounded operation packet. IDs are canonical UUID strings; logicalTime is
// decimal on disk and in QML to avoid JavaScript's 53-bit precision limit.
// Kinds (docs/collaboration.md "Set up" table): Set is a last-writer
// property value; Restore a version snapshot; Create/Delete entity lifecycle
// with observed-remove deletes; TextInsert/TextDelete a plain-text sequence;
// Checkpoint a shared named version that never changes document values.
struct PropertySyncOperation {
    enum class Kind { Set, Restore, Create, Delete, TextInsert, TextDelete, Checkpoint };
    QString documentId;
    QString operationId;
    QString actorId;
    quint64 logicalTime = 0;
    QString entityId;
    QString propertyId;
    QVariant value;
    Kind kind = Kind::Set;
    QString meta;   // TextInsert anchor element ID, or Checkpoint name/time JSON
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
    Q_PROPERTY(QStringList textProperties READ textProperties WRITE setTextProperties NOTIFY changed)
    Q_PROPERTY(bool readOnly READ isReadOnly WRITE setReadOnly NOTIFY changed)
    Q_PROPERTY(bool connected READ isConnected NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit LocalPropertySyncClient(QObject *parent = nullptr) : QObject(parent) {}
    ~LocalPropertySyncClient() override;
    PropertyDocument *document() const { return m_document; }
    // AGENT-CONTRACT (docs/collaboration.md "Set up"): the document assigned
    // here is this replica's baseline; every replica must start from the same
    // entities and values. Baseline entities and values are captured now.
    void setDocument(PropertyDocument *document);
    LocalPropertySyncService *service() const { return m_service; }
    void setService(LocalPropertySyncService *service);
    QString actorId() const { return m_actorId; }
    void setActorId(const QString &id);
    // String properties merged as character sequences. Must be identical on
    // every replica and set before the first operation.
    QStringList textProperties() const { return m_textProperties; }
    void setTextProperties(const QStringList &properties);
    bool isReadOnly() const { return m_readOnly; }
    void setReadOnly(bool readOnly);
    bool isConnected() const { return m_connected; }
    int pendingCount() const { return m_pending.size(); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE bool connectToService();
    Q_INVOKABLE void disconnectFromService();
    Q_INVOKABLE bool setValue(const QString &entityId, const QString &propertyId,
                             const QVariant &value);
    // Returns the new entity UUID, or an empty string on failure.
    Q_INVOKABLE QString createEntity(const QVariantMap &values = {});
    Q_INVOKABLE bool deleteEntity(const QString &entityId);
    // UTF-16 code-unit positions in the property's current text.
    Q_INVOKABLE bool replaceText(const QString &entityId, const QString &propertyId,
                                 int start, int removeCount, const QString &text);
    // Selective undo of this actor's own edits, issued as new operations.
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool restoreVersion(const QByteArray &snapshot);
    Q_INVOKABLE void setPresence(const QVariantMap &presence);
    // Shares a named snapshot with every replica through the operation log.
    QString shareCheckpoint(const QString &name, const QByteArray &snapshot);
    const QVector<PropertySyncOperation> &sharedCheckpoints() const { return m_checkpoints; }
    // Public for protocol adapters and deterministic duplicate/reorder tests.
    bool receive(const PropertySyncOperation &operation);
signals:
    void changed();
    void operationLost(const QString &operationId, const QString &winningOperationId);
    void undoConflict(const QString &operationId, const QString &reason);
    void checkpointShared(const QString &operationId);
    void ownPresenceChanged(const QVariantMap &presence);
    void presenceChanged(const QString &actorId, const QVariantMap &presence);
    void presenceLeft(const QString &actorId);
private:
    friend class LocalPropertySyncService;
    friend class PropertySyncSocketClient;
    using Kind = PropertySyncOperation::Kind;
    struct TextElement {
        QString id;          // "<op>:<index>"
        QChar character;
        quint64 time = 0;
        QString actorId;
        QString op;
        int index = 0;
        bool deleted = false;
    };
    struct TextSequence {
        QVector<TextElement> elements;
        QSet<QString> pendingDeletes;
        QVector<PropertySyncOperation> waiting;   // inserts whose anchor is unknown
    };
    struct HistoryEntry {
        Kind kind = Kind::Set;
        QString operationId;
        QString entityId;
        QString propertyId;
        QVariant previous;
        QStringList elements;   // text elements this step inserted
        QStringList removed;    // text elements this step deleted
        QString text;           // characters of the removed elements
    };
    static HistoryEntry step(Kind kind, const QString &operationId, const QString &entityId,
                             const QString &propertyId = {}, const QVariant &previous = {})
    {
        HistoryEntry entry;
        entry.kind = kind;
        entry.operationId = operationId;
        entry.entityId = entityId;
        entry.propertyId = propertyId;
        entry.previous = previous;
        return entry;
    }
    enum class Record { Fresh, Undo, Redo };
    bool fail(const QString &message);
    bool flush();
    bool inSync() const;
    bool readyToEdit();
    PropertySyncOperation makeOperation(Kind kind, const QString &entityId,
                                        const QString &propertyId, const QVariant &value,
                                        const QString &meta = {});
    bool submitLocal(const PropertySyncOperation &operation,
                     const std::optional<HistoryEntry> &entry = std::nullopt);
    void record(HistoryEntry entry);
    bool invert(const HistoryEntry &entry, Record into);
    bool setValueImpl(const QString &entityId, const QString &propertyId,
                      const QVariant &value, bool recordHistory);
    bool deleteEntityImpl(const QString &entityId, bool recordHistory);
    bool reviveEntity(const QString &entityId, bool recordHistory);
    bool textEdit(const QString &entityId, const QString &propertyId,
                  const QStringList &deleteIds, const QString &insertText,
                  const QString &anchor, bool recordHistory);
    bool receiveSet(const PropertySyncOperation &operation);
    bool receiveRestore(const PropertySyncOperation &operation);
    bool receiveEntity(const PropertySyncOperation &operation);
    bool receiveText(const PropertySyncOperation &operation);
    bool entityAlive(const QString &entityId) const;
    bool reconcileEntity(const QString &entityId);
    bool addSupport(const QString &entityId, const QString &operationId);
    QVariant baseValue(const QString &entityId, const QString &propertyId) const;
    bool isTextProperty(const QString &propertyId) const;
    TextSequence *sequence(const QString &entityId, const QString &propertyId);
    bool integrateInsert(TextSequence &sequence, const PropertySyncOperation &operation);
    QString visibleText(const TextSequence &sequence) const;
    bool publishText(const QString &entityId, const QString &propertyId);
    void receivePresence(const QString &actorId, const QVariantMap &presence);
    void receivePresenceLeft(const QString &actorId);
    QPointer<PropertyDocument> m_document;
    QPointer<LocalPropertySyncService> m_service;
    QString m_actorId;
    QString m_lastError;
    QStringList m_textProperties;
    quint64 m_clock = 0;
    quint64 m_expectedRevision = 0;
    quint64 m_resetGeneration = 0;
    bool m_connected = false;
    bool m_readOnly = false;
    QVector<PropertySyncOperation> m_pending;
    QHash<QString, PropertySyncOperation> m_seen;
    QHash<QString, PropertySyncOperation> m_winners;
    QSet<QString> m_baselineEntities;
    QHash<QString, QVariant> m_baselineValues;
    QHash<QString, QSet<QString>> m_support;
    QHash<QString, QSet<QString>> m_removed;
    QHash<QString, TextSequence> m_text;
    QVector<PropertySyncOperation> m_checkpoints;
    QVector<HistoryEntry> m_undo;
    QVector<HistoryEntry> m_redo;
    Record m_recording = Record::Fresh;
    QVariantMap m_presence;
};

} // namespace QindaTK
