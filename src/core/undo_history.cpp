// SPDX-License-Identifier: LGPL-3.0-or-later
// Generic history engine: per-document steps, clean marks, budgets and the
// single-live-history ownership rule. The QML facade is in undo_service.cpp.
#include "undo_service.h"
#include <QThread>
#include <QUuid>

namespace QindaTK {
namespace {
OperationError failure(ErrorCode code, const QString &message)
{
    return {code, message, {}, QStringLiteral("history")};
}
QString newStepId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
} // namespace

DocumentModel::~DocumentModel()
{
    // AGENT-GUARD: runs before ~QObject of the model, while the derived model
    // is already gone. The service must not call back into this model, so the
    // removal is silent here and observers hear about it on the next turn.
    if (auto *service = historyService()) service->forget(m_historyKey, this, false);
}

UndoService *DocumentModel::historyService() const
{
    return qobject_cast<UndoService *>(m_history.data());
}

void DocumentModel::invalidateCleanState()
{
    if (auto *service = historyService()) service->invalidateCleanKey(m_historyKey);
}

void DocumentModel::invalidateHistory()
{
    if (auto *service = historyService()) service->invalidateKey(m_historyKey);
}

DocumentCommandPtr DocumentModel::composeCommands(const DocumentCommand &,
                                                  const DocumentCommand &) const
{
    return {};
}

UndoService::~UndoService()
{
    for (auto it = m_histories.begin(); it != m_histories.end(); ++it) {
        disconnect(it->destroyed);
        if (it->model) {
            it->model->m_history.clear();
            it->model->m_historyKey.clear();
        }
    }
}

UndoService::History *UndoService::historyFor(const DocumentModel *model)
{
    if (!model || model->m_history.data() != this) return nullptr;
    auto it = m_histories.find(model->m_historyKey);
    return it != m_histories.end() && it->model == model && it->object ? &it.value() : nullptr;
}

const UndoService::History *UndoService::historyFor(const DocumentModel *model) const
{
    if (!model || model->m_history.data() != this) return nullptr;
    auto it = m_histories.constFind(model->m_historyKey);
    return it != m_histories.cend() && it->model == model && it->object ? &it.value() : nullptr;
}

const UndoService::History *UndoService::activeHistory() const
{
    if (m_activeKey.isEmpty() || !m_activeObject) return nullptr;
    auto it = m_histories.constFind(m_activeKey);
    return it != m_histories.cend() && it->object == m_activeObject ? &it.value() : nullptr;
}

bool UndoService::hasModel(const DocumentModel *model) const
{
    return historyFor(model) != nullptr;
}

void UndoService::notify(const QString &key)
{
    emit historyChanged(key);
    emit changed();
}

void UndoService::attach(DocumentModel *model, History state)
{
    Q_ASSERT(!model->historyService());
    const QString key = model->modelId().toString();
    QObject *object = model->modelObject();
    state.model = model;
    state.object = object;
    state.busy = false;
    state.destroyed = connect(object, &QObject::destroyed, this,
                              [this, key] { forget(key, nullptr, true); });
    model->m_history = this;
    model->m_historyKey = key;
    m_histories.insert(key, std::move(state));
    if (!activeHistory()) {
        m_activeKey = key;
        m_activeObject = object;
    }
    model->historyAttached(this);
}

bool UndoService::addModel(DocumentModel *model)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!model) return false;
    QObject *object = model->modelObject();
    if (!object || object->thread() != thread()) return false;
    if (model->m_history.data() == this) return historyFor(model) != nullptr;
    // AGENT-GUARD: one live history per model. Two services recording the
    // same model would each replay inverse data the other already changed.
    if (model->historyService()) return false;
    const DocumentId id = model->modelId();
    if (!id.isValid() || m_histories.contains(id.toString())) return false;
    History state;
    state.cleanRevision = model->modelHeader().revision.value;
    attach(model, std::move(state));
    notify(id.toString());
    return true;
}

bool UndoService::adoptModel(DocumentModel *model)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!model) return false;
    UndoService *owner = model->historyService();
    if (owner == this) return historyFor(model) != nullptr;
    if (!owner) return addModel(model);
    QObject *object = model->modelObject();
    const QString key = model->modelId().toString();
    if (!object || object->thread() != thread() || m_histories.contains(key)) return false;
    History moved;
    // AGENT-GUARD: nothing may observe the model between release and attach;
    // a binding that asked for its history there would re-attach it to its
    // standalone service and leave two live histories. Notify afterwards.
    if (!owner->release(model, &moved, false)) return false;
    attach(model, std::move(moved));
    enforceLimits(m_histories[key]);
    owner->notify(key);
    notify(key);
    return true;
}

bool UndoService::release(DocumentModel *model, History *state, bool notifyNow)
{
    auto *entry = historyFor(model);
    if (!entry || entry->busy || !entry->macroLabel.isEmpty()) return false;
    const QString key = model->m_historyKey;
    disconnect(entry->destroyed);
    *state = std::move(*entry);
    m_histories.remove(key);
    model->m_history.clear();
    model->m_historyKey.clear();
    if (m_activeKey == key) {
        m_activeKey.clear();
        m_activeObject.clear();
    }
    model->historyAttached(nullptr);
    if (notifyNow) notify(key);
    return true;
}

bool UndoService::removeModel(DocumentModel *model)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!historyFor(model)) return false;
    forget(model->m_historyKey, model, true);
    model->m_history.clear();
    model->m_historyKey.clear();
    model->historyAttached(nullptr);
    return true;
}

void UndoService::forget(const QString &key, const DocumentModel *model, bool notifyNow)
{
    auto it = m_histories.find(key);
    if (it == m_histories.end() || (model && it->model != model)) return;
    disconnect(it->destroyed);
    m_histories.erase(it);
    if (m_activeKey == key) {
        m_activeKey.clear();
        m_activeObject.clear();
    }
    if (notifyNow) notify(key);
    else QMetaObject::invokeMethod(this, [this, key] { notify(key); }, Qt::QueuedConnection);
}

void UndoService::invalidateKey(const QString &key)
{
    auto it = m_histories.find(key);
    if (it == m_histories.end()) return;
    it->steps.clear();
    it->cursor = 0;
    it->cleanCursor = -1;
    it->macroLabel.clear();
    it->macroCommand.reset();
    it->evicted = 0;
    ++it->epoch;
    notify(key);
}

void UndoService::invalidateCleanKey(const QString &key)
{
    auto it = m_histories.find(key);
    if (it == m_histories.end()) return;
    // Every cursor position now maps to different content, including the
    // positions pending saves captured.
    it->cleanCursor = -1;
    ++it->epoch;
    notify(key);
}

OperationError UndoService::canRecord(DocumentModel *model, const DocumentCommand &command) const
{
    const History *state = historyFor(model);
    if (!state) return failure(ErrorCode::Unavailable, QStringLiteral("Document history is not in this service"));
    if (state->busy) return failure(ErrorCode::Unavailable, QStringLiteral("A history operation is in progress"));
    quint64 bytes = command.retainedBytes();
    if (!state->macroLabel.isEmpty() && state->macroCommand) {
        const DocumentCommandPtr combined = model->composeCommands(*state->macroCommand, command);
        if (!combined) return failure(ErrorCode::Unsupported, QStringLiteral("This document cannot group these edits"));
        bytes = combined->retainedBytes();
    }
    if (bytes > m_memoryLimit)
        return failure(ErrorCode::Unavailable, QStringLiteral("Edit exceeds undo memory limit"));
    return {};
}

void UndoService::recordStep(History &state, DocumentModel *model, const QString &label,
                             const QString &mergeKey, const DocumentCommandPtr &command)
{
    if (!state.macroLabel.isEmpty()) {
        state.macroCommand = state.macroCommand
            ? model->composeCommands(*state.macroCommand, *command) : command;
        return;
    }
    if (state.cursor < state.steps.size()) {
        state.steps.resize(state.cursor);
        if (state.cleanCursor > state.cursor) state.cleanCursor = -1;
    }
    if (!mergeKey.isEmpty() && state.cursor > 0 && state.cleanCursor != state.cursor
        && state.steps[state.cursor - 1].mergeKey == mergeKey) {
        Step &prior = state.steps[state.cursor - 1];
        const DocumentCommandPtr merged = model->composeCommands(*prior.command, *command);
        // A merge that nets to nothing keeps both steps: each still applies.
        if (merged && !merged->isNoOp() && merged->retainedBytes() <= m_memoryLimit) {
            prior.command = merged;
            prior.bytes = merged->retainedBytes();
            prior.label = label;
            // The entry keeps its identity for navigation, but it now ends in
            // a different state: a save mark taken before the merge must not
            // match it.
            ++prior.merges;
            enforceLimits(state);
            return;
        }
    }
    state.steps.append({newStepId(), label, mergeKey, command, command->retainedBytes(), 0});
    ++state.cursor;
    enforceLimits(state);
}

void UndoService::enforceLimits(History &state)
{
    quint64 used = 0;
    for (const auto &entry : state.steps) used += entry.bytes;
    const auto over = [&] {
        return used > m_memoryLimit
            || (state.stepLimit > 0 && state.steps.size() > state.stepLimit);
    };
    while (!state.steps.isEmpty() && over()) {
        if (state.cursor > 0) {
            // Oldest undo step first; its "before" state becomes unreachable.
            used -= state.steps.front().bytes;
            state.steps.removeFirst();
            ++state.evicted;
            --state.cursor;
            if (state.cleanCursor > 0) --state.cleanCursor;
            else state.cleanCursor = -1;
        } else {
            // Only redo steps remain; drop the farthest so the chain stays valid.
            used -= state.steps.back().bytes;
            state.steps.removeLast();
            if (state.cleanCursor > state.steps.size()) state.cleanCursor = -1;
        }
    }
}

OperationError UndoService::commit(DocumentModel *model, const QString &label,
                                   const QString &mergeKey, DocumentCommandPtr command)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!command || label.isEmpty())
        return failure(ErrorCode::InvalidArgument, QStringLiteral("An edit needs a label and a step"));
    if (command->isNoOp())
        return failure(ErrorCode::InvalidArgument, QStringLiteral("The edit changes nothing"));
    if (!historyFor(model))
        return failure(ErrorCode::Unavailable, QStringLiteral("Document history is not in this service"));
    if (auto problem = model->prepareCommand(*command, true)) return problem;
    if (auto problem = canRecord(model, *command)) return problem;
    // AGENT-GUARD: record before publishing so an observer of the model's
    // notification already sees the new step; publish cannot fail after a
    // successful prepare, so the pair is atomic on the GUI thread.
    History *state = historyFor(model);
    const QString key = model->m_historyKey;
    const QPointer<QObject> alive = model->modelObject();
    recordStep(*state, model, label, mergeKey, command);
    state->busy = true;
    model->publishCommand(*command, true);
    if (alive) {
        if (auto *after = historyFor(model)) after->busy = false;
    }
    m_lastError.clear();
    notify(key);
    return {};
}

OperationError UndoService::step(DocumentModel *model, bool forward)
{
    Q_ASSERT(thread() == QThread::currentThread());
    History *state = historyFor(model);
    if (!state) return failure(ErrorCode::Unavailable, QStringLiteral("Document history is not in this service"));
    if (state->busy) return failure(ErrorCode::Unavailable, QStringLiteral("A history operation is in progress"));
    if (!state->macroLabel.isEmpty())
        return failure(ErrorCode::Unavailable, QStringLiteral("Finish the grouped edit first"));
    if (forward ? state->cursor >= state->steps.size() : state->cursor <= 0)
        return failure(ErrorCode::Unavailable, forward ? QStringLiteral("No edit to redo")
                                                       : QStringLiteral("No edit to undo"));
    const Step current = state->steps[forward ? state->cursor : state->cursor - 1];
    if (auto problem = model->prepareCommand(*current.command, forward)) return problem;
    state->cursor += forward ? 1 : -1;
    state->busy = true;
    const QString key = model->m_historyKey;
    const QPointer<QObject> alive = model->modelObject();
    model->publishCommand(*current.command, forward);
    if (alive) {
        if (auto *after = historyFor(model)) after->busy = false;
    }
    m_lastError.clear();
    notify(key);
    return {};
}

bool UndoService::record(PropertyDocument *document, const QString &label,
                         const QString &mergeKey, const DocumentCommandPtr &command)
{
    Q_ASSERT(thread() == QThread::currentThread());
    History *state = historyFor(document);
    if (!state || label.isEmpty() || !command || canRecord(document, *command)) return false;
    recordStep(*state, document, label, mergeKey, command);
    m_lastError.clear();
    notify(document->m_historyKey);
    return true;
}

bool UndoService::canUndo(const DocumentModel *model) const
{
    const auto *state = historyFor(model);
    return state && state->macroLabel.isEmpty() && state->cursor > 0;
}

bool UndoService::canRedo(const DocumentModel *model) const
{
    const auto *state = historyFor(model);
    return state && state->macroLabel.isEmpty() && state->cursor < state->steps.size();
}

QString UndoService::undoText(const DocumentModel *model) const
{
    return canUndo(model) ? historyFor(model)->steps[historyFor(model)->cursor - 1].label : QString{};
}

QString UndoService::redoText(const DocumentModel *model) const
{
    return canRedo(model) ? historyFor(model)->steps[historyFor(model)->cursor].label : QString{};
}

bool UndoService::isModelClean(const DocumentModel *model) const
{
    const auto *state = historyFor(model);
    return state && state->macroLabel.isEmpty() && state->cleanCursor == state->cursor;
}

void UndoService::markModelClean(DocumentModel *model)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto *state = historyFor(model);
    if (!state || !state->macroLabel.isEmpty()) return;
    state->cleanCursor = state->cursor;
    state->cleanRevision = model->modelHeader().revision.value;
    notify(model->m_historyKey);
}

UndoService::CleanMark UndoService::cleanMark(const DocumentModel *model) const
{
    const auto *state = historyFor(model);
    // Open macro edits are published but not yet a step: no position names them.
    if (!state || !state->macroLabel.isEmpty()) return {};
    const Step *last = state->cursor > 0 ? &state->steps[state->cursor - 1] : nullptr;
    return {last ? last->id : QString{}, last ? last->merges : 0, state->epoch, state->evicted, true};
}

void UndoService::markCleanAt(DocumentModel *model, const CleanMark &mark, quint64 revision)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto *state = historyFor(model);
    if (!state) return;
    qsizetype position = -1;
    if (mark.valid && mark.epoch == state->epoch) {
        if (mark.stepId.isEmpty()) {
            position = state->evicted == mark.evicted ? 0 : -1;
        } else {
            for (qsizetype i = 0; i < state->steps.size(); ++i) {
                if (state->steps[i].id != mark.stepId) continue;
                if (state->steps[i].merges == mark.stepMerges) position = i + 1;
                break;
            }
        }
    }
    state->cleanCursor = position;
    state->cleanRevision = revision;
    notify(model->m_historyKey);
}

int UndoService::stepLimit(const DocumentModel *model) const
{
    const auto *state = historyFor(model);
    return state ? state->stepLimit : 0;
}

bool UndoService::setStepLimit(DocumentModel *model, int steps)
{
    auto *state = historyFor(model);
    if (!state || steps < 0) return false;
    if (state->stepLimit == steps) return true;
    state->stepLimit = steps;
    enforceLimits(*state);
    notify(model->m_historyKey);
    return true;
}

int UndoService::stepCount(const DocumentModel *model) const
{
    const auto *state = historyFor(model);
    return state ? int(state->steps.size()) : 0;
}

quint64 UndoService::retainedBytes(const DocumentModel *model) const
{
    const auto *state = historyFor(model);
    if (!state) return 0;
    quint64 total = state->macroCommand ? state->macroCommand->retainedBytes() : 0;
    for (const auto &entry : state->steps) total += entry.bytes;
    return total;
}

} // namespace QindaTK
