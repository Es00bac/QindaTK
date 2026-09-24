// SPDX-License-Identifier: LGPL-3.0-or-later
// Active-document facade over the generic history engine (undo_history.cpp).
#include "undo_service.h"
#include <QThread>

namespace QindaTK {

DocumentModel *UndoService::activeModel() const
{
    const auto *state = activeHistory();
    return state ? state->model : nullptr;
}

QObject *UndoService::activeModelObject() const
{
    return activeHistory() ? m_activeObject.data() : nullptr;
}

PropertyDocument *UndoService::activeDocument() const
{
    return qobject_cast<PropertyDocument *>(activeModelObject());
}

void UndoService::setActiveModel(DocumentModel *model)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (model && !historyFor(model) && !addModel(model)) return;
    const QString key = model ? model->m_historyKey : QString{};
    if (key == m_activeKey && activeModel() == model) return;
    m_activeKey = key;
    m_activeObject = model ? model->modelObject() : nullptr;
    emit changed();
}

void UndoService::setActiveModelObject(QObject *object)
{
    auto *model = dynamic_cast<DocumentModel *>(object);
    if (object && !model) return;
    setActiveModel(model);
}

bool UndoService::canUndo() const
{
    const auto *state = activeHistory();
    return state && state->macroLabel.isEmpty() && state->cursor > 0;
}

bool UndoService::canRedo() const
{
    const auto *state = activeHistory();
    return state && state->macroLabel.isEmpty() && state->cursor < state->steps.size();
}

QString UndoService::undoText() const
{
    const auto *state = activeHistory();
    return canUndo() ? state->steps[state->cursor - 1].label : QString{};
}

QString UndoService::redoText() const
{
    const auto *state = activeHistory();
    return canRedo() ? state->steps[state->cursor].label : QString{};
}

bool UndoService::isClean() const
{
    const auto *state = activeHistory();
    return state && state->macroLabel.isEmpty() && state->cleanCursor == state->cursor;
}

QString UndoService::cleanRevision() const
{
    const auto *state = activeHistory();
    return state ? QString::number(state->cleanRevision) : QString{};
}

QVariantList UndoService::historyEntries() const
{
    QVariantList result;
    const auto *state = activeHistory();
    if (!state) return result;
    result.reserve(state->steps.size());
    for (qsizetype i = 0; i < state->steps.size(); ++i) {
        const auto &entry = state->steps[i];
        result.append(QVariantMap{{QStringLiteral("entryId"), entry.id},
                                  {QStringLiteral("label"), entry.label},
                                  {QStringLiteral("index"), int(i + 1)},
                                  {QStringLiteral("applied"), i < state->cursor},
                                  {QStringLiteral("clean"), i + 1 == state->cleanCursor}});
    }
    return result;
}

int UndoService::historyIndex() const
{
    const auto *state = activeHistory();
    return state ? int(state->cursor) : 0;
}

int UndoService::cleanIndex() const
{
    const auto *state = activeHistory();
    return state ? int(state->cleanCursor) : -1;
}

bool UndoService::navigateToHistoryIndex(int index)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto *state = activeHistory();
    if (!state || !state->macroLabel.isEmpty() || index < 0 || index > state->steps.size())
        return fail(QStringLiteral("History position unavailable"));
    while (historyIndex() > index) if (!undo()) return false;
    while (historyIndex() < index) if (!redo()) return false;
    return true;
}

bool UndoService::navigateToHistoryEntry(const QString &entryId)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto *state = activeHistory();
    if (!state || entryId.isEmpty()) return fail(QStringLiteral("History entry unavailable"));
    for (qsizetype i = 0; i < state->steps.size(); ++i)
        if (state->steps[i].id == entryId) return navigateToHistoryIndex(int(i + 1));
    return fail(QStringLiteral("History entry unavailable"));
}

bool UndoService::fail(const QString &message)
{
    m_lastError = message;
    emit rejected(message);
    emit changed();
    return false;
}

void UndoService::setMemoryLimit(quint64 bytes)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (bytes < 1024 || bytes == m_memoryLimit) return;
    for (auto it = m_histories.cbegin(); it != m_histories.cend(); ++it)
        if (it->macroCommand && it->macroCommand->retainedBytes() > bytes) return;
    m_memoryLimit = bytes;
    for (auto it = m_histories.begin(); it != m_histories.end(); ++it) enforceLimits(it.value());
    emit changed();
}

void UndoService::setMemoryLimitString(const QString &bytes)
{
    bool ok = false;
    const quint64 parsed = bytes.toULongLong(&ok, 10);
    if (ok && QString::number(parsed) == bytes) setMemoryLimit(parsed);
}

bool UndoService::undo()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!canUndo()) return fail(QStringLiteral("No edit to undo"));
    if (const auto problem = step(activeModel(), false)) return fail(problem.message);
    return true;
}

bool UndoService::redo()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!canRedo()) return fail(QStringLiteral("No edit to redo"));
    if (const auto problem = step(activeModel(), true)) return fail(problem.message);
    return true;
}

void UndoService::markClean()
{
    markModelClean(activeModel());
}

bool UndoService::beginMacro(const QString &label)
{
    Q_ASSERT(thread() == QThread::currentThread());
    DocumentModel *model = activeModel();
    auto *state = historyFor(model);
    if (!state || state->busy || label.isEmpty() || !state->macroLabel.isEmpty()) return false;
    state->macroLabel = label;
    state->macroCommand.reset();
    notify(model->m_historyKey);
    return true;
}

bool UndoService::endMacro()
{
    Q_ASSERT(thread() == QThread::currentThread());
    DocumentModel *model = activeModel();
    auto *state = historyFor(model);
    if (!state || state->busy || state->macroLabel.isEmpty()) return false;
    const QString label = state->macroLabel;
    const DocumentCommandPtr command = state->macroCommand;
    state->macroLabel.clear();
    state->macroCommand.reset();
    // Grouped edits that cancel out leave no step behind.
    if (command && !command->isNoOp()) recordStep(*state, model, label, {}, command);
    m_lastError.clear();
    notify(model->m_historyKey);
    return true;
}

bool UndoService::cancelMacro()
{
    Q_ASSERT(thread() == QThread::currentThread());
    DocumentModel *model = activeModel();
    auto *state = historyFor(model);
    if (!state || state->busy || state->macroLabel.isEmpty()) return false;
    const DocumentCommandPtr command = state->macroCommand;
    const QString key = model->m_historyKey;
    if (command && !command->isNoOp()) {
        if (const auto problem = model->prepareCommand(*command, false)) return fail(problem.message);
        state->macroLabel.clear();
        state->macroCommand.reset();
        state->busy = true;
        const QPointer<QObject> alive = model->modelObject();
        model->publishCommand(*command, false);
        if (alive) {
            if (auto *after = historyFor(model)) after->busy = false;
        }
    } else {
        // Nothing grouped, or edits that cancel out: the model is already
        // back where the macro began.
        state->macroLabel.clear();
        state->macroCommand.reset();
    }
    notify(key);
    return true;
}

} // namespace QindaTK
