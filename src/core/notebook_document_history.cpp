// SPDX-License-Identifier: LGPL-3.0-or-later
#include "notebook_document_p.h"
#include "undo_service.h"

namespace QindaTK {

// AGENT-CONTRACT (docs/document-models.md#commands, docs/notebooks.md#history):
// a notebook step is the state before and after, identified by stamps. Qt's
// implicit sharing keeps untouched pages and blocks shared between the live
// state and every recorded step, so a step retains only what it changed.
// prepareCommand compares stamps, never content, so large notebooks undo in
// constant time; a stamp is issued once per document lifetime.
struct NotebookDocument::NotebookCommand final : DocumentCommand {
    QString documentId;
    quint64 resetGeneration = 0;
    Notebook::State before, after;
    quint64 beforeStamp = 0, afterStamp = 0;
    quint64 bytes = 0;
    quint64 retainedBytes() const override { return bytes; }
    bool isNoOp() const override { return before == after; }
};

namespace {
quint64 pageBytes(const Notebook::Page &page)
{
    quint64 bytes = sizeof(Notebook::Page) + quint64(page.title.size()) * 2;
    for (const Notebook::Block &block : page.blocks)
        bytes += sizeof(Notebook::Block) + 96
            + quint64(block.data.value(QStringLiteral("text")).toString().size()) * 2;
    return bytes;
}
// Bytes a step keeps alive beyond the live state: the pages whose content
// differs between the two states (shared pages cost nothing).
quint64 changedBytes(const Notebook::State &before, const Notebook::State &after)
{
    QHash<QString, const Notebook::Page *> old;
    for (const Notebook::Page &page : before.pages) old.insert(page.id, &page);
    quint64 bytes = 256 + quint64(before.sections.size() + after.sections.size()) * 64
        + quint64(before.pages.size() + after.pages.size()) * 16;
    QSet<QString> seen;
    for (const Notebook::Page &page : after.pages) {
        seen.insert(page.id);
        const Notebook::Page *prior = old.value(page.id);
        if (!prior || prior->blocks.constData() != page.blocks.constData() || !(*prior == page))
            bytes += pageBytes(page) + (prior ? pageBytes(*prior) : 0);
    }
    for (const Notebook::Page &page : before.pages)
        if (!seen.contains(page.id)) bytes += pageBytes(page);
    return bytes;
}
} // namespace

UndoService *NotebookDocument::history() const
{
    if (auto *service = historyService()) return service;
    auto *self = const_cast<NotebookDocument *>(this);
    if (m_localHistory && m_localHistory->addModel(self)) m_localHistory->setStepLimit(self, 200);
    return historyService();
}

UndoService *NotebookDocument::undoService() const { return history(); }
bool NotebookDocument::canUndo() const { return history() && history()->canUndo(this); }
bool NotebookDocument::canRedo() const { return history() && history()->canRedo(this); }

void NotebookDocument::historyAttached(UndoService *service)
{
    disconnect(m_historyConnection);
    m_historyConnection = {};
    if (service)
        m_historyConnection = connect(service, &UndoService::historyChanged, this,
            [this](const QString &id) { if (id == m_documentId) emit historyChanged(); });
    emit historyChanged();
}

SnapshotHeader NotebookDocument::modelHeader() const
{
    return {QString::fromLatin1(SchemaId), DocumentId::parse(m_documentId), {m_revision}, m_resetGeneration};
}

QMetaObject::Connection NotebookDocument::connectModelChanged(QObject *receiver, std::function<void()> slot)
{
    return connect(this, &NotebookDocument::committedChanged, receiver, std::move(slot));
}

OperationError NotebookDocument::prepareCommand(const DocumentCommand &command, bool forward) const
{
    const auto *step = dynamic_cast<const NotebookCommand *>(&command);
    if (!step || step->documentId != m_documentId || step->resetGeneration != m_resetGeneration)
        return {ErrorCode::StaleGeneration, QStringLiteral("Notebook history belongs to another document"), {}, {}};
    if (m_stamp != (forward ? step->beforeStamp : step->afterStamp))
        return {ErrorCode::StaleRevision, QStringLiteral("Notebook changed outside the recorded step"), {}, {}};
    return {};
}

void NotebookDocument::publishCommand(const DocumentCommand &command, bool forward)
{
    const auto &step = static_cast<const NotebookCommand &>(command);
    m_state = forward ? step.after : step.before;
    m_stamp = forward ? step.afterStamp : step.beforeStamp;
    ++m_revision;
    m_indexValid = false;
    clearError();
    emit committedChanged();
    emit changed();
}

DocumentCommandPtr NotebookDocument::composeCommands(const DocumentCommand &first,
                                                     const DocumentCommand &second) const
{
    const auto *a = dynamic_cast<const NotebookCommand *>(&first);
    const auto *b = dynamic_cast<const NotebookCommand *>(&second);
    if (!a || !b || a->documentId != b->documentId || a->resetGeneration != b->resetGeneration
        || a->afterStamp != b->beforeStamp) return {};
    auto step = std::make_shared<NotebookCommand>();
    step->documentId = a->documentId;
    step->resetGeneration = a->resetGeneration;
    step->before = a->before;
    step->after = b->after;
    step->beforeStamp = a->beforeStamp;
    step->afterStamp = b->afterStamp;
    step->bytes = changedBytes(step->before, step->after);
    return step;
}

bool NotebookDocument::commit(const Notebook::State &after, const QString &label, const QString &mergeKey)
{
    if (after == m_state) {
        clearError();
        return true;
    }
    auto step = std::make_shared<NotebookCommand>();
    step->documentId = m_documentId;
    step->resetGeneration = m_resetGeneration;
    step->before = m_state;
    step->after = after;
    step->beforeStamp = m_stamp;
    step->afterStamp = m_nextStamp++;
    step->bytes = changedBytes(step->before, step->after);
    UndoService *service = history();
    if (!service) return fail(QStringLiteral("Notebook history is unavailable"));
    const OperationError error = service->commit(this, label, mergeKey, step);
    if (error) return fail(error.message);
    return true;
}

bool NotebookDocument::undo()
{
    UndoService *service = history();
    if (!service) return fail(QStringLiteral("Notebook history is unavailable"));
    const OperationError error = service->undo(this);
    return error ? fail(error.message) : true;
}

bool NotebookDocument::redo()
{
    UndoService *service = history();
    if (!service) return fail(QStringLiteral("Notebook history is unavailable"));
    const OperationError error = service->redo(this);
    return error ? fail(error.message) : true;
}

} // namespace QindaTK
