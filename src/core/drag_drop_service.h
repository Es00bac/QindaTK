// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "property_document.h"
#include "transfer_payload.h"
#include "undo_service.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

struct DropPreview {
    bool accepted = false;
    QString summary;
    QString error;
    ErrorCode errorCode = ErrorCode::Validation;
    QString token;
    // Set only by a domain adapter that can commit source removal and
    // destination insertion atomically under the relevant undo boundaries.
    bool atomicCrossDocumentMove = false;
};

// Domain-owned endpoint. A preview must be side-effect free. A successful
// token is private to the endpoint/session pair; commit must either publish
// the complete domain operation or reject without changing authoritative data.
// Implementations return true from supportsAtomicCrossDocumentMove only when
// they can guarantee source+destination mutation as one atomic operation.
class DropTarget : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~DropTarget() override = default;

    virtual QString stableTargetId() const = 0;
    virtual QString viewId() const = 0;
    virtual quint64 viewGeneration() const = 0;
    virtual SnapshotHeader snapshot() const = 0;
    virtual bool supportsAtomicCrossDocumentMove() const { return false; }
    virtual DropPreview previewDrop(const QString &sessionId,
                                    const TransferPayload &payload,
                                    TransferOperation operation,
                                    const QVariantMap &location) = 0;
    virtual OperationError commitDrop(const QString &sessionId,
                                      const QString &previewToken) = 0;
    virtual void cancelDrop(const QString &sessionId,
                            const QString &previewToken) = 0;
};

// Reference object adapter for applications whose collection property is a
// QVariantList of QVariantMap rows with stable UUID strings in the `id` key.
// It supports safe Copy only, remaps every incoming row ID, rejects unresolved
// resource/entity references, and commits the insertion as one undoable edit.
class PropertyCollectionDropTarget final : public DropTarget {
public:
    PropertyCollectionDropTarget(PropertyDocument *document, UndoService *undo,
                                 EntityId collectionEntity, QString propertyId,
                                 QString targetId, QString viewId,
                                 QObject *parent = nullptr);

    QString stableTargetId() const override { return m_targetId; }
    QString viewId() const override { return m_viewId; }
    quint64 viewGeneration() const override { return m_viewGeneration; }
    SnapshotHeader snapshot() const override;
    void setViewGeneration(quint64 generation) { m_viewGeneration = generation; }
    DropPreview previewDrop(const QString &sessionId,
                            const TransferPayload &payload,
                            TransferOperation operation,
                            const QVariantMap &location) override;
    OperationError commitDrop(const QString &sessionId,
                              const QString &previewToken) override;
    void cancelDrop(const QString &sessionId,
                    const QString &previewToken) override;

private:
    struct Plan {
        QString token;
        SnapshotHeader snapshot;
        QVariantList candidate;
    };
    QPointer<PropertyDocument> m_document;
    QPointer<UndoService> m_undo;
    EntityId m_collectionEntity;
    QString m_propertyId;
    QString m_targetId;
    QString m_viewId;
    quint64 m_viewGeneration = 0;
    QHash<QString, Plan> m_plans;
};

// Coordinates a drag session without owning model data or platform events.
// View adapters provide stable view IDs/generations and autoscroll requests;
// domain endpoints own validation, transaction creation and undo recording.
class DragDropService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString sessionId READ sessionId NOTIFY changed)
    Q_PROPERTY(QString sourceDocumentId READ sourceDocumentId NOTIFY changed)
    Q_PROPERTY(QString sourceRevision READ sourceRevision NOTIFY changed)
    Q_PROPERTY(QString sourceViewId READ sourceViewId NOTIFY changed)
    Q_PROPERTY(QString sourceViewGeneration READ sourceViewGeneration NOTIFY changed)
    Q_PROPERTY(QStringList allowedOperations READ allowedOperations NOTIFY changed)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY changed)
    Q_PROPERTY(QVariantMap error READ errorMap NOTIFY changed)
public:
    explicit DragDropService(QObject *parent = nullptr);
    ~DragDropService() override;

    QString phase() const;
    QString sessionId() const { return m_sessionId; }
    QString sourceDocumentId() const;
    QString sourceRevision() const;
    QString sourceViewId() const { return m_sourceViewId; }
    QString sourceViewGeneration() const { return QString::number(m_sourceViewGeneration); }
    QStringList allowedOperations() const;
    QVariantMap preview() const { return m_preview; }
    QVariantMap errorMap() const;

    // Empty allowedOperations means the payload's selected operation alone.
    // sourceDocument is optional for platform/external payloads; when supplied,
    // its stable identity/revision/reset generation are checked throughout.
    QString beginDrag(const TransferPayload &payload,
                      const QVector<TransferOperation> &allowedOperations = {},
                      const QString &sourceViewId = {},
                      quint64 sourceViewGeneration = 0,
                      PropertyDocument *sourceDocument = nullptr,
                      OperationError *error = nullptr);

    bool previewDrop(DropTarget *target, TransferOperation operation,
                     const QVariantMap &location = {});
    OperationError commitDrop();

    Q_INVOKABLE QString beginEncodedDrag(const QByteArray &encodedPayload,
                                         const QStringList &allowedOperations,
                                         const QString &sourceViewId,
                                         const QString &sourceViewGeneration,
                                         PropertyDocument *sourceDocument = nullptr);
    Q_INVOKABLE QVariantMap previewEncodedDrop(QObject *target,
                                               const QString &operation,
                                               const QVariantMap &location = {});
    Q_INVOKABLE bool commit();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void escape() { cancel(); }
    Q_INVOKABLE void invalidateView(const QString &viewId);
    Q_INVOKABLE void requestAutoscroll(const QString &viewId,
                                       const QString &direction, bool active);

signals:
    void changed();
    void committed(const QString &sessionId);
    void cancelled(const QString &sessionId);
    void rejected(const QString &sessionId, const QString &message, int code);
    void autoscrollRequested(const QString &viewId, const QString &direction,
                             bool active);

private:
    enum class Phase { Idle, Dragging, Preview, PreviewRejected,
                       Committing, Committed, Cancelled, Rejected };
    static QString operationName(TransferOperation operation);
    static std::optional<TransferOperation> parseOperation(const QString &name);
    static bool sameHeader(const SnapshotHeader &a, const SnapshotHeader &b);
    bool fail(ErrorCode code, const QString &message, bool endSession = false);
    bool sourceIsCurrent() const;
    bool targetIsCurrent() const;
    void clearPreview(bool notifyTarget);
    void resetSession(Phase phase, bool notifyTarget);
    void stopAutoscroll();

    Phase m_phase = Phase::Idle;
    QString m_sessionId;
    TransferPayload m_payload;
    QVector<TransferOperation> m_allowed;
    QString m_sourceViewId;
    quint64 m_sourceViewGeneration = 0;
    QPointer<PropertyDocument> m_sourceDocument;
    bool m_hadSourceDocument = false;
    SnapshotHeader m_sourceSnapshot;
    QPointer<DropTarget> m_target;
    QString m_targetId;
    QString m_targetViewId;
    quint64 m_targetViewGeneration = 0;
    SnapshotHeader m_targetSnapshot;
    TransferOperation m_previewOperation = TransferOperation::Copy;
    QString m_previewToken;
    QVariantMap m_preview;
    OperationError m_error;
    bool m_commitAttempted = false;
    QString m_autoscrollViewId;
    QString m_autoscrollDirection;
};

} // namespace QindaTK
