// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include <QObject>
#include <QHash>
#include <QSet>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

struct PropertyDescriptor {
    QString id;
    QMetaType type;
    QVariant defaultValue;
    QString unitId;
    bool writable = true;
    bool animatable = false;
};

struct PropertyChange {
    EntityId target;
    QString propertyId;
    QVariant before;
    QVariant after;
};

// Small authoritative model and adapter reference for application domains.
// Other domains can implement the same transaction boundary around their own
// storage; no control owns this state. All methods run on the GUI thread.
class PropertyDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString documentId READ documentIdString CONSTANT)
    Q_PROPERTY(QString revision READ revisionString NOTIFY changed)
    Q_PROPERTY(QString resetGeneration READ resetGenerationString NOTIFY reset)
public:
    explicit PropertyDocument(QObject *parent = nullptr);
    DocumentId documentId() const { return m_documentId; }
    QString documentIdString() const { return m_documentId.toString(); }
    RevisionId revision() const { return {m_revision}; }
    QString revisionString() const { return QString::number(m_revision); }
    quint64 resetGeneration() const { return m_resetGeneration; }
    QString resetGenerationString() const { return QString::number(m_resetGeneration); }
    SnapshotHeader header() const;

    bool defineProperty(const PropertyDescriptor &descriptor);
    std::optional<PropertyDescriptor> descriptor(const QString &id) const;
    bool addEntity(EntityId id, const QVariantMap &values = {});
    bool removeEntity(EntityId id);
    bool contains(EntityId id) const;
    bool resetModel(const QVector<QPair<EntityId, QVariantMap>> &entities);
    QVariant value(EntityId id, const QString &propertyId) const;
    QVariant committedValue(EntityId id, const QString &propertyId) const;
    bool setTargetWritable(EntityId id, const QString &propertyId, bool writable);
    bool setTargetAnimated(EntityId id, const QString &propertyId, bool animated);
    bool isTargetWritable(EntityId id, const QString &propertyId) const;
    bool isTargetAnimated(EntityId id, const QString &propertyId) const;
    OperationError validate(EntityId id, const QString &propertyId,
                            const QVariant &candidate) const;

    Q_INVOKABLE QVariant valueFor(const QString &id, const QString &propertyId) const;
    Q_INVOKABLE bool containsId(const QString &id) const;
signals:
    void changed();
    void propertyChanged(const QString &entityId, const QString &propertyId);
    void entityRemoved(const QString &entityId);
    void historyInvalidated();
    void reset();
private:
    friend class EditTransaction;
    friend class UndoService;
    friend class ReferenceDocumentCodec;
    struct EntityState {
        QHash<QString, QVariant> values;
        QSet<QString> readOnlyProperties;
        QSet<QString> animatedProperties;
    };
    struct Preview {
        QUuid transactionId;
        QVector<PropertyChange> changes;
    };
    OperationError applyCommitted(const QVector<PropertyChange> &changes, bool forward);
    OperationError showPreview(const QUuid &transactionId,
                               const QVector<PropertyChange> &changes);
    void discardPreview(const QUuid &transactionId);
    bool validEntityValues(const QVariantMap &values) const;
    bool equalValue(const QVariant &a, const QVariant &b) const;
    DocumentId m_documentId = DocumentId::create();
    quint64 m_revision = 0;
    quint64 m_resetGeneration = 0;
    QHash<QString, PropertyDescriptor> m_descriptors;
    QHash<QString, EntityState> m_entities;
    std::optional<Preview> m_preview;
};

} // namespace QindaTK
