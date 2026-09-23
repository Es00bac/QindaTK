// SPDX-License-Identifier: LGPL-3.0-or-later
#include "drag_drop_service.h"

#include <QSet>
#include <QThread>
#include <QUuid>
#include <utility>

namespace QindaTK {
namespace {
OperationError makeError(ErrorCode code, const QString &message)
{
    return {code, message, {}, {}};
}
}

DragDropService::DragDropService(QObject *parent) : QObject(parent) {}

DragDropService::~DragDropService()
{
    clearPreview(true);
}

QString DragDropService::phase() const
{
    switch (m_phase) {
    case Phase::Idle: return QStringLiteral("idle");
    case Phase::Dragging: return QStringLiteral("dragging");
    case Phase::Preview: return QStringLiteral("preview");
    case Phase::PreviewRejected: return QStringLiteral("previewRejected");
    case Phase::Committing: return QStringLiteral("committing");
    case Phase::Committed: return QStringLiteral("committed");
    case Phase::Cancelled: return QStringLiteral("cancelled");
    case Phase::Rejected: return QStringLiteral("rejected");
    }
    return QStringLiteral("idle");
}

QString DragDropService::sourceDocumentId() const
{
    return m_payload.sourceDocumentId.isValid()
        ? m_payload.sourceDocumentId.toString() : QString{};
}

QString DragDropService::sourceRevision() const
{
    return m_payload.sourceRevision.toDecimal();
}

QStringList DragDropService::allowedOperations() const
{
    QStringList result;
    result.reserve(m_allowed.size());
    for (TransferOperation operation : m_allowed)
        result.append(operationName(operation));
    return result;
}

QVariantMap DragDropService::errorMap() const
{
    if (!m_error) return {};
    return {{QStringLiteral("message"), m_error.message},
            {QStringLiteral("code"), static_cast<int>(m_error.code)},
            {QStringLiteral("fieldPath"), m_error.fieldPath}};
}

QString DragDropService::operationName(TransferOperation operation)
{
    switch (operation) {
    case TransferOperation::Copy: return QStringLiteral("copy");
    case TransferOperation::Move: return QStringLiteral("move");
    case TransferOperation::Link: return QStringLiteral("link");
    }
    return {};
}

std::optional<TransferOperation> DragDropService::parseOperation(const QString &name)
{
    if (name == QStringLiteral("copy")) return TransferOperation::Copy;
    if (name == QStringLiteral("move")) return TransferOperation::Move;
    if (name == QStringLiteral("link")) return TransferOperation::Link;
    return std::nullopt;
}

bool DragDropService::sameHeader(const SnapshotHeader &a, const SnapshotHeader &b)
{
    return a.schemaId == b.schemaId && a.documentId == b.documentId
        && a.revision == b.revision && a.resetGeneration == b.resetGeneration;
}

QString DragDropService::beginDrag(const TransferPayload &payload,
                                  const QVector<TransferOperation> &allowedOperations,
                                  const QString &sourceViewId,
                                  quint64 sourceViewGeneration,
                                  PropertyDocument *sourceDocument,
                                  OperationError *error)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto rejectBegin = [this, error](ErrorCode code, const QString &message) {
        const OperationError value = makeError(code, message);
        if (error) *error = value;
        m_error = value;
        emit changed();
        emit rejected({}, value.message, static_cast<int>(value.code));
        return QString{};
    };
    if (m_phase == Phase::Dragging || m_phase == Phase::Preview
        || m_phase == Phase::PreviewRejected || m_phase == Phase::Committing) {
        return rejectBegin(ErrorCode::Unavailable,
                           QStringLiteral("A drag session is already active."));
    }
    OperationError codecError;
    if (TransferPayloadCodec::encode(payload, &codecError).isEmpty())
        return rejectBegin(codecError.code, codecError.message);
    if (sourceViewId.size() > 512 || sourceViewId.contains(QChar::Null))
        return rejectBegin(ErrorCode::InvalidArgument,
                           QStringLiteral("Source view ID is invalid."));
    if (sourceDocument) {
        const SnapshotHeader current = sourceDocument->header();
        if (current.documentId != payload.sourceDocumentId
            || current.revision != payload.sourceRevision)
            return rejectBegin(ErrorCode::StaleRevision,
                               QStringLiteral("Transfer payload no longer matches its source document."));
    }

    QVector<TransferOperation> accepted = allowedOperations;
    if (accepted.isEmpty()) accepted.append(payload.operation);
    QSet<int> seen;
    for (TransferOperation operation : accepted) {
        const QString name = operationName(operation);
        if (name.isEmpty() || seen.contains(static_cast<int>(operation)))
            return rejectBegin(ErrorCode::InvalidArgument,
                               QStringLiteral("Allowed operations contain an invalid or duplicate entry."));
        seen.insert(static_cast<int>(operation));
    }

    clearPreview(true);
    m_payload = payload;
    m_allowed = std::move(accepted);
    m_sourceViewId = sourceViewId;
    m_sourceViewGeneration = sourceViewGeneration;
    m_sourceDocument = sourceDocument;
    m_hadSourceDocument = sourceDocument != nullptr;
    m_sourceSnapshot = sourceDocument ? sourceDocument->header()
                                     : SnapshotHeader{{}, payload.sourceDocumentId,
                                                      payload.sourceRevision, 0};
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_error = {};
    m_preview.clear();
    m_commitAttempted = false;
    m_phase = Phase::Dragging;
    if (error) *error = {};
    emit changed();
    return m_sessionId;
}

QString DragDropService::beginEncodedDrag(const QByteArray &encodedPayload,
                                          const QStringList &operationNames,
                                          const QString &sourceViewId,
                                          const QString &sourceViewGeneration,
                                          PropertyDocument *sourceDocument)
{
    bool generationOk = false;
    const quint64 generation = sourceViewGeneration.toULongLong(&generationOk);
    if (!generationOk) {
        fail(ErrorCode::InvalidArgument,
             QStringLiteral("Source view generation must be an unsigned decimal string."));
        return {};
    }
    OperationError codecError;
    const auto payload = TransferPayloadCodec::decode(encodedPayload, &codecError);
    if (!payload) {
        fail(codecError.code, codecError.message);
        return {};
    }
    QVector<TransferOperation> operations;
    operations.reserve(operationNames.size());
    for (const QString &name : operationNames) {
        const auto operation = parseOperation(name);
        if (!operation) {
            fail(ErrorCode::InvalidArgument,
                 QStringLiteral("Unknown transfer operation: %1").arg(name));
            return {};
        }
        operations.append(*operation);
    }
    return beginDrag(*payload, operations, sourceViewId, generation,
                     sourceDocument, nullptr);
}

bool DragDropService::sourceIsCurrent() const
{
    return !m_hadSourceDocument || (m_sourceDocument
        && sameHeader(m_sourceDocument->header(), m_sourceSnapshot));
}

bool DragDropService::targetIsCurrent() const
{
    return m_target && m_target->stableTargetId() == m_targetId
        && m_target->viewId() == m_targetViewId
        && m_target->viewGeneration() == m_targetViewGeneration
        && sameHeader(m_target->snapshot(), m_targetSnapshot);
}

bool DragDropService::previewDrop(DropTarget *target,
                                 TransferOperation operation,
                                 const QVariantMap &location)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_phase != Phase::Dragging && m_phase != Phase::Preview
        && m_phase != Phase::PreviewRejected) {
        fail(ErrorCode::Unavailable, QStringLiteral("No active drag can be previewed."));
        return false;
    }
    const QString activeSession = m_sessionId;
    m_phase = Phase::Dragging;
    clearPreview(true);
    if (m_sessionId != activeSession || m_phase != Phase::Dragging) return false;
    if (!sourceIsCurrent()) {
        fail(ErrorCode::StaleRevision,
             QStringLiteral("Source document changed during the drag."), true);
        return false;
    }
    bool operationAllowed = false;
    for (TransferOperation allowed : m_allowed)
        operationAllowed |= allowed == operation;
    if (!operationAllowed) {
        m_phase = Phase::PreviewRejected;
        m_error = makeError(ErrorCode::Unsupported,
                            QStringLiteral("The source did not allow this operation."));
        m_preview = {{QStringLiteral("accepted"), false},
                     {QStringLiteral("error"), m_error.message}};
        emit changed();
        emit rejected(m_sessionId, m_error.message, static_cast<int>(m_error.code));
        return false;
    }
    if (!target || target->stableTargetId().isEmpty() || target->viewId().isEmpty()
        || !target->snapshot().documentId.isValid()) {
        m_phase = Phase::PreviewRejected;
        m_error = makeError(ErrorCode::InvalidArgument,
                            QStringLiteral("Drop target identity or document snapshot is invalid."));
        m_preview = {{QStringLiteral("accepted"), false},
                     {QStringLiteral("error"), m_error.message}};
        emit changed();
        emit rejected(m_sessionId, m_error.message, static_cast<int>(m_error.code));
        return false;
    }
    const SnapshotHeader targetSnapshot = target->snapshot();
    const QString targetStableId = target->stableTargetId();
    const QString targetViewId = target->viewId();
    const quint64 targetViewGeneration = target->viewGeneration();
    const bool crossDocument = targetSnapshot.documentId != m_payload.sourceDocumentId;
    if (crossDocument && operation == TransferOperation::Move
        && !target->supportsAtomicCrossDocumentMove()) {
        m_phase = Phase::PreviewRejected;
        m_error = makeError(ErrorCode::Unsupported,
                            QStringLiteral("Cross-document Move requires an atomic source and destination adapter."));
        m_preview = {{QStringLiteral("accepted"), false},
                     {QStringLiteral("error"), m_error.message}};
        emit changed();
        emit rejected(m_sessionId, m_error.message, static_cast<int>(m_error.code));
        return false;
    }

    const QPointer<DropTarget> targetGuard(target);
    const TransferPayload payload = m_payload;
    const DropPreview proposed = target->previewDrop(activeSession, payload,
                                                       operation, location);
    if (m_sessionId != activeSession || m_phase != Phase::Dragging) {
        if (targetGuard && !proposed.token.isEmpty())
            targetGuard->cancelDrop(activeSession, proposed.token);
        return false;
    }
    if (!proposed.accepted || proposed.token.isEmpty()
        || (crossDocument && operation == TransferOperation::Move
            && !proposed.atomicCrossDocumentMove)) {
        if (targetGuard && !proposed.token.isEmpty())
            targetGuard->cancelDrop(m_sessionId, proposed.token);
        m_phase = Phase::PreviewRejected;
        const ErrorCode code = proposed.accepted ? ErrorCode::Unsupported
                                                  : proposed.errorCode;
        const QString message = proposed.accepted
            ? QStringLiteral("Target preview did not prove an atomic cross-document Move.")
            : (proposed.error.isEmpty() ? QStringLiteral("Drop target rejected the payload.")
                                        : proposed.error);
        m_error = makeError(code, message);
        m_preview = {{QStringLiteral("accepted"), false},
                     {QStringLiteral("summary"), proposed.summary},
                     {QStringLiteral("error"), message}};
        emit changed();
        emit rejected(m_sessionId, message, static_cast<int>(code));
        return false;
    }
    if (!sourceIsCurrent() || !targetGuard) {
        if (targetGuard) targetGuard->cancelDrop(m_sessionId, proposed.token);
        fail(ErrorCode::StaleRevision,
             QStringLiteral("Source or target expired during drop validation."), true);
        return false;
    }
    if (targetGuard->stableTargetId() != targetStableId
        || targetGuard->viewId() != targetViewId
        || targetGuard->viewGeneration() != targetViewGeneration
        || !sameHeader(targetGuard->snapshot(), targetSnapshot)) {
        targetGuard->cancelDrop(m_sessionId, proposed.token);
        fail(ErrorCode::StaleGeneration,
             QStringLiteral("Target changed while validating the drop."), true);
        return false;
    }
    m_target = targetGuard;
    m_targetId = targetStableId;
    m_targetViewId = targetViewId;
    m_targetViewGeneration = targetViewGeneration;
    m_targetSnapshot = targetSnapshot;
    m_previewOperation = operation;
    m_previewToken = proposed.token;
    m_preview = {{QStringLiteral("accepted"), true},
                 {QStringLiteral("operation"), operationName(operation)},
                 {QStringLiteral("targetId"), m_targetId},
                 {QStringLiteral("viewId"), m_targetViewId},
                 {QStringLiteral("viewGeneration"), QString::number(m_targetViewGeneration)},
                 {QStringLiteral("documentId"), m_targetSnapshot.documentId.toString()},
                 {QStringLiteral("revision"), m_targetSnapshot.revision.toDecimal()},
                 {QStringLiteral("summary"), proposed.summary}};
    m_error = {};
    m_phase = Phase::Preview;
    emit changed();
    return true;
}

QVariantMap DragDropService::previewEncodedDrop(QObject *target,
                                                const QString &operationNameValue,
                                                const QVariantMap &location)
{
    const auto operation = parseOperation(operationNameValue);
    auto *dropTarget = qobject_cast<DropTarget *>(target);
    if (!operation || !dropTarget) {
        fail(ErrorCode::InvalidArgument,
             QStringLiteral("Drop target or operation is invalid."));
        return m_preview;
    }
    previewDrop(dropTarget, *operation, location);
    return m_preview;
}

OperationError DragDropService::commitDrop()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_commitAttempted || m_phase == Phase::Committed)
        return makeError(ErrorCode::Unavailable,
                         QStringLiteral("Drop commit was already attempted."));
    if (m_phase != Phase::Preview) {
        fail(ErrorCode::Unavailable,
             QStringLiteral("Drop has no committable preview."));
        return m_error;
    }
    if (!sourceIsCurrent()) {
        fail(ErrorCode::StaleRevision,
             QStringLiteral("Source document changed after drop preview."), true);
        return m_error;
    }
    if (!targetIsCurrent()) {
        fail(ErrorCode::StaleGeneration,
             QStringLiteral("Drop target, document, or view changed after preview."), true);
        return m_error;
    }
    m_commitAttempted = true;
    stopAutoscroll();
    m_phase = Phase::Committing;
    emit changed();
    const QString completedSession = m_sessionId;
    const OperationError result = m_target->commitDrop(m_sessionId, m_previewToken);
    m_target = nullptr;
    m_previewToken.clear();
    if (result) {
        m_error = result;
        m_phase = Phase::Rejected;
        m_preview.insert(QStringLiteral("accepted"), false);
        m_preview.insert(QStringLiteral("error"), result.message);
        emit changed();
        emit rejected(completedSession, result.message, static_cast<int>(result.code));
        return result;
    }
    m_error = {};
    m_phase = Phase::Committed;
    emit changed();
    emit committed(completedSession);
    return {};
}

bool DragDropService::commit()
{
    return !commitDrop();
}

bool DragDropService::fail(ErrorCode code, const QString &message, bool endSession)
{
    const QString activeSession = m_sessionId;
    m_error = makeError(code, message);
    if (endSession) resetSession(Phase::Rejected, true);
    else {
        m_phase = Phase::PreviewRejected;
        emit changed();
    }
    emit rejected(activeSession, message, static_cast<int>(code));
    return false;
}

void DragDropService::clearPreview(bool notifyTarget)
{
    stopAutoscroll();
    if (notifyTarget && m_target && !m_previewToken.isEmpty())
        m_target->cancelDrop(m_sessionId, m_previewToken);
    m_target = nullptr;
    m_targetId.clear();
    m_targetViewId.clear();
    m_targetViewGeneration = 0;
    m_targetSnapshot = {};
    m_previewToken.clear();
    m_preview.clear();
}

void DragDropService::resetSession(Phase terminalPhase, bool notifyTarget)
{
    const QString oldSession = m_sessionId;
    m_phase = terminalPhase;
    clearPreview(notifyTarget);
    m_payload = {};
    m_allowed.clear();
    m_sourceViewId.clear();
    m_sourceViewGeneration = 0;
    m_sourceDocument = nullptr;
    m_hadSourceDocument = false;
    m_sourceSnapshot = {};
    m_sessionId.clear();
    m_commitAttempted = false;
    emit changed();
    if (terminalPhase == Phase::Cancelled)
        emit cancelled(oldSession);
}

void DragDropService::cancel()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_phase == Phase::Idle || m_phase == Phase::Committed
        || m_phase == Phase::Cancelled || m_phase == Phase::Rejected
        || m_phase == Phase::Committing)
        return;
    resetSession(Phase::Cancelled, true);
}

void DragDropService::invalidateView(const QString &viewIdValue)
{
    if (viewIdValue.isEmpty()) return;
    if (m_autoscrollViewId == viewIdValue) stopAutoscroll();
    if ((m_sourceViewId == viewIdValue || m_targetViewId == viewIdValue)
        && m_phase != Phase::Idle && m_phase != Phase::Committed
        && m_phase != Phase::Cancelled && m_phase != Phase::Rejected)
        fail(ErrorCode::StaleGeneration,
             QStringLiteral("Drag view was recycled or invalidated."), true);
}

void DragDropService::requestAutoscroll(const QString &viewIdValue,
                                        const QString &direction, bool active)
{
    static const QSet<QString> directions = {
        QStringLiteral("up"), QStringLiteral("down"),
        QStringLiteral("left"), QStringLiteral("right")};
    if (!active) {
        if (!viewIdValue.isEmpty() && viewIdValue != m_autoscrollViewId) return;
        stopAutoscroll();
        return;
    }
    if (m_phase != Phase::Preview || !m_target
        || viewIdValue != m_targetViewId || !directions.contains(direction)
        || !targetIsCurrent()) return;
    if (m_autoscrollViewId == viewIdValue
        && m_autoscrollDirection == direction) return;
    stopAutoscroll();
    m_autoscrollViewId = viewIdValue;
    m_autoscrollDirection = direction;
    emit autoscrollRequested(viewIdValue, direction, true);
}

void DragDropService::stopAutoscroll()
{
    if (m_autoscrollViewId.isEmpty()) return;
    const QString view = std::exchange(m_autoscrollViewId, {});
    const QString direction = std::exchange(m_autoscrollDirection, {});
    emit autoscrollRequested(view, direction, false);
}

} // namespace QindaTK
