// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_document.h"
#include "undo_service.h"

namespace QindaTK {

// AGENT-CONTRACT (docs/document-models.md#commands): one mesh edit or one
// complete paint stroke is one immutable before/after snapshot step. Tokens
// name committed contents, so a step applies only to the exact state it was
// recorded against; revision still increments on every publish.
struct MeshRigDocument::MeshCommand final : DocumentCommand {
    QString documentId;
    quint64 resetGeneration = 0;
    State before, after;
    quint64 beforeToken = 0, afterToken = 0;
    MeshCommand(QString id, quint64 generation, State oldState, State newState,
                quint64 oldToken, quint64 newToken)
        : documentId(std::move(id)), resetGeneration(generation),
          before(std::move(oldState)), after(std::move(newState)),
          beforeToken(oldToken), afterToken(newToken) {}
    quint64 retainedBytes() const override {
        quint64 bytes = sizeof(*this)
            + quint64(before.vertices.size() + after.vertices.size()) * 160
            + quint64(before.faces.size() + after.faces.size()) * 320
            + quint64(before.bones.size() + after.bones.size()) * 512;
        // Unchanged channels share pixel storage with the live document.
        for (int c = 0; c < ChannelCount; ++c)
            if (before.textures[c].cacheKey() != after.textures[c].cacheKey())
                bytes += quint64(before.textures[c].sizeInBytes() + after.textures[c].sizeInBytes());
        return bytes;
    }
};

UndoService *MeshRigDocument::history() const
{
    if (auto *service = historyService()) return service;
    auto *self = const_cast<MeshRigDocument *>(this);
    if (m_localHistory && m_localHistory->addModel(self)) m_localHistory->setStepLimit(self, 16);
    return historyService();
}
UndoService *MeshRigDocument::undoService() const { return history(); }
bool MeshRigDocument::canUndo() const { return !m_painting && history() && history()->canUndo(this); }
bool MeshRigDocument::canRedo() const { return !m_painting && history() && history()->canRedo(this); }
bool MeshRigDocument::undo()
{
    if (!canUndo()) return fail("No undo step available");
    const auto error = history()->undo(this);
    return error ? fail(error.message) : true;
}
bool MeshRigDocument::redo()
{
    if (!canRedo()) return fail("No redo step available");
    const auto error = history()->redo(this);
    return error ? fail(error.message) : true;
}

void MeshRigDocument::historyAttached(UndoService *service)
{
    disconnect(m_historyConnection);
    m_historyConnection = {};
    if (service) m_historyConnection = connect(service, &UndoService::historyChanged, this,
        [this](const QString &id) { if (id == m_documentId) emit changed(); });
    emit historyChanged();
    emit changed();
}
SnapshotHeader MeshRigDocument::modelHeader() const
{
    return {QStringLiteral("qindatk.mesh-rig.v2"), DocumentId::parse(m_documentId),
            {m_revision}, m_resetGeneration};
}
QMetaObject::Connection MeshRigDocument::connectModelChanged(QObject *receiver, std::function<void()> slot)
{
    return connect(this, &MeshRigDocument::committedChanged, receiver, std::move(slot));
}
OperationError MeshRigDocument::prepareCommand(const DocumentCommand &command, bool forward) const
{
    const auto *step = dynamic_cast<const MeshCommand *>(&command);
    if (!step || step->documentId != m_documentId || step->resetGeneration != m_resetGeneration)
        return {ErrorCode::StaleGeneration, QStringLiteral("Mesh history belongs to another document"), {}, {}};
    if (m_painting)
        return {ErrorCode::Unavailable, QStringLiteral("Finish the paint stroke first"), {}, {}};
    if (m_token != (forward ? step->beforeToken : step->afterToken))
        return {ErrorCode::StaleRevision, QStringLiteral("Mesh changed outside the recorded step"), {}, {}};
    return {};
}
void MeshRigDocument::publishCommand(const DocumentCommand &command, bool forward)
{
    const auto &step = static_cast<const MeshCommand &>(command);
    m_state = forward ? step.after : step.before;
    m_token = forward ? step.afterToken : step.beforeToken;
    ++m_revision;
    emit committedChanged();
    notify();
}
DocumentCommandPtr MeshRigDocument::composeCommands(const DocumentCommand &first,
                                                    const DocumentCommand &second) const
{
    const auto *a = dynamic_cast<const MeshCommand *>(&first);
    const auto *b = dynamic_cast<const MeshCommand *>(&second);
    if (!a || !b || a->documentId != b->documentId || a->resetGeneration != b->resetGeneration
        || a->afterToken != b->beforeToken) return {};
    return std::make_shared<const MeshCommand>(a->documentId, a->resetGeneration, a->before,
                                               b->after, a->beforeToken, b->afterToken);
}
bool MeshRigDocument::commit(const State &next, const QString &label)
{
    auto *service = history();
    if (!service) return fail("Mesh history is unavailable");
    const auto step = std::make_shared<const MeshCommand>(m_documentId, m_resetGeneration, m_state,
                                                          next, m_token, ++m_nextToken);
    // AGENT-GUARD: inside beginEditGesture/endEditGesture every commit shares
    // one merge key, so a slider drag is one undo step and cannot evict older
    // edits from the bounded history; outside a gesture each edit is its own.
    const QString mergeKey = m_gestureOpen
        ? QStringLiteral("meshrig-gesture-%1").arg(m_gestureSerial) : QString();
    const auto error = service->commit(this, label, mergeKey, step);
    if (error) return fail(error.message);
    return true;
}

} // namespace QindaTK
