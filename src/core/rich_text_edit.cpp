// SPDX-License-Identifier: LGPL-3.0-or-later
// Primitive step application, transactions and history of RichTextDocument.
#include "rich_text_document.h"
#include "rich_text_spans.h"

namespace QindaTK {
using namespace RichText;

void RichTextDocument::touch(Paragraph *p, ChangeSet &change)
{
    p->revision = ++m_contentCounter;
    change.paragraphs.insert(p->id);
    change.removedParagraphs.remove(p->id);
    if (p->table) {
        p->table->revision = ++m_contentCounter;
        change.tables.insert(p->table->id);
    }
}

void RichTextDocument::finishChange(const ChangeSet &change)
{
    if (!change.isEmpty()) emit contentChanged(change);
    emit changed();
}

OperationError RichTextDocument::applyText(StepText &step, bool forward, ChangeSet &change)
{
    Paragraph *p = paragraph(step.paragraph);
    if (!p) return reject(ErrorCode::StaleRevision, QStringLiteral("Unknown paragraph"));
    const QString &removed = forward ? step.removed : step.inserted;
    const QString &inserted = forward ? step.inserted : step.removed;
    const QVector<Span> &removedSpans = forward ? step.removedSpans : step.insertedSpans;
    const QVector<Span> &insertedSpans = forward ? step.insertedSpans : step.removedSpans;
    const QVector<RichObject> &leaving = forward ? step.removedObjects : step.insertedObjects;
    const QVector<RichObject> &arriving = forward ? step.insertedObjects : step.removedObjects;
    if (step.offset < 0 || step.offset + removed.size() > p->text.size())
        return reject(ErrorCode::InvalidArgument, QStringLiteral("Text range outside paragraph"));
    if (QStringView(p->text).mid(step.offset, removed.size()) != removed
        || Spans::slice(p->spans, step.offset, int(removed.size())) != Spans::normalized(removedSpans))
        return reject(ErrorCode::Conflict, QStringLiteral("Paragraph text changed since the edit"));
    if (Spans::totalLength(insertedSpans) != inserted.size())
        return reject(ErrorCode::InvalidArgument, QStringLiteral("Spans do not cover the text"));
    for (const RichObject &object : leaving)
        if (!(m_objects.value(object.id) == object))
            return reject(ErrorCode::Conflict, QStringLiteral("Object changed since the edit"));
    for (const RichObject &object : arriving)
        if (m_objects.contains(object.id))
            return reject(ErrorCode::Conflict, QStringLiteral("Object already exists"));
    p->text.replace(step.offset, removed.size(), inserted);
    p->spans = Spans::replaced(p->spans, step.offset, int(removed.size()), insertedSpans);
    for (const RichObject &object : leaving) m_objects.remove(object.id);
    for (const RichObject &object : arriving) m_objects.insert(object.id, object);
    change.objects = change.objects || !leaving.isEmpty() || !arriving.isEmpty();
    touch(p, change);
    mapAnchorsForText(p->id, step.offset, int(removed.size()), int(inserted.size()));
    return {};
}

OperationError RichTextDocument::applySpans(StepSpans &step, bool forward, ChangeSet &change)
{
    Paragraph *p = paragraph(step.paragraph);
    if (!p) return reject(ErrorCode::StaleRevision, QStringLiteral("Unknown paragraph"));
    const QVector<Span> &from = forward ? step.before : step.after;
    const QVector<Span> &to = forward ? step.after : step.before;
    const int markFrom = forward ? step.markBefore : step.markAfter;
    const int markTo = forward ? step.markAfter : step.markBefore;
    const int length = Spans::totalLength(from);
    if (length != Spans::totalLength(to) || step.offset < 0 || step.offset + length > p->text.size())
        return reject(ErrorCode::InvalidArgument, QStringLiteral("Format range outside paragraph"));
    if (Spans::slice(p->spans, step.offset, length) != Spans::normalized(from)
        || (markFrom >= 0 && p->markFormat != markFrom))
        return reject(ErrorCode::Conflict, QStringLiteral("Formatting changed since the edit"));
    p->spans = Spans::replaced(p->spans, step.offset, length, to);
    if (markTo >= 0) p->markFormat = markTo;
    touch(p, change);
    return {};
}

OperationError RichTextDocument::applySplit(const QUuid &id, int offset,
                                            const ParagraphPtr &created, ChangeSet &change)
{
    Paragraph *p = paragraph(id);
    if (!p || !created || m_paragraphs.contains(created->id) || offset < 0
        || offset > p->text.size())
        return reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid paragraph split"));
    if (offset > 0 && offset < p->text.size() && p->text.at(offset - 1).isHighSurrogate()
        && p->text.at(offset).isLowSurrogate())
        return reject(ErrorCode::InvalidArgument, QStringLiteral("Split inside a character"));
    ensureOrder();
    const int length = int(p->text.size()) - offset;
    created->text = p->text.mid(offset);
    created->spans = Spans::slice(p->spans, offset, length);
    p->text.truncate(offset);
    p->spans = Spans::slice(p->spans, 0, offset);
    if (p->table) {
        auto &paragraphs = p->table->rows[p->row].cells[p->cell].paragraphs;
        const auto at = std::find_if(paragraphs.begin(), paragraphs.end(),
                                     [p](const ParagraphPtr &item) { return item.get() == p; });
        paragraphs.insert(at + 1, created);
    } else {
        m_blocks.insert(p->blockIndex + 1, Block{created, {}});
    }
    m_paragraphs.insert(created->id, created.get());
    m_orderDirty = true;
    change.structure = true;
    touch(p, change);
    ensureOrder();
    touch(created.get(), change);
    for (PositionAnchor *anchor : std::as_const(m_anchors))
        if (anchor->position.paragraph == id && anchor->position.offset > offset)
            anchor->position = {created->id, anchor->position.offset - offset,
                                anchor->position.affinity};
    return {};
}

OperationError RichTextDocument::applyMerge(const QUuid &id, const ParagraphPtr &next,
                                            int *joinOffset, ChangeSet &change)
{
    Paragraph *p = paragraph(id);
    if (!p || !next || paragraph(next->id) != next.get())
        return reject(ErrorCode::StaleRevision, QStringLiteral("Unknown paragraph for merge"));
    ensureOrder();
    if (nextParagraph(p) != next.get() || p->table != next->table || p->row != next->row
        || p->cell != next->cell)
        return reject(ErrorCode::Conflict, QStringLiteral("Paragraphs are not adjacent"));
    const int join = int(p->text.size());
    if (*joinOffset >= 0 && *joinOffset != join)
        return reject(ErrorCode::Conflict, QStringLiteral("Paragraph text changed since the split"));
    p->text += next->text;
    p->spans = Spans::replaced(p->spans, join, 0, next->spans);
    if (p->table) {
        auto &paragraphs = p->table->rows[p->row].cells[p->cell].paragraphs;
        paragraphs.removeIf([&next](const ParagraphPtr &item) { return item == next; });
    } else {
        m_blocks.removeAt(next->blockIndex);
    }
    m_paragraphs.remove(next->id);
    m_orderDirty = true;
    *joinOffset = join;
    change.structure = true;
    touch(p, change);
    change.removedParagraphs.insert(next->id);
    change.paragraphs.remove(next->id);
    for (PositionAnchor *anchor : std::as_const(m_anchors))
        if (anchor->position.paragraph == next->id)
            anchor->position = {id, anchor->position.offset + join, anchor->position.affinity};
    return {};
}

QVector<ParagraphPtr> *RichTextDocument::cellParagraphs(const ContainerSpec &spec)
{
    Table *t = table(spec.table);
    if (!t) return nullptr;
    for (TableRow &row : t->rows) {
        if (row.id != spec.row) continue;
        for (TableCell &cell : row.cells)
            if (cell.id == spec.cell) return &cell.paragraphs;
    }
    return nullptr;
}

OperationError RichTextDocument::applyBlocks(StepBlocks &step, bool insert, ChangeSet &change)
{
    const bool body = step.container.isBody();
    QVector<ParagraphPtr> *cell = body ? nullptr : cellParagraphs(step.container);
    if (!body && !cell) return reject(ErrorCode::StaleRevision, QStringLiteral("Unknown table cell"));
    const int size = body ? int(m_blocks.size()) : int(cell->size());
    const int count = int(step.blocks.size());
    for (const Block &block : std::as_const(step.blocks))
        if ((!block.paragraph && !block.table) || (!body && block.table))
            return reject(ErrorCode::Unsupported, QStringLiteral("Tables cannot nest in cells"));
    if (insert) {
        if (step.index < 0 || step.index > size)
            return reject(ErrorCode::InvalidArgument, QStringLiteral("Block index out of range"));
        for (const Block &block : std::as_const(step.blocks))
            if (m_paragraphs.contains(block.id()) || m_tables.contains(block.id()))
                return reject(ErrorCode::Conflict, QStringLiteral("Block already present"));
        for (const RichObject &object : std::as_const(step.objects))
            if (m_objects.contains(object.id))
                return reject(ErrorCode::Conflict, QStringLiteral("Object already exists"));
        for (int i = 0; i < count; ++i) {
            const Block &block = step.blocks.at(i);
            if (body) m_blocks.insert(step.index + i, block);
            else cell->insert(step.index + i, block.paragraph);
            registerBlock(block);
        }
        for (const RichObject &object : std::as_const(step.objects)) m_objects.insert(object.id, object);
    } else {
        if (step.index < 0 || step.index + count > size)
            return reject(ErrorCode::InvalidArgument, QStringLiteral("Block index out of range"));
        for (int i = 0; i < count; ++i) {
            const Block &block = step.blocks.at(i);
            const bool same = body ? (m_blocks.at(step.index + i).paragraph == block.paragraph
                                      && m_blocks.at(step.index + i).table == block.table)
                                   : cell->at(step.index + i) == block.paragraph;
            if (!same) return reject(ErrorCode::Conflict, QStringLiteral("Blocks changed since the edit"));
        }
        for (const RichObject &object : std::as_const(step.objects))
            if (!(m_objects.value(object.id) == object))
                return reject(ErrorCode::Conflict, QStringLiteral("Object changed since the edit"));
        const Position fallback = fallbackAfterRemoval(step.blocks);
        QSet<QUuid> gone;
        for (const Block &block : std::as_const(step.blocks)) {
            if (block.paragraph) gone.insert(block.paragraph->id);
            else
                for (const TableRow &row : block.table->rows)
                    for (const TableCell &c : row.cells)
                        for (const ParagraphPtr &p : c.paragraphs) gone.insert(p->id);
        }
        if (body) m_blocks.remove(step.index, count);
        else cell->remove(step.index, count);
        for (const Block &block : std::as_const(step.blocks)) unregisterBlock(block);
        for (const RichObject &object : std::as_const(step.objects)) m_objects.remove(object.id);
        for (PositionAnchor *anchor : std::as_const(m_anchors))
            if (gone.contains(anchor->position.paragraph)) anchor->position = fallback;
        for (const QUuid &id : std::as_const(gone)) {
            change.removedParagraphs.insert(id);
            change.paragraphs.remove(id);
        }
    }
    m_orderDirty = true;
    ensureOrder();
    change.structure = true;
    change.objects = change.objects || !step.objects.isEmpty();
    if (!body) {
        change.tables.insert(step.container.table);
        if (Table *t = table(step.container.table)) t->revision = ++m_contentCounter;
    }
    for (const Block &block : std::as_const(step.blocks)) {
        if (block.table) {
            change.tables.insert(block.table->id);
            block.table->revision = ++m_contentCounter;
        }
        if (!insert) continue;
        if (block.paragraph) touch(block.paragraph.get(), change);
        else
            for (const TableRow &row : block.table->rows)
                for (const TableCell &c : row.cells)
                    for (const ParagraphPtr &p : c.paragraphs) touch(p.get(), change);
    }
    return {};
}

OperationError RichTextDocument::applyMove(const StepMove &step, bool forward, ChangeSet &change)
{
    // Forward moves blocks [from, from+count) so they start at `to` (an index
    // counted before the move); the inverse moves them back.
    const int size = int(m_blocks.size());
    if (step.count <= 0 || step.from < 0 || step.from + step.count > size || step.to < 0
        || step.to > size || (step.to >= step.from && step.to <= step.from + step.count))
        return reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid block move"));
    const int destination = step.to > step.from ? step.to - step.count : step.to;
    const int start = forward ? step.from : destination;
    const int target = forward ? destination : step.from;
    const QVector<Block> moved = m_blocks.mid(start, step.count);
    m_blocks.remove(start, step.count);
    for (int i = 0; i < moved.size(); ++i) m_blocks.insert(target + i, moved.at(i));
    m_orderDirty = true;
    ensureOrder();
    change.structure = true;
    for (const Block &block : moved) {
        if (block.paragraph) {
            touch(block.paragraph.get(), change);
            continue;
        }
        block.table->revision = ++m_contentCounter;
        change.tables.insert(block.table->id);
    }
    return {};
}

OperationError RichTextDocument::applyStep(Step &step, bool forward, ChangeSet &change)
{
    return std::visit([&](auto &s) -> OperationError {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, StepText>) {
            return applyText(s, forward, change);
        } else if constexpr (std::is_same_v<T, StepSpans>) {
            return applySpans(s, forward, change);
        } else if constexpr (std::is_same_v<T, StepParagraphFormat>) {
            Paragraph *p = paragraph(s.paragraph);
            if (!p) return reject(ErrorCode::StaleRevision, QStringLiteral("Unknown paragraph"));
            if (!(p->format == (forward ? s.before : s.after)))
                return reject(ErrorCode::Conflict, QStringLiteral("Paragraph format changed"));
            const bool listChange = p->format.listId != (forward ? s.after : s.before).listId
                || p->format.listLevel != (forward ? s.after : s.before).listLevel;
            p->format = forward ? s.after : s.before;
            change.lists = change.lists || listChange;
            change.structure = change.structure || listChange
                || s.before.pageBreakBefore != s.after.pageBreakBefore;
            touch(p, change);
            return {};
        } else if constexpr (std::is_same_v<T, StepSplit>) {
            if (forward) return applySplit(s.paragraph, s.offset, s.created, change);
            int join = s.offset;
            return applyMerge(s.paragraph, s.created, &join, change);
        } else if constexpr (std::is_same_v<T, StepMerge>) {
            if (forward) {
                int join = -1;
                OperationError error = applyMerge(s.paragraph, s.next, &join, change);
                if (!error) s.joinOffset = join;
                return error;
            }
            return applySplit(s.paragraph, s.joinOffset, s.next, change);
        } else if constexpr (std::is_same_v<T, StepBlocks>) {
            return applyBlocks(s, forward == s.insert, change);
        } else if constexpr (std::is_same_v<T, StepMove>) {
            return applyMove(s, forward, change);
        } else if constexpr (std::is_same_v<T, StepList>) {
            const auto &from = forward ? s.before : s.after;
            const auto &to = forward ? s.after : s.before;
            const ListDefinition *current = list(s.listId);
            if (bool(current) != bool(from) || (current && !(*current == *from)))
                return reject(ErrorCode::Conflict, QStringLiteral("List definition changed"));
            if (to) m_lists.insert(s.listId, *to);
            else m_lists.remove(s.listId);
            change.lists = true;
            // A created or removed list has no items yet or any more; only an
            // in-place change moves the indents of the paragraphs that use it.
            if (from && to) change.listDefinitions.insert(s.listId);
            return {};
        } else if constexpr (std::is_same_v<T, StepObject>) {
            const RichObject *current = object(s.objectId);
            if (!current || !(*current == (forward ? s.before : s.after)))
                return reject(ErrorCode::Conflict, QStringLiteral("Object changed since the edit"));
            m_objects.insert(s.objectId, forward ? s.after : s.before);
            change.objects = true;
            for (Paragraph *p : paragraphsInOrder())
                for (int i = 0; i < p->text.size(); ++i)
                    if (p->text.at(i) == QChar(ObjectChar)
                        && charFormat(Spans::formatAt(p->spans, i)).objectId == s.objectId)
                        touch(p, change);
            return {};
        } else {
            if (!(m_page == (forward ? s.pageBefore : s.pageAfter))
                || !(m_defaults == (forward ? s.defaultsBefore : s.defaultsAfter)))
                return reject(ErrorCode::Conflict, QStringLiteral("Page setup changed"));
            m_page = forward ? s.pageAfter : s.pageBefore;
            m_defaults = forward ? s.defaultsAfter : s.defaultsBefore;
            change.document = true;
            return {};
        }
    }, step);
}

bool RichTextDocument::revertSteps(QVector<Step> &steps, qsizetype count, ChangeSet &change)
{
    for (qsizetype i = count - 1; i >= 0; --i)
        if (applyStep(steps[i], false, change)) return false;
    return true;
}

} // namespace QindaTK
