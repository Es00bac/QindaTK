// SPDX-License-Identifier: LGPL-3.0-or-later
#include "undo_service.h"
#include "workbook_format.h"
#include "workbook_p.h"

// The Workbook as a DocumentModel (docs/document-models.md): its steps are
// WorkbookCommands recorded by the UndoService that holds its single live
// history, the private one while standalone or a session's shared one.
namespace QindaTK {
using namespace Calc;
namespace {
// AGENT-NOTE: a transaction of many cell edits records one SetCells mutation
// per edit. Adjacent ones on the same sheet merge into one without changing
// the result, because apply() assigns entries in order (inverse entries are
// already reversed). One mutation then holds the cells, not one per cell.
MutationList coalescedCells(MutationList list)
{
    MutationList out;
    out.reserve(list.size());
    for (Mutation &m : list) {
        Mutation *last = out.empty() ? nullptr : &out.back();
        if (last && m.kind == Mutation::Kind::SetCells && last->kind == Mutation::Kind::SetCells
            && last->sheetId == m.sheetId) {
            last->cells.insert(last->cells.end(), std::make_move_iterator(m.cells.begin()),
                               std::make_move_iterator(m.cells.end()));
            continue;
        }
        out.push_back(std::move(m));
    }
    out.shrink_to_fit();
    return out;
}
} // namespace

WorkbookCommand::WorkbookCommand(QString documentId, quint64 resetGeneration, MutationList forward,
                                 MutationList backward)
    : documentId(std::move(documentId)), resetGeneration(resetGeneration),
      forward(coalescedCells(std::move(forward))), backward(coalescedCells(std::move(backward)))
{
    // The forward list shares sheet states, features and names with the live
    // document when it is recorded, so only its cell data is counted.
    m_bytes = quint64(sizeof(WorkbookCommand)) + quint64(Calc::retainedBytes(this->backward))
        + quint64(Calc::retainedBytes(this->forward, true));
}

DocumentId Workbook::modelId() const { return DocumentId::parse(d->documentId); }

SnapshotHeader Workbook::modelHeader() const
{
    return {QString::fromLatin1(WorkbookFormat::FormatId), DocumentId::parse(d->documentId), {d->revision},
            d->resetGeneration};
}

QMetaObject::Connection Workbook::connectModelChanged(QObject *receiver, std::function<void()> slot)
{
    // Every commit, undo, redo, restore and reset moves the revision.
    return connect(this, &Workbook::revisionChanged, receiver, std::move(slot));
}

OperationError Workbook::prepareCommand(const DocumentCommand &command, bool forward) const
{
    const auto *step = dynamic_cast<const WorkbookCommand *>(&command);
    if (!step) return {ErrorCode::InvalidArgument, tr("The step belongs to another document kind"), {}, {}};
    if (step == d->pendingCommit) return {}; // its operation already applied and validated it
    Q_UNUSED(forward)
    if (d->txn) return {ErrorCode::Unavailable, tr("Finish the active workbook transaction first"), {}, {}};
    if (step->documentId != d->documentId || step->resetGeneration != d->resetGeneration)
        return {ErrorCode::StaleGeneration, tr("The workbook was replaced after this step was recorded"), {}, {}};
    return {};
}

void Workbook::publishCommand(const DocumentCommand &command, bool forward)
{
    const auto *step = dynamic_cast<const WorkbookCommand *>(&command);
    Q_ASSERT(step);
    if (!step) return;
    if (step == d->pendingCommit) d->finishCommit();
    else d->replayCommand(*step, forward);
}

DocumentCommandPtr Workbook::composeCommands(const DocumentCommand &first, const DocumentCommand &second) const
{
    const auto *a = dynamic_cast<const WorkbookCommand *>(&first);
    const auto *b = dynamic_cast<const WorkbookCommand *>(&second);
    if (!a || !b || a->documentId != b->documentId || a->resetGeneration != b->resetGeneration) return {};
    MutationList forward = a->forward;
    forward.insert(forward.end(), b->forward.begin(), b->forward.end());
    MutationList backward = b->backward;
    backward.insert(backward.end(), a->backward.begin(), a->backward.end());
    return std::make_shared<const WorkbookCommand>(a->documentId, a->resetGeneration, std::move(forward),
                                                   std::move(backward));
}

void Workbook::historyAttached(UndoService *service)
{
    disconnect(d->historyConnection);
    d->historyConnection = {};
    if (service)
        d->historyConnection = connect(service, &UndoService::historyChanged, this, [this](const QString &id) {
            if (id == d->documentId) emit historyChanged();
        });
    emit historyChanged();
}

UndoService *Workbook::history() const
{
    if (UndoService *service = historyService()) return service;
    // A session released the workbook (it closed): continue on the private service.
    d->attachLocalHistory();
    return historyService();
}

void WorkbookPrivate::attachLocalHistory()
{
    if (!localHistory || q->historyService()) return;
    if (localHistory->addModel(q)) localHistory->setStepLimit(q, Workbook::MaxHistorySteps);
}

UndoService *Workbook::undoService() const { return history(); }
bool Workbook::canUndo() const
{
    const UndoService *service = history();
    return !d->txn && service && service->canUndo(this);
}
bool Workbook::canRedo() const
{
    const UndoService *service = history();
    return !d->txn && service && service->canRedo(this);
}
QString Workbook::undoText() const
{
    const UndoService *service = history();
    return service ? service->undoText(this) : QString();
}
QString Workbook::redoText() const
{
    const UndoService *service = history();
    return service ? service->redoText(this) : QString();
}
bool Workbook::isModified() const
{
    const UndoService *service = history();
    return !service || !service->isModelClean(this);
}

bool Workbook::undo()
{
    if (d->txn) return d->fail(tr("Finish the active workbook transaction first"));
    UndoService *service = history();
    if (!service) return d->fail(tr("The workbook has no history service"));
    if (const OperationError problem = service->undo(this)) return d->fail(problem.message);
    return true;
}
bool Workbook::redo()
{
    if (d->txn) return d->fail(tr("Finish the active workbook transaction first"));
    UndoService *service = history();
    if (!service) return d->fail(tr("The workbook has no history service"));
    if (const OperationError problem = service->redo(this)) return d->fail(problem.message);
    return true;
}
void Workbook::markSaved()
{
    if (UndoService *service = history()) service->markModelClean(this);
}
void Workbook::markUnsaved() { invalidateCleanState(); }

void WorkbookPrivate::finishCommit()
{
    Transaction t = std::move(*txn);
    txn.reset();
    QHash<quint32, Bounds> changed = t.touched;
    recalculate(t.seeds, &changed);
    ++revision;
    finish(t.label, true, changed, t);
}

void WorkbookPrivate::replayCommand(const WorkbookCommand &command, bool forward)
{
    txn.emplace();
    for (const Mutation &m : forward ? command.forward : command.backward) {
        QString error;
        if (record(m, &error)) continue;
        // AGENT-GUARD: prepareCommand accepted this step for the current
        // document and reset generation, so a failure is a lost invariant.
        // Nothing stays applied, and no recorded step may replay again.
        rollbackTo(0);
        const QSet<quint64> seeds = txn->seeds;
        txn.reset();
        if (!seeds.isEmpty()) recalculate(seeds, nullptr);
        fail(Workbook::tr("History replay failed: %1").arg(error));
        q->invalidateHistory();
        return;
    }
    Transaction t = std::move(*txn);
    txn.reset();
    QHash<quint32, Bounds> changed = t.touched;
    recalculate(t.seeds, &changed);
    ++revision;
    clearError();
    finish(t.label, true, changed, t);
}

} // namespace QindaTK
