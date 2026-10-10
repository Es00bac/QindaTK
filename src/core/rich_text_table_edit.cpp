// SPDX-License-Identifier: LGPL-3.0-or-later
// Stable-identity table grid edits (StepTable), aware of merged cells.
// Snapshots, merge, split and table styles: rich_text_table_spans.cpp.
#include "rich_text_document.h"
#include "rich_text_ops_p.h"
#include <cmath>

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

namespace {

constexpr double MaximumColumnWeight = 1e6;

// Collects a grid's paragraph IDs. False when a paragraph appears twice:
// versions of one grid share paragraphs, one grid never does. Covered
// slots hold no paragraphs; mapTableGrid checks that visible cells do.
bool collectParagraphIds(const Table &table, QSet<QUuid> *ids)
{
    for (const TableRow &row : table.rows)
        for (const TableCell &cell : row.cells)
            for (const ParagraphPtr &p : cell.paragraphs) {
                if (!p || ids->contains(p->id)) return false;
                ids->insert(p->id);
            }
    return true;
}

// Why a replacement grid cannot be applied, or an empty string.
QString gridProblem(const Table &t, const QUuid &tableId)
{
    const qsizetype columns = t.columnWeights.size();
    if (t.id != tableId || t.rows.isEmpty() || t.rows.size() > MaximumTableRows || columns == 0
        || columns > MaximumTableColumns || t.columnIds.size() != columns || t.headerRows < 0
        || t.headerRows >= t.rows.size())
        return QStringLiteral("Invalid table grid");
    for (double weight : t.columnWeights)
        if (!(weight > 0 && weight <= MaximumColumnWeight)) return QStringLiteral("Invalid table column width");
    QSet<QUuid> ids(t.columnIds.cbegin(), t.columnIds.cend());
    for (const TableRow &row : t.rows) {
        if (row.cells.size() != columns) return QStringLiteral("Invalid table row");
        ids.insert(row.id);
    }
    if (ids.size() != columns + t.rows.size() || ids.contains(QUuid()))
        return QStringLiteral("Duplicate table row or column identity");
    TableGridMap map;
    return mapTableGrid(t, &map);
}

int rowIndex(const Table &table, const QUuid &id)
{
    for (int i = 0; i < table.rows.size(); ++i)
        if (table.rows.at(i).id == id) return i;
    return -1;
}

int columnIndex(const Table &table, const QUuid &id)
{
    return int(table.columnIds.indexOf(id));
}

// Objects anchored in paragraphs of `from` that `to` no longer holds.
QVector<RichObject> objectsRemoved(const RichTextDocument &doc, const Table &from, const Table &to)
{
    QSet<QUuid> kept;
    collectParagraphIds(to, &kept);
    QVector<RichObject> result;
    for (const TableRow &row : from.rows)
        for (const TableCell &cell : row.cells)
            for (const ParagraphPtr &p : cell.paragraphs)
                if (!kept.contains(p->id)) result += doc.objectsIn(*p, 0, int(p->text.size()));
    return result;
}

// Index in the target grid of the first item at or after `index` that
// survives, else of the nearest surviving one before it; -1 if none does.
template <typename IdAt>
int survivingIndex(int index, int count, IdAt idAt, const QHash<QUuid, int> &target)
{
    for (int i = index; i < count; ++i)
        if (const auto found = target.constFind(idAt(i)); found != target.constEnd()) return *found;
    for (int i = index - 1; i >= 0; --i)
        if (const auto found = target.constFind(idAt(i)); found != target.constEnd()) return *found;
    return -1;
}

// Where an anchor in a removed paragraph of `from` goes in `to`: the cell
// at its row and column when both survive, otherwise the next surviving row
// or column (the previous one at the end), so a caret stays beside the edit.
// A slot a merged cell covers resolves to that merged cell.
Position nearestCell(const Table &from, const Table &to, const TableGridMap &toMap, const QUuid &paragraph)
{
    QHash<QUuid, int> rows, columns;
    for (int r = 0; r < to.rows.size(); ++r) rows.insert(to.rows.at(r).id, r);
    for (int c = 0; c < to.columnIds.size(); ++c) columns.insert(to.columnIds.at(c), c);
    for (int r = 0; r < from.rows.size(); ++r)
        for (int c = 0; c < from.rows.at(r).cells.size(); ++c)
            for (const ParagraphPtr &p : from.rows.at(r).cells.at(c).paragraphs) {
                if (p->id != paragraph) continue;
                const int row = survivingIndex(r, int(from.rows.size()),
                                               [&from](int i) { return from.rows.at(i).id; }, rows);
                const int column = survivingIndex(c, int(from.columnIds.size()),
                                                  [&from](int i) { return from.columnIds.at(i); }, columns);
                return {anchorCell(to, toMap, qMax(0, row), qMax(0, column)).paragraphs.first()->id, 0};
            }
    return {anchorCell(to, toMap, 0, 0).paragraphs.first()->id, 0};
}

} // namespace

namespace RichText::Detail {

TableCell cellLike(const RichTextDocument &doc, const TableCell &model)
{
    // The model's background plus the typing format and paragraph format of
    // its first paragraph (without list membership), so an inserted row or
    // column looks like its neighbour.
    const Paragraph &source = *model.paragraphs.first();
    auto paragraph = std::make_shared<Paragraph>();
    paragraph->id = QUuid::createUuid();
    paragraph->markFormat = doc.charFormat(source.markFormat).href.isEmpty() ? source.markFormat : 0;
    paragraph->format = source.format;
    paragraph->format.listId.clear();
    paragraph->format.listLevel = 0;
    paragraph->format.pageBreakBefore = false;
    return {QUuid::createUuid(), {paragraph}, model.background, 1, 1};
}

const TableCell &anchorCell(const Table &table, const TableGridMap &map, int row, int column)
{
    const int slot = map.anchorOf(row, column);
    return table.rows.at(slot / map.columns).cells.at(slot % map.columns);
}

int slotOfCell(const Table &table, const QUuid &id)
{
    const int columns = int(table.columnWeights.size());
    for (int r = 0; r < table.rows.size(); ++r)
        for (int c = 0; c < table.rows.at(r).cells.size(); ++c)
            if (table.rows.at(r).cells.at(c).id == id) return r * columns + c;
    return -1;
}

TablePtr livePointer(const RichTextDocument &doc, const Table *t)
{
    // Rebuilds the document order first: Table::blockIndex is stale after a
    // paragraph merge.
    if (!t) return {};
    doc.paragraphsInOrder();
    const Block block = doc.blocks().value(t->blockIndex);
    return block.table.get() == t ? block.table : TablePtr{};
}

bool commitGrid(RichTextDocument &doc, const QString &label, const TablePtr &current, const TablePtr &next)
{
    AutoTransaction transaction(&doc, label);
    if (!transaction.valid) return false;
    StepTable step{current->id, current, next, objectsRemoved(doc, *current, *next)};
    return transaction.finish(!doc.apply(std::move(step)), {});
}

} // namespace RichText::Detail

OperationError RichTextDocument::applyTable(const StepTable &step, bool forward, ChangeSet &change)
{
    const TablePtr &from = forward ? step.before : step.after;
    const TablePtr &to = forward ? step.after : step.before;
    // AGENT-GUARD: applyMerge leaves Table::blockIndex stale until the order
    // is rebuilt. Replacing m_blocks[blockIndex] without this would overwrite
    // a neighbouring block (and duplicate the table) during undo or redo.
    ensureOrder();
    if (!from || !to || table(step.tableId) != from.get() || m_blocks.value(from->blockIndex).table != from)
        return reject(ErrorCode::Conflict, QStringLiteral("Table changed since the edit"));
    if (const QString problem = gridProblem(*to, step.tableId); !problem.isEmpty())
        return reject(ErrorCode::Validation, problem);
    if (const NamedStyle *named = style(to->styleId);
        !to->styleId.isEmpty() && (!named || named->kind != StyleKind::Table))
        return reject(ErrorCode::Validation, QStringLiteral("Unknown table style"));
    QSet<QUuid> oldIds, newIds;
    if (!collectParagraphIds(*from, &oldIds) || !collectParagraphIds(*to, &newIds))
        return reject(ErrorCode::Validation, QStringLiteral("Invalid table cell"));
    for (const QUuid &id : std::as_const(newIds))
        if (!oldIds.contains(id) && m_paragraphs.contains(id))
            return reject(ErrorCode::Conflict, QStringLiteral("Table paragraph already present"));
    for (const RichObject &object : step.removedObjects) {
        if (forward && !(m_objects.value(object.id) == object))
            return reject(ErrorCode::Conflict, QStringLiteral("Table object changed since the edit"));
        if (!forward && m_objects.contains(object.id))
            return reject(ErrorCode::Conflict, QStringLiteral("Table object already exists"));
    }
    TableGridMap toMap;
    mapTableGrid(*to, &toMap);
    for (PositionAnchor *anchor : std::as_const(m_anchors)) {
        const QUuid id = anchor->position.paragraph;
        if (oldIds.contains(id) && !newIds.contains(id)) anchor->position = nearestCell(*from, *to, toMap, id);
    }
    const int index = from->blockIndex;
    unregisterBlock(m_blocks.at(index));
    m_blocks[index] = Block{{}, to};
    registerBlock(m_blocks.at(index));
    for (const RichObject &object : step.removedObjects) {
        if (forward) m_objects.remove(object.id);
        else m_objects.insert(object.id, object);
    }
    for (const QUuid &id : oldIds - newIds) {
        change.removedParagraphs.insert(id);
        change.paragraphs.remove(id);
    }
    for (const QUuid &id : newIds - oldIds) {
        change.removedParagraphs.remove(id);
        change.paragraphs.insert(id);
    }
    m_orderDirty = true;
    ensureOrder();
    to->revision = ++m_contentCounter;
    change.tables.insert(step.tableId);
    change.structure = true;
    change.objects = change.objects || !step.removedObjects.isEmpty();
    return {};
}

std::optional<Position> RichTextDocument::deleteTable(const QUuid &tableId)
{
    if (refuseWhileTracking(tr("Deleting a table"))) return std::nullopt;
    const TablePtr current = livePointer(*this, table(tableId));
    if (!current) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown table"));
        return std::nullopt;
    }
    AutoTransaction transaction(this, tr("Delete table"));
    if (!transaction.valid) return std::nullopt;
    const int index = current->blockIndex;
    const QVector<Block> whole{m_blocks.at(index)};
    bool ok = !apply(StepBlocks{{}, index, whole, objectsIn(whole), false});
    if (ok && m_blocks.isEmpty()) {
        auto empty = std::make_shared<Paragraph>();
        empty->id = QUuid::createUuid();
        ok = !apply(StepBlocks{{}, 0, {Block{empty, {}}}, {}, true});
    }
    Position caret;
    if (ok) {
        paragraphsInOrder();
        // A table's top-left slot is always an anchor cell with a paragraph.
        const auto startOf = [](const Block &block) {
            return block.paragraph ? Position{block.paragraph->id, 0}
                                   : Position{block.table->rows.first().cells.first().paragraphs.first()->id, 0};
        };
        if (index < m_blocks.size()) {
            caret = startOf(m_blocks.at(index));
        } else {
            const Block &before = m_blocks.last();
            caret = before.paragraph ? Position{before.paragraph->id, int(before.paragraph->text.size())}
                                     : startOf(before);
        }
    }
    if (!transaction.finish(ok, caret)) return std::nullopt;
    return caret;
}

bool RichTextDocument::insertTableRow(const QString &tableId, int index)
{
    if (refuseWhileTracking(tr("Inserting a table row"))) return false;
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    if (!current || index < 0 || index > current->rows.size() || current->rows.size() >= MaximumTableRows)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table row insertion"));
    TableGridMap map;
    mapTableGrid(*current, &map);
    const int rows = map.rows;
    const int columns = map.columns;
    // The new row copies the look of the row it joins: a header row inside
    // the header block, otherwise the neighbouring body row. Inside a merged
    // cell it becomes covered and the merged cell grows by one row.
    const int header = current->headerRows;
    const int model = index < header ? index : index > header ? index - 1 : qMin(index, rows - 1);
    auto next = std::make_shared<Table>(*current);
    TableRow row{QUuid::createUuid(), {}, 0.0};
    QSet<int> grown;
    for (int c = 0; c < columns; ++c) {
        if (index > 0 && index < rows && map.anchorOf(index - 1, c) == map.anchorOf(index, c)) {
            grown.insert(map.anchorOf(index, c));
            row.cells.append(TableCell{QUuid::createUuid(), {}, {}, 1, 1});
        } else {
            row.cells.append(cellLike(*this, anchorCell(*current, map, model, c)));
        }
    }
    for (int slot : std::as_const(grown)) ++next->rows[slot / columns].cells[slot % columns].rowSpan;
    next->rows.insert(index, row);
    if (index < header) ++next->headerRows;
    return commitGrid(*this, tr("Insert table row"), current, next);
}

bool RichTextDocument::removeTableRowNow(const QString &tableId, const QString &rowId)
{
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    const int index = current ? rowIndex(*current, QUuid(rowId)) : -1;
    if (index < 0 || current->rows.size() <= 1)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table row deletion"));
    TableGridMap map;
    mapTableGrid(*current, &map);
    const int columns = map.columns;
    auto next = std::make_shared<Table>(*current);
    // A merged cell loses the row; one anchored in it moves to the slot
    // below with its content and identity (that covered slot's ID goes with
    // the deleted row).
    for (int c = 0; c < columns; ++c) {
        const int slot = map.anchorOf(index, c);
        const int anchorRow = slot / columns;
        if (slot % columns != c) continue;
        const TableCell &anchor = current->rows.at(anchorRow).cells.at(c);
        if (anchor.rowSpan == 1) continue;
        if (anchorRow < index) {
            --next->rows[anchorRow].cells[c].rowSpan;
            continue;
        }
        TableCell &below = next->rows[index + 1].cells[c];
        below.id = anchor.id;
        below.paragraphs = anchor.paragraphs;
        below.background = anchor.background;
        below.rowSpan = anchor.rowSpan - 1;
        below.columnSpan = anchor.columnSpan;
    }
    next->rows.removeAt(index);
    if (index < next->headerRows) --next->headerRows;
    next->headerRows = qMin(next->headerRows, int(next->rows.size()) - 1);
    return commitGrid(*this, tr("Delete table row"), current, next);
}

bool RichTextDocument::insertTableColumn(const QString &tableId, int index)
{
    if (refuseWhileTracking(tr("Inserting a table column"))) return false;
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    if (!current || index < 0 || index > current->columnWeights.size()
        || current->columnWeights.size() >= MaximumTableColumns)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table column insertion"));
    TableGridMap map;
    mapTableGrid(*current, &map);
    const int columns = map.columns;
    // Like the column to its left (the first column when inserted first):
    // same relative width, and each row's cell styled like its neighbour.
    // Inside a merged cell the slot is covered and the merged cell widens.
    const int model = qMax(0, index - 1);
    auto next = std::make_shared<Table>(*current);
    next->columnIds.insert(index, QUuid::createUuid());
    next->columnWeights.insert(index, current->columnWeights.at(model));
    QSet<int> grown;
    for (int r = 0; r < map.rows; ++r) {
        if (index > 0 && index < columns && map.anchorOf(r, index - 1) == map.anchorOf(r, index)) {
            grown.insert(map.anchorOf(r, index));
            next->rows[r].cells.insert(index, TableCell{QUuid::createUuid(), {}, {}, 1, 1});
        } else {
            next->rows[r].cells.insert(index, cellLike(*this, anchorCell(*current, map, r, model)));
        }
    }
    // Anchors of widened cells sit left of `index`, so insertion did not move them.
    for (int slot : std::as_const(grown)) ++next->rows[slot / columns].cells[slot % columns].columnSpan;
    return commitGrid(*this, tr("Insert table column"), current, next);
}

bool RichTextDocument::removeTableColumnNow(const QString &tableId, const QString &columnId)
{
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    const int index = current ? columnIndex(*current, QUuid(columnId)) : -1;
    if (index < 0 || current->columnWeights.size() <= 1)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table column deletion"));
    TableGridMap map;
    mapTableGrid(*current, &map);
    const int columns = map.columns;
    auto next = std::make_shared<Table>(*current);
    // A merged cell loses the column; one anchored in it moves to the slot
    // on its right with its content and identity.
    for (int r = 0; r < map.rows; ++r) {
        const int slot = map.anchorOf(r, index);
        const int anchorColumn = slot % columns;
        if (slot / columns != r) continue;
        const TableCell &anchor = current->rows.at(r).cells.at(anchorColumn);
        if (anchor.columnSpan == 1) continue;
        if (anchorColumn < index) {
            --next->rows[r].cells[anchorColumn].columnSpan;
            continue;
        }
        TableCell &right = next->rows[r].cells[index + 1];
        right.id = anchor.id;
        right.paragraphs = anchor.paragraphs;
        right.background = anchor.background;
        right.rowSpan = anchor.rowSpan;
        right.columnSpan = anchor.columnSpan - 1;
    }
    next->columnIds.removeAt(index);
    next->columnWeights.removeAt(index);
    for (TableRow &row : next->rows) row.cells.removeAt(index);
    return commitGrid(*this, tr("Delete table column"), current, next);
}

bool RichTextDocument::setTableColumnWidth(const QString &tableId, const QString &columnId, double width)
{
    if (refuseWhileTracking(tr("Changing a column width"))) return false;
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    const int index = current ? columnIndex(*current, QUuid(columnId)) : -1;
    if (index < 0 || !std::isfinite(width) || width < 1 || width > MaximumColumnWeight)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table column width"));
    if (current->columnWeights.at(index) == width) return true;
    auto next = std::make_shared<Table>(*current);
    next->columnWeights[index] = width;
    return commitGrid(*this, tr("Table column width"), current, next);
}

bool RichTextDocument::setTableCellText(const QString &tableId, const QString &cellId, const QString &text)
{
    // cleanInsertedText maps \r, \r\n and U+2029 to \n: any of them would
    // split the cell's paragraph, so they are rejected like \n itself.
    const QString clean = cleanInsertedText(text);
    const TablePtr t = livePointer(*this, table(QUuid(tableId)));
    if (!t || clean.contains(u'\n') || text.contains(QChar(ObjectChar)))
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid single-paragraph cell text"));
    const QUuid id(cellId);
    Paragraph *p = nullptr;
    for (const TableRow &row : t->rows)
        for (const TableCell &cell : row.cells)
            if (cell.id == id && cell.paragraphs.size() == 1) p = cell.paragraphs.first().get();
    if (!p) return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown or multi-paragraph table cell"));
    if (p->text == clean) return true;
    AutoTransaction transaction(this, tr("Edit table cell"));
    if (!transaction.valid) return false;
    // One replacement step in the format of the text it replaces.
    const int format = formatIndexAt({p->id, p->text.isEmpty() ? 0 : 1});
    if (m_trackChanges) {
        // Tracked (WP-052): the old text stays, marked deleted, and the new
        // text is an insertion in front of it.
        const std::optional<Position> start = deleteRange({p->id, 0}, {p->id, int(p->text.size())});
        const std::optional<Position> end = start ? insertText(*start, clean, format) : std::nullopt;
        return transaction.finish(end.has_value(), end.value_or(Position{p->id, 0}));
    }
    const QVector<Span> spans = clean.isEmpty() ? QVector<Span>{} : QVector<Span>{{int(clean.size()), format}};
    const bool ok = !apply(textStep(*this, *p, 0, int(p->text.size()), clean, spans));
    return transaction.finish(ok, {p->id, int(clean.size())});
}

} // namespace QindaTK
