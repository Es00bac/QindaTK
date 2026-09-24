// SPDX-License-Identifier: LGPL-3.0-or-later
// Stable-identity table grid edits (StepTable) and the TableEditor snapshot.
#include "rich_text_document.h"
#include "rich_text_ops_p.h"
#include <cmath>

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

namespace {

// The limits insertTable() and the codec apply.
constexpr int MaximumRows = 10000;
constexpr int MaximumColumns = 64;
constexpr double MaximumColumnWeight = 1e6;

// Collects a grid's paragraph IDs. False when a cell is empty or a paragraph
// appears twice: versions of one grid share paragraphs, one grid never does.
bool collectParagraphIds(const Table &table, QSet<QUuid> *ids)
{
    for (const TableRow &row : table.rows)
        for (const TableCell &cell : row.cells) {
            if (cell.paragraphs.isEmpty()) return false;
            for (const ParagraphPtr &p : cell.paragraphs) {
                if (!p || ids->contains(p->id)) return false;
                ids->insert(p->id);
            }
        }
    return true;
}

// Why a replacement grid cannot be applied, or an empty string.
QString gridProblem(const Table &t, const QUuid &tableId)
{
    const qsizetype columns = t.columnWeights.size();
    if (t.id != tableId || t.rows.isEmpty() || t.rows.size() > MaximumRows || columns == 0
        || columns > MaximumColumns || t.columnIds.size() != columns || t.headerRows < 0
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
    return {};
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

// A new empty cell shaped like `model`: its background plus the typing
// format and paragraph format of its first paragraph (without list
// membership), so an inserted row or column looks like its neighbour.
TableCell cellLike(const RichTextDocument &doc, const TableCell &model)
{
    const Paragraph &source = *model.paragraphs.first();
    auto paragraph = std::make_shared<Paragraph>();
    paragraph->id = QUuid::createUuid();
    paragraph->markFormat = doc.charFormat(source.markFormat).href.isEmpty() ? source.markFormat : 0;
    paragraph->format = source.format;
    paragraph->format.listId.clear();
    paragraph->format.listLevel = 0;
    paragraph->format.pageBreakBefore = false;
    return {QUuid::createUuid(), {paragraph}, model.background};
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

// Where an anchor in a removed cell of `from` goes in `to`: the cell at its
// row and column when both survive, otherwise the next surviving row or
// column (the previous one at the end), so a caret stays beside the edit.
Position nearestCell(const Table &from, const Table &to, const QUuid &paragraph)
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
                return {to.rows.at(qMax(0, row)).cells.at(qMax(0, column)).paragraphs.first()->id, 0};
            }
    return {to.rows.first().cells.first().paragraphs.first()->id, 0};
}

// The shared pointer that owns a registered table, or null. Rebuilds the
// document order first: Table::blockIndex is stale after a paragraph merge.
TablePtr livePointer(const RichTextDocument &doc, const Table *t)
{
    if (!t) return {};
    doc.paragraphsInOrder();
    const Block block = doc.blocks().value(t->blockIndex);
    return block.table.get() == t ? block.table : TablePtr{};
}

// Applies one StepTable replacing `current` with `next`, as its own undo
// step unless a transaction is already open.
bool commitGrid(RichTextDocument &doc, const QString &label, const TablePtr &current,
                const TablePtr &next)
{
    AutoTransaction transaction(&doc, label);
    if (!transaction.valid) return false;
    StepTable step{current->id, current, next, objectsRemoved(doc, *current, *next)};
    return transaction.finish(!doc.apply(std::move(step)), {});
}

} // namespace

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
    for (PositionAnchor *anchor : std::as_const(m_anchors)) {
        const QUuid id = anchor->position.paragraph;
        if (oldIds.contains(id) && !newIds.contains(id)) anchor->position = nearestCell(*from, *to, id);
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

QVariantList RichTextDocument::tableSnapshots() const
{
    QVariantList result;
    for (const Block &block : m_blocks)
        if (block.table) result.append(tableSnapshot(block.table->id.toString(QUuid::WithoutBraces)));
    return result;
}

QVariantMap RichTextDocument::tableSnapshot(const QString &tableId) const
{
    const Table *t = table(QUuid(tableId));
    if (!t) return {};
    QVariantList rows, columns, cells;
    for (int c = 0; c < t->columnWeights.size(); ++c)
        columns.append(QVariantMap{{QStringLiteral("id"), t->columnIds.value(c).toString(QUuid::WithoutBraces)},
                                   {QStringLiteral("label"), QString::number(c + 1)},
                                   {QStringLiteral("width"), t->columnWeights.at(c)}});
    for (int r = 0; r < t->rows.size(); ++r) {
        const TableRow &row = t->rows.at(r);
        const QString rowId = row.id.toString(QUuid::WithoutBraces);
        rows.append(QVariantMap{{QStringLiteral("id"), rowId}, {QStringLiteral("label"), QString::number(r + 1)},
                                {QStringLiteral("header"), r < t->headerRows}});
        for (int c = 0; c < row.cells.size(); ++c) {
            const TableCell &cell = row.cells.at(c);
            QStringList lines, paragraphIds;
            for (const ParagraphPtr &p : cell.paragraphs) {
                lines.append(p->text);
                paragraphIds.append(p->id.toString(QUuid::WithoutBraces));
            }
            cells.append(QVariantMap{{QStringLiteral("id"), cell.id.toString(QUuid::WithoutBraces)},
                                     {QStringLiteral("rowId"), rowId},
                                     {QStringLiteral("columnId"), t->columnIds.value(c).toString(QUuid::WithoutBraces)},
                                     {QStringLiteral("rowIndex"), r}, {QStringLiteral("columnIndex"), c},
                                     {QStringLiteral("rowSpan"), 1}, {QStringLiteral("columnSpan"), 1},
                                     {QStringLiteral("text"), lines.join(u'\n')},
                                     {QStringLiteral("paragraphIds"), paragraphIds}});
        }
    }
    return {{QStringLiteral("id"), t->id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("rows"), rows}, {QStringLiteral("columns"), columns},
            {QStringLiteral("cells"), cells}, {QStringLiteral("headerRows"), t->headerRows},
            {QStringLiteral("canMerge"), false},
            {QStringLiteral("maxRows"), MaximumRows}, {QStringLiteral("maxColumns"), MaximumColumns}};
}

bool RichTextDocument::insertTableRow(const QString &tableId, int index)
{
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    if (!current || index < 0 || index > current->rows.size() || current->rows.size() >= MaximumRows)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table row insertion"));
    // The new row copies the look of the row it joins: a header row inside
    // the header block, otherwise the neighbouring body row.
    const int header = current->headerRows;
    const int model = index < header ? index
        : index > header ? index - 1 : qMin(index, int(current->rows.size()) - 1);
    TableRow row{QUuid::createUuid(), {}, 0.0};
    for (const TableCell &cell : current->rows.at(model).cells) row.cells.append(cellLike(*this, cell));
    auto next = std::make_shared<Table>(*current);
    next->rows.insert(index, row);
    if (index < header) ++next->headerRows;
    return commitGrid(*this, tr("Insert table row"), current, next);
}

bool RichTextDocument::removeTableRow(const QString &tableId, const QString &rowId)
{
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    const int index = current ? rowIndex(*current, QUuid(rowId)) : -1;
    if (index < 0 || current->rows.size() <= 1)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table row deletion"));
    auto next = std::make_shared<Table>(*current);
    next->rows.removeAt(index);
    if (index < next->headerRows) --next->headerRows;
    next->headerRows = qMin(next->headerRows, int(next->rows.size()) - 1);
    return commitGrid(*this, tr("Delete table row"), current, next);
}

bool RichTextDocument::insertTableColumn(const QString &tableId, int index)
{
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    if (!current || index < 0 || index > current->columnWeights.size()
        || current->columnWeights.size() >= MaximumColumns)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table column insertion"));
    // Like the column to its left (the first column when inserted first):
    // same relative width, and each row's cell styled like its neighbour.
    const int model = qMax(0, index - 1);
    auto next = std::make_shared<Table>(*current);
    next->columnIds.insert(index, QUuid::createUuid());
    next->columnWeights.insert(index, current->columnWeights.at(model));
    for (TableRow &row : next->rows) row.cells.insert(index, cellLike(*this, row.cells.at(model)));
    return commitGrid(*this, tr("Insert table column"), current, next);
}

bool RichTextDocument::removeTableColumn(const QString &tableId, const QString &columnId)
{
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    const int index = current ? columnIndex(*current, QUuid(columnId)) : -1;
    if (index < 0 || current->columnWeights.size() <= 1)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table column deletion"));
    auto next = std::make_shared<Table>(*current);
    next->columnIds.removeAt(index);
    next->columnWeights.removeAt(index);
    for (TableRow &row : next->rows) row.cells.removeAt(index);
    return commitGrid(*this, tr("Delete table column"), current, next);
}

bool RichTextDocument::setTableColumnWidth(const QString &tableId, const QString &columnId, double width)
{
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
    const QVector<Span> spans = clean.isEmpty() ? QVector<Span>{} : QVector<Span>{{int(clean.size()), format}};
    const bool ok = !apply(textStep(*this, *p, 0, int(p->text.size()), clean, spans));
    return transaction.finish(ok, {p->id, int(clean.size())});
}

} // namespace QindaTK
