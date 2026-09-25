// SPDX-License-Identifier: LGPL-3.0-or-later
// Merged cells, table styles and the TableEditor snapshot (WP-051).
#include "rich_text_document.h"
#include "rich_text_ops_p.h"
#include <algorithm>

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

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
    TableGridMap map;
    mapTableGrid(*t, &map);
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
        // Visible cells only: slots a merged cell covers are part of it.
        for (int c = 0; c < row.cells.size(); ++c) {
            if (map.covered(r, c)) continue;
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
                                     {QStringLiteral("rowSpan"), cell.rowSpan}, {QStringLiteral("columnSpan"), cell.columnSpan},
                                     {QStringLiteral("text"), lines.join(u'\n')},
                                     {QStringLiteral("paragraphIds"), paragraphIds}});
        }
    }
    QVariantList styles;
    for (const NamedStyle &named : m_styles)
        if (named.kind == StyleKind::Table)
            styles.append(QVariantMap{{QStringLiteral("id"), named.id}, {QStringLiteral("label"), named.name}});
    std::sort(styles.begin(), styles.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("label")).toString().localeAwareCompare(
                   b.toMap().value(QStringLiteral("label")).toString()) < 0;
    });
    if (!styles.isEmpty())
        styles.prepend(QVariantMap{{QStringLiteral("id"), QString()}, {QStringLiteral("label"), tr("No table style")}});
    return {{QStringLiteral("id"), t->id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("rows"), rows}, {QStringLiteral("columns"), columns},
            {QStringLiteral("cells"), cells}, {QStringLiteral("headerRows"), t->headerRows},
            {QStringLiteral("canMerge"), true}, {QStringLiteral("styleId"), t->styleId},
            {QStringLiteral("styles"), styles},
            {QStringLiteral("maxRows"), MaximumTableRows}, {QStringLiteral("maxColumns"), MaximumTableColumns}};
}

bool RichTextDocument::mergeTableCells(const QString &tableId, const QStringList &cellIds)
{
    if (refuseWhileTracking(tr("Merging table cells"))) return false;
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    if (!current) return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown table"));
    TableGridMap map;
    mapTableGrid(*current, &map);
    const int columns = map.columns;
    QSet<int> chosen;
    for (const QString &id : cellIds) {
        const int slot = slotOfCell(*current, QUuid(id));
        if (slot < 0 || map.covered(slot / columns, slot % columns))
            return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown or covered table cell"));
        chosen.insert(slot);
    }
    if (chosen.size() < 2) return !reject(ErrorCode::InvalidArgument, QStringLiteral("Select at least two cells to merge"));
    int top = map.rows, left = columns, bottom = -1, right = -1;
    for (int slot : std::as_const(chosen)) {
        const TableCell &cell = current->rows.at(slot / columns).cells.at(slot % columns);
        top = qMin(top, slot / columns);
        left = qMin(left, slot % columns);
        bottom = qMax(bottom, slot / columns + cell.rowSpan - 1);
        right = qMax(right, slot % columns + cell.columnSpan - 1);
    }
    // The chosen cells must tile their bounding rectangle exactly.
    for (int r = top; r <= bottom; ++r)
        for (int c = left; c <= right; ++c)
            if (!chosen.contains(map.anchorOf(r, c)))
                return !reject(ErrorCode::Validation, QStringLiteral("Merged cells must form a rectangle"));
    if (top < current->headerRows && bottom >= current->headerRows)
        return !reject(ErrorCode::Validation, QStringLiteral("Merged cells cannot cross the header rows"));
    // Content in reading order; cells holding one empty paragraph add nothing.
    QVector<ParagraphPtr> content;
    for (int r = top; r <= bottom; ++r)
        for (int c = left; c <= right; ++c) {
            if (map.anchorOf(r, c) != r * columns + c) continue;
            const QVector<ParagraphPtr> &paragraphs = current->rows.at(r).cells.at(c).paragraphs;
            if (paragraphs.size() == 1 && paragraphs.first()->text.isEmpty()) continue;
            content += paragraphs;
        }
    if (content.isEmpty()) content = current->rows.at(top).cells.at(left).paragraphs;
    auto next = std::make_shared<Table>(*current);
    for (int r = top; r <= bottom; ++r)
        for (int c = left; c <= right; ++c) {
            TableCell &cell = next->rows[r].cells[c];
            if (r == top && c == left) {
                cell.paragraphs = content;
                cell.rowSpan = bottom - top + 1;
                cell.columnSpan = right - left + 1;
                continue;
            }
            cell.paragraphs.clear();
            cell.background = QColor();
            cell.rowSpan = cell.columnSpan = 1;
        }
    return commitGrid(*this, tr("Merge table cells"), current, next);
}

bool RichTextDocument::splitTableCell(const QString &tableId, const QString &cellId)
{
    if (refuseWhileTracking(tr("Splitting a table cell"))) return false;
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    const int slot = current ? slotOfCell(*current, QUuid(cellId)) : -1;
    const int columns = current ? int(current->columnWeights.size()) : 1;
    const TableCell *anchor = slot < 0 ? nullptr : &current->rows.at(slot / columns).cells.at(slot % columns);
    if (!anchor || anchor->paragraphs.isEmpty() || (anchor->rowSpan == 1 && anchor->columnSpan == 1))
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Only a merged cell can be split"));
    // The content stays in the first cell; the covered slots come back with
    // their own identities and an empty paragraph shaped like the first cell.
    auto next = std::make_shared<Table>(*current);
    const int row = slot / columns;
    const int column = slot % columns;
    for (int r = row; r < row + anchor->rowSpan; ++r)
        for (int c = column; c < column + anchor->columnSpan; ++c) {
            TableCell &cell = next->rows[r].cells[c];
            cell.rowSpan = cell.columnSpan = 1;
            if (r == row && c == column) continue;
            const TableCell fresh = cellLike(*this, *anchor);
            cell.paragraphs = fresh.paragraphs;
            cell.background = fresh.background;
        }
    return commitGrid(*this, tr("Split table cell"), current, next);
}

bool RichTextDocument::setTableStyle(const QString &tableId, const QString &styleId)
{
    if (refuseWhileTracking(tr("Changing a table style"))) return false;
    const TablePtr current = livePointer(*this, table(QUuid(tableId)));
    const NamedStyle *named = style(styleId);
    if (!current || (!styleId.isEmpty() && (!named || named->kind != StyleKind::Table)))
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown table or table style"));
    if (current->styleId == styleId) return true;
    auto next = std::make_shared<Table>(*current);
    next->styleId = styleId;
    return commitGrid(*this, tr("Table style"), current, next);
}

} // namespace QindaTK
