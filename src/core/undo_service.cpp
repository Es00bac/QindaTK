// SPDX-License-Identifier: LGPL-3.0-or-later
#include "undo_service.h"
#include <QThread>
#include <QUuid>
#include <limits>

namespace QindaTK {
namespace {
QVector<PropertyChange> joined(const QVector<PropertyChange> &first,
                               const QVector<PropertyChange> &second)
{
    QVector<PropertyChange> result = first;
    for (const auto &change : second) {
        bool found = false;
        for (auto &existing : result) {
            if (existing.target != change.target || existing.propertyId != change.propertyId) continue;
            existing.after = change.after;
            found = true;
            break;
        }
        if (!found) result.append(change);
    }
    return result;
}
}

UndoService::History *UndoService::history(PropertyDocument *document)
{
    if (!document) return nullptr;
    auto it = m_histories.find(document->documentIdString());
    return it != m_histories.end() && it->document == document ? &it.value() : nullptr;
}

const UndoService::History *UndoService::activeHistory() const
{
    if (!m_active) return nullptr;
    auto it = m_histories.constFind(m_active->documentIdString());
    return it != m_histories.cend() && it->document == m_active ? &it.value() : nullptr;
}

bool UndoService::addDocument(PropertyDocument *document)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!document || document->thread() != thread()) return false;
    const QString id = document->documentIdString();
    if (m_histories.contains(id)) return history(document) != nullptr;
    History state;
    state.document = document;
    state.cleanRevision = document->revision().value;
    m_histories.insert(id, state);
    connect(document, &PropertyDocument::historyInvalidated, this,
            [this, id] { clearDocument(id); });
    connect(document, &QObject::destroyed, this, [this, id] {
        m_histories.remove(id);
        if (!m_active) emit changed();
    });
    if (!m_active) m_active = document;
    emit changed();
    return true;
}

bool UndoService::removeDocument(PropertyDocument *document)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!history(document)) return false;
    m_histories.remove(document->documentIdString());
    if (m_active == document) m_active = nullptr;
    emit changed();
    return true;
}

void UndoService::setActiveDocument(PropertyDocument *document)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (document && !history(document) && !addDocument(document)) return;
    if (m_active == document) return;
    m_active = document;
    emit changed();
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

bool UndoService::isClean(PropertyDocument *document) const
{
    auto it = m_histories.constFind(document ? document->documentIdString() : QString{});
    return it != m_histories.cend() && it->document == document
        && it->macroLabel.isEmpty() && it->cleanCursor == it->cursor;
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
        const auto &step = state->steps[i];
        result.append(QVariantMap{{QStringLiteral("entryId"), step.id},
                                  {QStringLiteral("label"), step.label},
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

quint64 UndoService::cost(const QVector<PropertyChange> &changes)
{
    quint64 total = 0;
    for (const auto &change : changes) {
        const auto valueBytes = [](const QVariant &value) -> quint64 {
            if (value.metaType().id() == QMetaType::QByteArray)
                return static_cast<quint64>(value.toByteArray().size());
            return static_cast<quint64>(value.toString().size()) * 2 + 32;
        };
        total += 128 + static_cast<quint64>(change.propertyId.size()) * 2
            + valueBytes(change.before) + valueBytes(change.after);
    }
    return total;
}

bool UndoService::canRecord(PropertyDocument *document,
                            const QVector<PropertyChange> &changes) const
{
    auto it = m_histories.constFind(document ? document->documentIdString() : QString{});
    if (it == m_histories.cend() || it->document != document) return false;
    const QVector<PropertyChange> combined = it->macroLabel.isEmpty()
        ? changes : joined(it->macroChanges, changes);
    return cost(combined) <= m_memoryLimit;
}

void UndoService::setMemoryLimit(quint64 bytes)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (bytes < 1024 || bytes == m_memoryLimit) return;
    for (auto it = m_histories.cbegin(); it != m_histories.cend(); ++it)
        if (!it->macroLabel.isEmpty() && cost(it->macroChanges) > bytes) return;
    m_memoryLimit = bytes;
    for (auto it = m_histories.begin(); it != m_histories.end(); ++it) {
        auto &state = it.value();
        quint64 used = 0;
        for (const auto &step : state.steps) used += step.bytes;
        while (used > m_memoryLimit && !state.steps.isEmpty()) {
            used -= state.steps.front().bytes;
            state.steps.removeFirst();
            if (state.cursor > 0) --state.cursor;
            if (state.cleanCursor > 0) --state.cleanCursor;
            else state.cleanCursor = -1;
        }
    }
    emit changed();
}

void UndoService::setMemoryLimitString(const QString &bytes)
{
    bool ok = false;
    const quint64 parsed = bytes.toULongLong(&ok, 10);
    if (ok && QString::number(parsed) == bytes) setMemoryLimit(parsed);
}

bool UndoService::record(PropertyDocument *document, const QString &label,
                         const QString &mergeKey, const QVector<PropertyChange> &changes)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto *state = history(document);
    if (!state || label.isEmpty() || !canRecord(document, changes)) return false;
    if (changes.isEmpty()) return true;
    if (!state->macroLabel.isEmpty()) {
        state->macroChanges = joined(state->macroChanges, changes);
        emit changed();
        return true;
    }
    if (state->cursor < state->steps.size()) {
        state->steps.resize(state->cursor);
        if (state->cleanCursor > state->cursor) state->cleanCursor = -1;
    }
    if (!mergeKey.isEmpty() && state->cursor > 0 && state->cleanCursor != state->cursor
        && state->steps[state->cursor - 1].mergeKey == mergeKey
        && cost(joined(state->steps[state->cursor - 1].changes, changes)) <= m_memoryLimit) {
        auto &prior = state->steps[state->cursor - 1];
        prior.changes = joined(prior.changes, changes);
        prior.bytes = cost(prior.changes);
        prior.label = label;
    } else {
        state->steps.append({QUuid::createUuid().toString(QUuid::WithoutBraces),
                             label, mergeKey, changes, cost(changes)});
        ++state->cursor;
    }
    quint64 used = 0;
    for (const auto &step : state->steps) used += step.bytes;
    while (used > m_memoryLimit && !state->steps.isEmpty()) {
        used -= state->steps.front().bytes;
        state->steps.removeFirst();
        if (state->cursor > 0) --state->cursor;
        if (state->cleanCursor > 0) --state->cleanCursor;
        else state->cleanCursor = -1;
    }
    m_lastError.clear();
    emit changed();
    return true;
}

void UndoService::clearDocument(const QString &id)
{
    auto it = m_histories.find(id);
    if (it == m_histories.end()) return;
    it->steps.clear();
    it->cursor = 0;
    it->cleanCursor = -1;
    it->macroLabel.clear();
    it->macroChanges.clear();
    emit changed();
}

bool UndoService::undo()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!canUndo()) return fail(QStringLiteral("No edit to undo"));
    auto *state = history(m_active);
    const auto result = m_active->applyCommitted(state->steps[state->cursor - 1].changes, false);
    if (result) return fail(result.message);
    --state->cursor;
    m_lastError.clear();
    emit changed();
    return true;
}

bool UndoService::redo()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!canRedo()) return fail(QStringLiteral("No edit to redo"));
    auto *state = history(m_active);
    const auto result = m_active->applyCommitted(state->steps[state->cursor].changes, true);
    if (result) return fail(result.message);
    ++state->cursor;
    m_lastError.clear();
    emit changed();
    return true;
}

void UndoService::markClean()
{
    markClean(m_active);
}

void UndoService::markClean(PropertyDocument *document)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto *state = history(document);
    if (!state || !state->macroLabel.isEmpty()) return;
    state->cleanCursor = state->cursor;
    state->cleanRevision = document->revision().value;
    emit changed();
}

bool UndoService::beginMacro(const QString &label)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto *state = history(m_active);
    if (!state || label.isEmpty() || !state->macroLabel.isEmpty()) return false;
    state->macroLabel = label;
    state->macroChanges.clear();
    emit changed();
    return true;
}

bool UndoService::endMacro()
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto *state = history(m_active);
    if (!state || state->macroLabel.isEmpty()) return false;
    const QString label = state->macroLabel;
    const auto changes = state->macroChanges;
    state->macroLabel.clear();
    state->macroChanges.clear();
    if (changes.isEmpty()) {
        emit changed();
        return true;
    }
    return record(m_active, label, {}, changes);
}

bool UndoService::cancelMacro()
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto *state = history(m_active);
    if (!state || state->macroLabel.isEmpty()) return false;
    if (!state->macroChanges.isEmpty()) {
        const auto result = m_active->applyCommitted(state->macroChanges, false);
        if (result) return fail(result.message);
    }
    state->macroLabel.clear();
    state->macroChanges.clear();
    emit changed();
    return true;
}

} // namespace QindaTK
