// SPDX-License-Identifier: LGPL-3.0-or-later
#include "edit_transaction.h"
#include <QSet>
#include <QSignalBlocker>
#include <QThread>

namespace QindaTK {
namespace {
OperationError failure(ErrorCode code, const QString &message, const QString &field = {})
{
    return {code, message, field, {}};
}
}

void EditTransaction::setDocument(PropertyDocument *document)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_document == document) return;
    cancel();
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) connect(document, &QObject::destroyed, this, [this] { finish(true); });
    emit configurationChanged();
}

void EditTransaction::setUndoService(UndoService *undo)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_undo == undo) return;
    cancel();
    m_undo = undo;
    emit configurationChanged();
}

void EditTransaction::setTargetIds(const QStringList &ids)
{
    if (m_targetIds == ids) return;
    cancel();
    m_targetIds = ids;
    emit configurationChanged();
}

void EditTransaction::setPropertyId(const QString &propertyId)
{
    if (m_propertyId == propertyId) return;
    cancel();
    m_propertyId = propertyId;
    emit configurationChanged();
}

void EditTransaction::setLabel(const QString &label)
{
    if (m_label == label) return;
    cancel();
    m_label = label;
    emit configurationChanged();
}

void EditTransaction::setMergeKey(const QString &mergeKey)
{
    if (m_mergeKey == mergeKey) return;
    cancel();
    m_mergeKey = mergeKey;
    emit configurationChanged();
}

void EditTransaction::setValidator(
    std::function<OperationError(const QVector<PropertyChange> &)> validator)
{
    m_validator = std::move(validator);
}

OperationError EditTransaction::reject(OperationError error, bool end)
{
    if (end) finish(true);
    m_error = error;
    emit stateChanged();
    emit rejected(error.message, static_cast<int>(error.code));
    return error;
}

void EditTransaction::finish(bool emitCancel)
{
    if (!m_active) return;
    const QString oldId = transactionId();
    if (m_document) m_document->discardPreview(m_id);
    m_active = false;
    m_changes.clear();
    m_id = {};
    emit stateChanged();
    if (emitCancel) emit canceled(oldId);
}

bool EditTransaction::begin(const QVector<EntityId> &targets,
                            const QString &propertyId, const QString &label,
                            const QString &mergeKey)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_active) { reject(failure(ErrorCode::Unavailable, QStringLiteral("Edit already active")), false); return false; }
    if (!m_document || !m_undo || !m_undo->history(m_document)
        || targets.isEmpty() || propertyId.isEmpty() || label.isEmpty()) {
        reject(failure(ErrorCode::InvalidArgument, QStringLiteral("Edit configuration is incomplete")), false);
        return false;
    }
    QSet<QString> seen;
    QVector<PropertyChange> changes;
    for (const auto &id : targets) {
        const QString key = id.toString();
        if (!id.isValid() || seen.contains(key)) {
            reject(failure(ErrorCode::InvalidArgument, QStringLiteral("Duplicate or invalid target")), false);
            return false;
        }
        seen.insert(key);
        const QVariant before = m_document->committedValue(id, propertyId);
        if (auto error = m_document->validate(id, propertyId, before)) {
            reject(error, false);
            return false;
        }
        changes.append({id, propertyId, before, before});
    }
    m_changes = changes;
    m_propertyId = propertyId;
    m_label = label;
    m_mergeKey = mergeKey;
    m_base = m_document->header();
    m_id = QUuid::createUuid();
    m_active = true;
    m_error = {};
    emit stateChanged();
    return true;
}

bool EditTransaction::begin()
{
    QVector<EntityId> targets;
    targets.reserve(m_targetIds.size());
    for (const auto &id : m_targetIds) targets.append(EntityId::parse(id));
    return begin(targets, m_propertyId, m_label, m_mergeKey);
}

OperationError EditTransaction::updateValue(const QVariant &candidate)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!m_active || !m_document)
        return reject(failure(ErrorCode::Unavailable, QStringLiteral("No active edit")), true);
    const SnapshotHeader current = m_document->header();
    if (current.resetGeneration != m_base.resetGeneration)
        return reject(failure(ErrorCode::StaleGeneration, QStringLiteral("Document model was reset")), true);
    if (current.revision != m_base.revision)
        return reject(failure(ErrorCode::StaleRevision, QStringLiteral("Document changed during edit")), true);
    auto proposed = m_changes;
    for (auto &change : proposed) change.after = candidate;
    if (auto error = m_document->showPreview(m_id, proposed)) return reject(error, false);
    m_changes = proposed;
    m_error = {};
    emit stateChanged();
    return {};
}

OperationError EditTransaction::commitEdit()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!m_active || !m_document || !m_undo)
        return reject(failure(ErrorCode::Unavailable, QStringLiteral("No active edit")), true);
    const auto current = m_document->header();
    if (current.resetGeneration != m_base.resetGeneration)
        return reject(failure(ErrorCode::StaleGeneration, QStringLiteral("Document model was reset")), true);
    if (current.revision != m_base.revision)
        return reject(failure(ErrorCode::StaleRevision, QStringLiteral("Document changed during edit")), true);
    QVector<PropertyChange> effective;
    for (const auto &change : m_changes) {
        if (auto error = m_document->validate(change.target, change.propertyId, change.after))
            return reject(error, true);
        if (!m_document->equalValue(change.before, change.after)) effective.append(change);
    }
    if (m_validator) {
        if (auto error = m_validator(effective)) return reject(error, true);
    }
    if (!m_undo->canRecord(m_document, effective))
        return reject(failure(ErrorCode::Unavailable, QStringLiteral("Edit exceeds undo memory limit")), true);
    const QString completedId = transactionId();
    const QVector<PropertyChange> presentationChanges = m_changes;
    {
        // AGENT-GUARD: observers must see both authoritative values and their
        // inverse step together. Hold notifications until both are published.
        QSignalBlocker documentSignals(m_document.data());
        QSignalBlocker undoSignals(m_undo.data());
        m_document->discardPreview(m_id);
        if (!effective.isEmpty()) {
            if (auto error = m_document->applyCommitted(effective, true))
                return reject(error, true);
            // canRecord preflight and blocked reentrant observers keep this
            // deterministic on the GUI thread.
            const bool recorded = m_undo->record(m_document, m_label, m_mergeKey, effective);
            Q_ASSERT(recorded);
            Q_UNUSED(recorded)
        }
        m_active = false;
        m_changes.clear();
        m_id = {};
        m_error = {};
    }
    for (const auto &change : presentationChanges) {
        if (!m_document) break;
        emit m_document->propertyChanged(change.target.toString(), change.propertyId);
    }
    if (m_document) emit m_document->changed();
    if (!effective.isEmpty() && m_undo) emit m_undo->changed();
    emit stateChanged();
    emit committed(completedId);
    return {};
}

void EditTransaction::cancel()
{
    Q_ASSERT(thread() == QThread::currentThread());
    finish(true);
}

} // namespace QindaTK
