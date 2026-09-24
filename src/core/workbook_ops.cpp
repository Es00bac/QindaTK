// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_p.h"

namespace QindaTK {
using namespace Calc;
namespace {
int positiveModulo(qint64 value, int modulus) { return int(((value % modulus) + modulus) % modulus); }
bool rectangleInside(const Sheet &s, int row, int column, int rows, int columns)
{
    return row >= 0 && column >= 0 && rows >= 1 && columns >= 1
        && qint64(row) + rows <= s.rows && qint64(column) + columns <= s.columns;
}
QString kindName(const CellContent &content)
{
    if (content.kind == CellContent::Kind::Empty) return QStringLiteral("empty");
    if (content.kind == CellContent::Kind::Formula) return QStringLiteral("formula");
    return typeName(content.literal.type);
}
} // namespace

QVariantList Workbook::visibleCells(const QString &sheetId, int firstRow, int rowCount,
                                    int firstColumn, int columnCount) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s || !rectangleInside(*s, firstRow, firstColumn, rowCount, columnCount)
        || qint64(rowCount) * columnCount > MaxVisibleCells) return {};
    QVariantList result;
    for (int r = firstRow; r < firstRow + rowCount; ++r)
        for (int c = firstColumn; c < firstColumn + columnCount; ++c) {
            const Cell *cell = d->findCell(*s, r, c);
            if (!cell) continue;
            result.append(QVariantMap{{"row", r}, {"column", c}, {"display", d->displayAt(*s, r, c, cell->value)},
                                      {"type", typeName(cell->value.type)},
                                      {"formula", cell->content.kind == CellContent::Kind::Formula}});
        }
    return result;
}

QVariantMap Workbook::usedRange(const QString &sheetId) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s || s->cells.empty()) return {};
    int lastColumn = 0;
    for (const auto &[position, cell] : s->cells) lastColumn = std::max(lastColumn, keyColumn(position));
    return {{"lastRow", keyRow(s->cells.rbegin()->first)}, {"lastColumn", lastColumn}};
}

QVariantMap Workbook::checkInput(const QString &sheetId, const QString &input) const
{
    QVariantMap result{{"ok", true}, {"message", QString()}, {"start", 0}, {"end", 0},
                       {"references", QVariantList()}};
    if (!d->sheet(sheetId)) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("message"), QStringLiteral("Unknown sheet"));
        return result;
    }
    CellContent content;
    ParseError error;
    if (!d->parseInput(sheetId, input, &content, &error)) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("message"), error.message);
        result.insert(QStringLiteral("start"), error.start);
        result.insert(QStringLiteral("end"), error.end);
        return result;
    }
    result.insert(QStringLiteral("kind"), kindName(content));
    const Sheet &host = *d->sheet(sheetId);
    result.insert(QStringLiteral("canonical"), d->inputText(host, content));
    if (content.kind != CellContent::Kind::Formula) return result;
    QVariantList references;
    for (const Node &n : content.formula->nodes) {
        if (n.kind != Node::Kind::Cell && n.kind != Node::Kind::Range) continue;
        const QString target = n.sheet >= 0 ? content.formula->sheets.at(n.sheet) : sheetId;
        const RefPoint last = n.kind == Node::Kind::Range ? n.last : n.first;
        references.append(QVariantMap{{"start", n.start}, {"end", n.end},
                                      {"text", input.mid(n.start, n.end - n.start)}, {"sheetId", target},
                                      {"firstRow", n.first.row}, {"firstColumn", n.first.column},
                                      {"lastRow", last.row}, {"lastColumn", last.column}});
    }
    result.insert(QStringLiteral("references"), references);
    return result;
}

bool Workbook::copyCells(const QString &sourceSheetId, int firstRow, int firstColumn, int rowCount,
                         int columnCount, const QString &targetSheetId, int targetRow, int targetColumn)
{
    return d->mutate(QStringLiteral("Copy cells"), [&](QString *error, QVariantMap *) {
        const Sheet *source = d->sheet(sourceSheetId);
        Sheet *target = d->sheet(targetSheetId);
        if (!source || !target) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*target, error)) return false;
        if (!rectangleInside(*source, firstRow, firstColumn, rowCount, columnCount)
            || !rectangleInside(*target, targetRow, targetColumn, rowCount, columnCount)) {
            *error = QStringLiteral("Copy range is outside the sheet");
            return false;
        }
        if (qint64(rowCount) * columnCount > MaxTransactionCells) {
            *error = QStringLiteral("Copy exceeds %1 cells").arg(MaxTransactionCells);
            return false;
        }
        Mutation m;
        m.sheetId = targetSheetId;
        for (int r = 0; r < rowCount; ++r)
            for (int c = 0; c < columnCount; ++c) {
                CellContent content = d->contentAt(*source, firstRow + r, firstColumn + c);
                if (content.kind == CellContent::Kind::Formula)
                    content.formula = offsetFormula(content.formula, targetRow - firstRow,
                                                    targetColumn - firstColumn);
                m.cells.push_back({targetRow + r, targetColumn + c, std::move(content)});
            }
        if (!d->record(std::move(m), error)) return false;
        const Area block{firstRow, firstColumn, firstRow + rowCount - 1, firstColumn + columnCount - 1};
        const Area to{targetRow, targetColumn, targetRow + rowCount - 1, targetColumn + columnCount - 1};
        return d->copyFormats(*source, block, *target, to, targetRow, targetColumn, true, error);
    });
}

// AGENT-NOTE: fill repeats the source block over the target in both
// directions, aligned to the source origin, and moves each copied formula
// by its distance from the source cell it came from. It copies; it does not
// extrapolate numeric series (docs/workbook.md).
bool Workbook::fill(const QString &sheetId, int firstRow, int firstColumn, int rowCount, int columnCount,
                    int targetRow, int targetColumn, int targetRowCount, int targetColumnCount)
{
    return d->mutate(QStringLiteral("Fill cells"), [&](QString *error, QVariantMap *) {
        Sheet *s = d->sheet(sheetId);
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        if (!rectangleInside(*s, firstRow, firstColumn, rowCount, columnCount)
            || !rectangleInside(*s, targetRow, targetColumn, targetRowCount, targetColumnCount)) {
            *error = QStringLiteral("Fill range is outside the sheet");
            return false;
        }
        if (qint64(targetRowCount) * targetColumnCount > MaxTransactionCells
            || qint64(rowCount) * columnCount > MaxTransactionCells) {
            *error = QStringLiteral("Fill exceeds %1 cells").arg(MaxTransactionCells);
            return false;
        }
        std::vector<CellContent> source(size_t(rowCount) * size_t(columnCount));
        for (int r = 0; r < rowCount; ++r)
            for (int c = 0; c < columnCount; ++c)
                source[size_t(r) * size_t(columnCount) + size_t(c)] = d->contentAt(*s, firstRow + r, firstColumn + c);
        Mutation m;
        m.sheetId = sheetId;
        for (int r = targetRow; r < targetRow + targetRowCount; ++r)
            for (int c = targetColumn; c < targetColumn + targetColumnCount; ++c) {
                const int sr = positiveModulo(qint64(r) - firstRow, rowCount);
                const int sc = positiveModulo(qint64(c) - firstColumn, columnCount);
                CellContent content = source[size_t(sr) * size_t(columnCount) + size_t(sc)];
                if (content.kind == CellContent::Kind::Formula)
                    content.formula = offsetFormula(content.formula, r - (firstRow + sr), c - (firstColumn + sc));
                m.cells.push_back({r, c, std::move(content)});
            }
        if (!d->record(std::move(m), error)) return false;
        const Area block{firstRow, firstColumn, firstRow + rowCount - 1, firstColumn + columnCount - 1};
        const Area to{targetRow, targetColumn, targetRow + targetRowCount - 1, targetColumn + targetColumnCount - 1};
        return d->copyFormats(*s, block, *s, to, firstRow, firstColumn, false, error);
    });
}

bool Workbook::clearCells(const QString &sheetId, int firstRow, int firstColumn, int rowCount, int columnCount)
{
    return d->mutate(QStringLiteral("Clear cells"), [&](QString *error, QVariantMap *) {
        const Sheet *s = d->sheet(sheetId);
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        if (!rectangleInside(*s, firstRow, firstColumn, rowCount, columnCount)) {
            *error = QStringLiteral("Clear range is outside the sheet");
            return false;
        }
        Mutation m;
        m.sheetId = sheetId;
        const int lastRow = firstRow + rowCount - 1, lastColumn = firstColumn + columnCount - 1;
        auto it = s->cells.lower_bound(positionKey(firstRow, firstColumn));
        const auto end = s->cells.upper_bound(positionKey(lastRow, lastColumn));
        for (; it != end; ++it) {
            const int c = keyColumn(it->first);
            if (c < firstColumn || c > lastColumn) continue;
            m.cells.push_back({keyRow(it->first), c, CellContent{}});
            if (m.cells.size() > size_t(MaxTransactionCells)) {
                *error = QStringLiteral("Clear exceeds %1 cells").arg(MaxTransactionCells);
                return false;
            }
        }
        return d->record(std::move(m), error);
    });
}

namespace {
bool structural(WorkbookPrivate *d, const QString &label, Mutation::Kind kind, const QString &sheetId,
                int at, int count)
{
    return d->mutate(label, [&](QString *error, QVariantMap *) {
        const Sheet *s = d->sheet(sheetId);
        if (s && !d->sheetEditable(*s, error)) return false;
        Mutation m;
        m.kind = kind;
        m.sheetId = sheetId;
        m.at = at;
        m.count = count;
        return d->record(std::move(m), error);
    });
}
}
bool Workbook::insertRows(const QString &sheetId, int row, int count)
{ return structural(d.get(), QStringLiteral("Insert rows"), Mutation::Kind::InsertRows, sheetId, row, count); }
bool Workbook::removeRows(const QString &sheetId, int row, int count)
{ return structural(d.get(), QStringLiteral("Remove rows"), Mutation::Kind::RemoveRows, sheetId, row, count); }
bool Workbook::insertColumns(const QString &sheetId, int column, int count)
{ return structural(d.get(), QStringLiteral("Insert columns"), Mutation::Kind::InsertColumns, sheetId, column, count); }
bool Workbook::removeColumns(const QString &sheetId, int column, int count)
{ return structural(d.get(), QStringLiteral("Remove columns"), Mutation::Kind::RemoveColumns, sheetId, column, count); }

QString Workbook::functionSignature(const QString &name) const
{
    const FunctionSpec *spec = functionByName(name);
    return spec ? QString::fromLatin1(spec->signature) : QString();
}
QString Workbook::address(int row, int column) const
{
    if (row < 0 || row >= MaxRows || column < 0 || column >= MaxColumns) return {};
    return cellAddress(row, column);
}
QString Workbook::rangeAddress(int firstRow, int firstColumn, int lastRow, int lastColumn) const
{
    const QString first = address(std::min(firstRow, lastRow), std::min(firstColumn, lastColumn));
    const QString last = address(std::max(firstRow, lastRow), std::max(firstColumn, lastColumn));
    if (first.isEmpty() || last.isEmpty()) return {};
    return first == last ? first : first + QLatin1Char(':') + last;
}
QVariantMap Workbook::parseAddress(const QString &text) const
{
    const QString trimmed = text.trimmed();
    const qsizetype colon = trimmed.indexOf(QLatin1Char(':'));
    RefPoint a, b;
    const bool ok = colon < 0 ? parseCellAddress(trimmed, &a)
                              : parseCellAddress(QStringView(trimmed).left(colon), &a)
                                    && parseCellAddress(QStringView(trimmed).mid(colon + 1), &b);
    if (!ok) return {{"ok", false}};
    if (colon < 0) b = a;
    return {{"ok", true}, {"firstRow", std::min(a.row, b.row)}, {"firstColumn", std::min(a.column, b.column)},
            {"lastRow", std::max(a.row, b.row)}, {"lastColumn", std::max(a.column, b.column)}};
}
QString Workbook::referenceText(const QString &fromSheetId, const QString &toSheetId, int firstRow,
                                int firstColumn, int lastRow, int lastColumn) const
{
    const QString range = rangeAddress(firstRow, firstColumn, lastRow, lastColumn);
    if (range.isEmpty() || fromSheetId == toSheetId || !d->sheet(toSheetId)) return range;
    return quoteSheetName(d->nameForId(toSheetId)) + QLatin1Char('!') + range;
}

} // namespace QindaTK
