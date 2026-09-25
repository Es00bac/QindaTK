// SPDX-License-Identifier: LGPL-3.0-or-later
// Tracked edits of structure and of generated content (WP-052): deleting a
// table row or column, changing an equation's source and changing what a
// reference field says. Each is recorded as ordinary tracked insertions and
// deletions, so accept, reject and undo work as they do for text
// (docs/references-review.md#tracked-changes).
#include "rich_text_document.h"
#include "rich_text_fields_p.h"
#include "rich_text_ops_p.h"
#include "rich_text_spans.h"

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

bool RichTextDocument::trackStructureDeletion(const Table &table, const QUuid &row, const QUuid &column)
{
    const int columnIndex = column.isNull() ? -1 : int(table.columnIds.indexOf(column));
    AutoTransaction transaction(this, row.isNull() ? tr("Delete table column") : tr("Delete table row"));
    if (!transaction.valid) return false;
    TrackedChange change{newAnnotationId(), ChangeKind::Deletion, m_reviewAuthor, reviewTime(), {}, {}, table.id, row, column};
    bool ok = !apply(StepAnnotation{change.id, std::nullopt, Annotation(change)});
    // The row's (or column's) text is marked deleted; accepting the change
    // removes the text and then the row or column itself.
    for (const TableRow &r : table.rows)
        for (int c = 0; ok && c < r.cells.size(); ++c) {
            if (!row.isNull() ? r.id != row : c != columnIndex) continue;
            for (const ParagraphPtr &p : r.cells.at(c).paragraphs)
                ok = ok && rewriteFormats(*this, *p, 0, int(p->text.size()), [&change](CharFormat &f) {
                    if (f.deletionId.isEmpty()) f.deletionId = change.id;
                });
        }
    return transaction.finish(ok, {});
}

bool RichTextDocument::removeTableRow(const QString &tableId, const QString &rowId)
{
    const Table *t = table(QUuid(tableId));
    if (!m_trackChanges || !t) return removeTableRowNow(tableId, rowId);
    const bool known = std::any_of(t->rows.cbegin(), t->rows.cend(), [&rowId](const TableRow &row) {
        return row.id == QUuid(rowId); });
    if (!known || t->rows.size() <= 1)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table row deletion"));
    return trackStructureDeletion(*t, QUuid(rowId), {});
}

bool RichTextDocument::removeTableColumn(const QString &tableId, const QString &columnId)
{
    const Table *t = table(QUuid(tableId));
    if (!m_trackChanges || !t) return removeTableColumnNow(tableId, columnId);
    if (!t->columnIds.contains(QUuid(columnId)) || t->columnIds.size() <= 1)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table column deletion"));
    return trackStructureDeletion(*t, {}, QUuid(columnId));
}

bool RichTextDocument::replaceEquationTracked(const RichObject &current, const RichObject &next)
{
    // Where the equation is anchored.
    Paragraph *anchor = nullptr;
    int offset = -1;
    for (Paragraph *p : paragraphsInOrder())
        for (int i = 0; !anchor && i < p->text.size(); ++i)
            if (p->text.at(i) == QChar(ObjectChar) && charFormat(Spans::formatAt(p->spans, i)).objectId == current.id)
                anchor = p, offset = i;
    if (!anchor) return !reject(ErrorCode::InvalidArgument, QStringLiteral("The equation is not in the text"));
    const CharFormat format = charFormat(Spans::formatAt(anchor->spans, offset));
    const auto insertion = m_annotations.changes.constFind(format.insertionId);
    // One's own inserted equation simply changes.
    if (insertion != m_annotations.changes.constEnd() && insertion->author == m_reviewAuthor)
        return !apply(StepObject{current.id, current, next});
    // AGENT-NOTE: the edited equation keeps its ID (editors follow it); the
    // former source stays in front of it as a copy marked deleted, so
    // rejecting restores it and accepting drops the copy.
    RichObject former = current;
    former.id = newAnnotationId();
    const TrackedChange deletion{newAnnotationId(), ChangeKind::Deletion, m_reviewAuthor, reviewTime(), {}, {}, {}, {}, {}};
    const std::optional<QString> inserted = trackedInsertionId({anchor->id, offset});
    if (!inserted || apply(StepAnnotation{deletion.id, std::nullopt, Annotation(deletion)})) return false;
    CharFormat old = format;
    old.objectId = former.id;
    old.deletionId = deletion.id;
    const QString insertionId = *inserted;
    return !apply(textStep(*this, *anchor, offset, 0, QString(QChar(ObjectChar)), {{1, internFormat(old)}}, {former}))
        && !apply(StepObject{current.id, current, next})
        && rewriteFormats(*this, *anchor, offset + 1, offset + 2, [&insertionId](CharFormat &f) {
               f.insertionId = insertionId;
               f.deletionId.clear();
           });
}

bool RichTextDocument::replaceFieldTracked(const ReferenceField &current, ReferenceField next)
{
    // AGENT-NOTE: cross-references follow a caption by its ID, so a caption
    // cannot be replaced by a new field without breaking them.
    if (current.kind == FieldKind::Sequence)
        return !reject(ErrorCode::Unsupported,
                       tr("Changing a caption's series is not recorded as a tracked change. Turn off Track changes to do it."));
    const Fields::Scan found = Fields::scan(*this);
    const auto placed = found.placed.constFind(current.id);
    if (placed == found.placed.constEnd())
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("The reference is not in the text"));
    const Position start{placed->paragraph->id, placed->offset};
    const Position end{placed->paragraph->id, placed->offset + placed->length};
    // The new reference goes after the old one, which is then marked deleted.
    next.id = newAnnotationId();
    return insertField(end, next).has_value() && deleteRange(start, end).has_value();
}

} // namespace QindaTK
