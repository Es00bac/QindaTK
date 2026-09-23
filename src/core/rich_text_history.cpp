// SPDX-License-Identifier: LGPL-3.0-or-later
// Transactions, local history and the external-history bridge (D-050).
#include "rich_text_document.h"

namespace QindaTK {
using namespace RichText;

bool RichTextDocument::beginTransaction(const QString &label, const QString &mergeKey,
                                        const Selection &before)
{
    if (m_open) {
        reject(ErrorCode::Conflict, QStringLiteral("A rich-text transaction is already open"));
        return false;
    }
    if (m_readOnly) {
        reject(ErrorCode::Validation, QStringLiteral("Document is read-only"));
        return false;
    }
    m_open = Transaction{QUuid::createUuid(), label, mergeKey, {}, before, before, 0};
    m_openChange = {};
    return true;
}

OperationError RichTextDocument::apply(Step step)
{
    if (!m_open) return reject(ErrorCode::InvalidArgument, QStringLiteral("No open transaction"));
    ChangeSet change;
    if (OperationError error = applyStep(step, true, change)) return error;
    m_open->steps.append(std::move(step));
    m_openChange.merge(change);
    return {};
}

void RichTextDocument::cancelTransaction()
{
    if (!m_open) return;
    Transaction open = std::move(*m_open);
    m_open.reset();
    ChangeSet change;
    revertSteps(open.steps, open.steps.size(), change);
    m_openChange = {};
}

bool RichTextDocument::commitTransaction(const Selection &after)
{
    if (!m_open) return false;
    ensureOrder();
    const bool bodyValid = !m_blocks.isEmpty() && m_blocks.last().paragraph;
    bool cellsValid = true;
    for (const Table *t : std::as_const(m_tables))
        for (const TableRow &row : t->rows)
            for (const TableCell &cell : row.cells) cellsValid = cellsValid && !cell.paragraphs.isEmpty();
    if (!bodyValid || !cellsValid) {
        cancelTransaction();
        reject(ErrorCode::Validation, QStringLiteral("Edit would leave an invalid document"));
        return false;
    }
    Transaction transaction = std::move(*m_open);
    m_open.reset();
    ChangeSet change = std::exchange(m_openChange, {});
    if (transaction.steps.isEmpty()) return true;
    transaction.after = after.anchor.isValid() ? after : transaction.after;
    transaction.bytes = cost(transaction);
    ++m_revision;
    m_lastError.clear();
    recordHistory(std::move(transaction));
    finishChange(change);
    emit historyChanged();
    return true;
}

quint64 RichTextDocument::cost(const Transaction &transaction)
{
    quint64 bytes = 64;
    for (const Step &step : transaction.steps) {
        bytes += 48;
        if (const auto *text = std::get_if<StepText>(&step))
            bytes += quint64(text->removed.size() + text->inserted.size()) * 2
                + quint64(text->removedSpans.size() + text->insertedSpans.size()) * 8;
        else if (const auto *spans = std::get_if<StepSpans>(&step))
            bytes += quint64(spans->before.size() + spans->after.size()) * 8;
        else if (const auto *merge = std::get_if<StepMerge>(&step))
            bytes += quint64(merge->next->text.size()) * 2;
        else if (const auto *blocks = std::get_if<StepBlocks>(&step))
            for (const Block &block : blocks->blocks)
                bytes += block.paragraph ? quint64(block.paragraph->text.size()) * 2 + 96 : 4096;
    }
    return bytes;
}

void RichTextDocument::recordHistory(Transaction transaction)
{
    const QString id = transaction.id.toString(QUuid::WithoutBraces);
    const QString label = transaction.label;
    if (m_externalUndo) {
        m_receipts.insert(transaction.id, Receipt{transaction, true, m_revision});
        emit committed(id, label);
        return;
    }
    m_history.resize(m_historyCursor);
    Transaction *top = m_history.isEmpty() ? nullptr : &m_history.last();
    // Coalesce a continued gesture (typing, repeated delete) when the caret
    // carries on exactly where the previous transaction left it.
    if (top && m_mergeAllowed && !transaction.mergeKey.isEmpty()
        && top->mergeKey == transaction.mergeKey && top->after.focus == transaction.before.focus
        && top->after.anchor == transaction.before.anchor) {
        top->steps += transaction.steps;
        top->after = transaction.after;
        top->bytes += transaction.bytes;
    } else {
        m_history.append(std::move(transaction));
    }
    m_historyCursor = m_history.size();
    m_mergeAllowed = true;
    trimHistory();
    emit committed(id, label);
}

void RichTextDocument::trimHistory()
{
    quint64 total = 0;
    for (const Transaction &t : std::as_const(m_history)) total += t.bytes;
    while (!m_history.isEmpty() && (m_history.size() > MaximumHistory || total > MaximumHistoryBytes)) {
        total -= m_history.first().bytes;
        m_history.removeFirst();
        --m_historyCursor;
    }
    m_historyCursor = qMax<qsizetype>(0, m_historyCursor);
}

bool RichTextDocument::canUndo() const
{
    return !m_readOnly && !m_externalUndo && !m_open && m_historyCursor > 0;
}

bool RichTextDocument::canRedo() const
{
    return !m_readOnly && !m_externalUndo && !m_open && m_historyCursor < m_history.size();
}

QString RichTextDocument::undoText() const
{
    return canUndo() ? m_history.at(m_historyCursor - 1).label : QString();
}

QString RichTextDocument::redoText() const
{
    return canRedo() ? m_history.at(m_historyCursor).label : QString();
}

const Transaction *RichTextDocument::lastCommitted() const
{
    return m_historyCursor > 0 ? &m_history.at(m_historyCursor - 1) : nullptr;
}

std::optional<Selection> RichTextDocument::undoTransaction()
{
    if (!canUndo()) return std::nullopt;
    Transaction &transaction = m_history[m_historyCursor - 1];
    ChangeSet change;
    for (qsizetype i = transaction.steps.size() - 1; i >= 0; --i) {
        if (!applyStep(transaction.steps[i], false, change)) continue;
        // Unreachable unless storage was edited outside transactions: restore
        // the steps already reverted and drop the history that no longer fits.
        for (qsizetype j = i + 1; j < transaction.steps.size(); ++j)
            applyStep(transaction.steps[j], true, change);
        m_history.clear();
        m_historyCursor = 0;
        emit historyChanged();
        return std::nullopt;
    }
    --m_historyCursor;
    ++m_revision;
    m_mergeAllowed = false;
    finishChange(change);
    emit historyChanged();
    emit historyNavigated(transaction.before, true);
    return transaction.before;
}

std::optional<Selection> RichTextDocument::redoTransaction()
{
    if (!canRedo()) return std::nullopt;
    Transaction &transaction = m_history[m_historyCursor];
    ChangeSet change;
    for (qsizetype i = 0; i < transaction.steps.size(); ++i) {
        if (!applyStep(transaction.steps[i], true, change)) continue;
        revertSteps(transaction.steps, i, change);
        m_history.resize(m_historyCursor);
        emit historyChanged();
        return std::nullopt;
    }
    ++m_historyCursor;
    ++m_revision;
    m_mergeAllowed = false;
    finishChange(change);
    emit historyChanged();
    emit historyNavigated(transaction.after, false);
    return transaction.after;
}

void RichTextDocument::setExternalUndo(bool enabled)
{
    if (m_externalUndo == enabled) return;
    if (m_open) cancelTransaction();
    m_externalUndo = enabled;
    m_history.clear();
    m_historyCursor = 0;
    m_receipts.clear();
    m_mergeAllowed = false;
    emit historyChanged();
}

bool RichTextDocument::applyHistoryChanges(const QString &transactionId, bool reverse,
                                           const QString &expectedRevision)
{
    auto found = m_receipts.find(QUuid(transactionId));
    if (!m_externalUndo || m_readOnly || m_open || found == m_receipts.end()
        || expectedRevision != revisionString() || found->applied != reverse) {
        reject(ErrorCode::StaleRevision, QStringLiteral("History step does not match the document"));
        return false;
    }
    Transaction &transaction = found->transaction;
    ChangeSet change;
    const qsizetype count = transaction.steps.size();
    for (qsizetype n = 0; n < count; ++n) {
        const qsizetype i = reverse ? count - 1 - n : n;
        if (!applyStep(transaction.steps[i], !reverse, change)) continue;
        for (qsizetype m = n - 1; m >= 0; --m)
            applyStep(transaction.steps[reverse ? count - 1 - m : m], reverse, change);
        return false;
    }
    found->applied = !reverse;
    ++m_revision;
    found->revision = m_revision;
    finishChange(change);
    emit historyChanged();
    emit historyNavigated(reverse ? transaction.before : transaction.after, reverse);
    return true;
}

bool RichTextDocument::releaseHistoryTransaction(const QString &transactionId)
{
    return m_receipts.remove(QUuid(transactionId)) > 0;
}

} // namespace QindaTK
