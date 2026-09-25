// SPDX-License-Identifier: LGPL-3.0-or-later
#include "transfer_function_model.h"
#include "undo_service.h"

namespace QindaTK {
// AGENT-CONTRACT (docs/document-models.md#commands, D-148): one typed step is
// the complete stop list before and after the edit (at most 256 stops), tied
// to this model's document ID and reset generation so a step recorded before
// configure()/restoreSettings() can never apply to the reloaded function.
struct TransferFunctionModel::TransferCommand final : DocumentCommand {
    QString documentId;
    quint64 resetGeneration = 0;
    QVector<Point> before, after;
    TransferCommand(QString id, quint64 generation, QVector<Point> oldPoints, QVector<Point> newPoints)
        : documentId(std::move(id)), resetGeneration(generation),
          before(std::move(oldPoints)), after(std::move(newPoints)) {}
    quint64 retainedBytes() const override
    {
        return sizeof(*this) + quint64(before.size() + after.size()) * (sizeof(Point) + 64);
    }
    bool isNoOp() const override { return before == after; }
};

UndoService *TransferFunctionModel::history() const
{
    if (auto *service = historyService()) return service;
    // A session released the history: fall back to the private service.
    auto *self = const_cast<TransferFunctionModel *>(this);
    if (m_localHistory && m_localHistory->addModel(self)) m_localHistory->setStepLimit(self, 128);
    return historyService();
}
UndoService *TransferFunctionModel::undoService() const { return history(); }
bool TransferFunctionModel::canUndo() const { return !m_editing && history() && history()->canUndo(this); }
bool TransferFunctionModel::canRedo() const { return !m_editing && history() && history()->canRedo(this); }
QString TransferFunctionModel::undoText() const { return history() ? history()->undoText(this) : QString(); }
QString TransferFunctionModel::redoText() const { return history() ? history()->redoText(this) : QString(); }

void TransferFunctionModel::historyAttached(UndoService *service)
{
    disconnect(m_historyConnection);
    m_historyConnection = {};
    if (service) m_historyConnection = connect(service, &UndoService::historyChanged, this,
        [this](const QString &id) { if (id == m_documentId) emit historyChanged(); });
    emit historyChanged();
}
SnapshotHeader TransferFunctionModel::modelHeader() const
{
    return {QStringLiteral("qindatk.transfer-function.v1"), DocumentId::parse(m_documentId),
            {m_revision}, m_resetGeneration};
}
QMetaObject::Connection TransferFunctionModel::connectModelChanged(QObject *receiver,
                                                                   std::function<void()> slot)
{
    return connect(this, &TransferFunctionModel::committed, receiver, std::move(slot));
}
OperationError TransferFunctionModel::prepareCommand(const DocumentCommand &command, bool forward) const
{
    const auto *step = dynamic_cast<const TransferCommand *>(&command);
    if (!step || step->documentId != m_documentId || step->resetGeneration != m_resetGeneration)
        return {ErrorCode::StaleGeneration, QStringLiteral("Transfer history belongs to another function"), {}, {}};
    if (m_editing)
        return {ErrorCode::Unavailable, QStringLiteral("Finish the transfer edit first"), {}, {}};
    if (m_points != (forward ? step->before : step->after))
        return {ErrorCode::StaleRevision, QStringLiteral("Transfer function changed outside the recorded step"), {}, {}};
    return {};
}
void TransferFunctionModel::publishCommand(const DocumentCommand &command, bool forward)
{
    const auto &step = static_cast<const TransferCommand &>(command);
    m_points = forward ? step.after : step.before;
    publish();
    emit committed();
}
DocumentCommandPtr TransferFunctionModel::composeCommands(const DocumentCommand &first,
                                                          const DocumentCommand &second) const
{
    const auto *a = dynamic_cast<const TransferCommand *>(&first);
    const auto *b = dynamic_cast<const TransferCommand *>(&second);
    if (!a || !b || a->documentId != b->documentId || a->resetGeneration != b->resetGeneration
        || a->after != b->before) return {};
    return std::make_shared<const TransferCommand>(a->documentId, a->resetGeneration, a->before, b->after);
}

bool TransferFunctionModel::apply(QVector<Point> after, const QString &label)
{
    if (m_editing) { m_points = std::move(after); publish(); return true; }
    if (after == m_points) { fail({}); return true; }
    const auto step = std::make_shared<const TransferCommand>(m_documentId, m_resetGeneration,
                                                              m_points, std::move(after));
    UndoService *service = history();
    if (!service) { fail(QStringLiteral("Transfer history is unavailable")); return false; }
    if (const OperationError error = service->commit(this, label, {}, step)) {
        fail(error.message); return false;
    }
    return true;
}
void TransferFunctionModel::resetHistory()
{
    if (m_editing) { m_editing = false; m_editBefore.clear(); emit editingChanged(); }
    ++m_resetGeneration;
    invalidateHistory();
    emit historyChanged();
}
bool TransferFunctionModel::beginEdit(const QString &label)
{
    if (m_editing) { fail(QStringLiteral("A transfer edit is already open")); return false; }
    m_editBefore = m_points;
    m_editLabel = label.isEmpty() ? tr("Edit transfer function") : label;
    m_editing = true;
    fail({});
    emit editingChanged(); emit historyChanged();
    return true;
}
bool TransferFunctionModel::commitEdit()
{
    if (!m_editing) { fail(QStringLiteral("No transfer edit is open")); return false; }
    QVector<Point> after = m_points;
    m_points = m_editBefore;
    m_editBefore.clear();
    m_editing = false;
    emit editingChanged();
    // AGENT-GUARD: m_points is back at the gesture's start before apply(), so
    // prepareCommand() sees `before` and the step undoes to the pre-drag state.
    const bool previewed = after != m_points;
    const bool ok = apply(std::move(after), m_editLabel);
    if (!ok && previewed) publish(); // views showed the preview; show the restored state
    emit historyChanged();
    return ok;
}
bool TransferFunctionModel::cancelEdit()
{
    if (!m_editing) { fail(QStringLiteral("No transfer edit is open")); return false; }
    const bool changed = m_points != m_editBefore;
    m_points = m_editBefore;
    m_editBefore.clear();
    m_editing = false;
    if (changed) publish(); else fail({});
    emit editingChanged(); emit historyChanged();
    return true;
}
bool TransferFunctionModel::undo()
{
    if (m_editing) { fail(QStringLiteral("Finish the transfer edit first")); return false; }
    UndoService *service = history();
    if (!service) return false;
    if (const OperationError error = service->undo(this)) { fail(error.message); return false; }
    return true;
}
bool TransferFunctionModel::redo()
{
    if (m_editing) { fail(QStringLiteral("Finish the transfer edit first")); return false; }
    UndoService *service = history();
    if (!service) return false;
    if (const OperationError error = service->redo(this)) { fail(error.message); return false; }
    return true;
}
} // namespace QindaTK
