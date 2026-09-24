// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scene_model_3d.h"
#include "undo_service.h"

namespace QindaTK {
// AGENT-CONTRACT (docs/document-models.md#commands): scene edits are bounded
// snapshots today (512 primitives); a session adopts this single history.
struct SceneModel3D::SceneCommand final : DocumentCommand {
    QString documentId;
    quint64 resetGeneration = 0;
    QVector<Entity> before, after;
    SceneCommand(QString id, quint64 generation, QVector<Entity> oldState, QVector<Entity> newState)
        : documentId(std::move(id)), resetGeneration(generation),
          before(std::move(oldState)), after(std::move(newState)) {}
    quint64 retainedBytes() const override {
        return sizeof(*this) + quint64(before.size() + after.size()) * (sizeof(Entity) + 128);
    }
    bool isNoOp() const override { return before == after; }
};

UndoService *SceneModel3D::history() const
{
    if (auto *service = historyService()) return service;
    auto *self = const_cast<SceneModel3D *>(this);
    if (m_localHistory && m_localHistory->addModel(self)) m_localHistory->setStepLimit(self, 64);
    return historyService();
}
UndoService *SceneModel3D::undoService() const { return history(); }
bool SceneModel3D::canUndo() const { return !editing() && history() && history()->canUndo(this); }
bool SceneModel3D::canRedo() const { return !editing() && history() && history()->canRedo(this); }

void SceneModel3D::historyAttached(UndoService *service)
{
    disconnect(m_historyConnection);
    m_historyConnection = {};
    if (service) m_historyConnection = connect(service, &UndoService::historyChanged, this,
        [this](const QString &id) { if (id == m_documentId) emit historyChanged(); });
    emit historyChanged();
}
SnapshotHeader SceneModel3D::modelHeader() const
{
    return {QStringLiteral("qindatk.scene3d.v1"), DocumentId::parse(m_documentId),
            {m_revision}, m_resetGeneration};
}
QMetaObject::Connection SceneModel3D::connectModelChanged(QObject *receiver, std::function<void()> slot)
{
    return connect(this, &SceneModel3D::committedChanged, receiver, std::move(slot));
}
OperationError SceneModel3D::prepareCommand(const DocumentCommand &command, bool forward) const
{
    const auto *step = dynamic_cast<const SceneCommand *>(&command);
    if (!step || step->documentId != m_documentId || step->resetGeneration != m_resetGeneration)
        return {ErrorCode::StaleGeneration, QStringLiteral("Scene history belongs to another document"), {}, {}};
    if (editing()) return {ErrorCode::Unavailable, QStringLiteral("Finish the scene transform first"), {}, {}};
    if (m_entities != (forward ? step->before : step->after))
        return {ErrorCode::StaleRevision, QStringLiteral("Scene changed outside the recorded step"), {}, {}};
    return {};
}
void SceneModel3D::publishCommand(const DocumentCommand &command, bool forward)
{
    const auto &step = static_cast<const SceneCommand &>(command);
    m_entities = forward ? step.after : step.before;
    ++m_revision;
    if (!m_selected.isEmpty() && index(m_selected) < 0) {
        m_selected.clear(); emit selectionChanged();
    }
    m_error.clear(); emit errorChanged(); emit committedChanged(); emit changed();
}
DocumentCommandPtr SceneModel3D::composeCommands(const DocumentCommand &first,
                                                  const DocumentCommand &second) const
{
    const auto *a = dynamic_cast<const SceneCommand *>(&first);
    const auto *b = dynamic_cast<const SceneCommand *>(&second);
    if (!a || !b || a->documentId != b->documentId || a->resetGeneration != b->resetGeneration
        || a->after != b->before) return {};
    return std::make_shared<const SceneCommand>(a->documentId, a->resetGeneration, a->before, b->after);
}
bool SceneModel3D::commitState(const QVector<Entity> &before, const QVector<Entity> &after,
                               const QString &label)
{
    if (before == after) {
        m_entities = after;
        m_error.clear(); emit errorChanged(); emit changed();
        return true;
    }
    m_entities = before;
    const auto step = std::make_shared<const SceneCommand>(m_documentId, m_resetGeneration, before, after);
    const auto error = history()->commit(this, label, {}, step);
    if (error) return fail(error.message);
    return true;
}
} // namespace QindaTK
