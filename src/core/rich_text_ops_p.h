// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"
#include <functional>

// Private helpers shared by the rich_text_*.cpp operation files.
namespace QindaTK::RichText::Detail {

// Joins an open transaction, or opens one it commits/cancels itself.
struct AutoTransaction {
    AutoTransaction(RichTextDocument *document, const QString &label);
    ~AutoTransaction();
    AutoTransaction(const AutoTransaction &) = delete;
    AutoTransaction &operator=(const AutoTransaction &) = delete;
    // Commits an owned transaction (caret as its after-selection) or cancels
    // it when !ok; a joined transaction is left to its owner.
    bool finish(bool ok, const Position &caret);
    RichTextDocument *doc = nullptr;
    bool owned = false;
    bool valid = true;
};

// Sets a string member for a scope and restores it (the tracked insertion
// that the paragraph breaks of one typing or paste gesture join).
struct ScopedValue {
    ScopedValue(QString &slot, const QString &value) : slot(slot), saved(slot) { if (!value.isEmpty()) slot = value; }
    ~ScopedValue() { slot = saved; }
    ScopedValue(const ScopedValue &) = delete;
    ScopedValue &operator=(const ScopedValue &) = delete;
    QString &slot;
    QString saved;
};

// Normalizes newlines and drops control and object characters from typed or
// pasted text; '\n' separates paragraphs, U+2028 stays a soft line break.
QString cleanInsertedText(QString text);

StepText textStep(const RichTextDocument &doc, const Paragraph &p, int offset, int length,
                  const QString &inserted, const QVector<Span> &insertedSpans,
                  const QVector<RichObject> &insertedObjects = {});

// Review and field helpers (rich_text_review_edit.cpp). rewriteFormats maps
// the character formats of [from, to) in `p` through `edit` as one StepSpans
// (none when nothing changes) and returns false when the step is refused.
bool rewriteFormats(RichTextDocument &doc, const Paragraph &p, int from, int to,
                    const std::function<void(CharFormat &)> &edit);
QString newAnnotationId();
QString reviewTime();                     // now, ISO 8601 UTC to the second

// Table grid helpers (rich_text_table_edit.cpp) shared with the merged-cell
// and table-style operations.
inline constexpr int MaximumTableRows = 10000;
inline constexpr int MaximumTableColumns = 64;
// The shared pointer that owns a registered table, or null.
TablePtr livePointer(const RichTextDocument &doc, const Table *table);
// Applies one StepTable replacing `current` with `next` (its own undo step
// unless a transaction is open).
bool commitGrid(RichTextDocument &doc, const QString &label, const TablePtr &current, const TablePtr &next);
// A new empty cell shaped like the visible cell `model`.
TableCell cellLike(const RichTextDocument &doc, const TableCell &model);
// The visible cell (plain or merged anchor) covering a slot.
const TableCell &anchorCell(const Table &table, const TableGridMap &map, int row, int column);
// Slot of the cell with `id` (row * columns + column), or -1.
int slotOfCell(const Table &table, const QUuid &id);

} // namespace QindaTK::RichText::Detail
