// SPDX-License-Identifier: LGPL-3.0-or-later
#include "editable_grid_model.h"

#include <QUuid>
#include <algorithm>

// Domain-source and compact-identity additions to EditableGridModel
// (docs/editable-grid.md#domain-sources). Everything here is opt-in: a grid
// without a GridCellSourceExtension or sourceCommits behaves as before.
namespace QindaTK {
namespace {
constexpr int MaxTransactionCells = 65536;
constexpr int MaxMergedRegions = 1024;
constexpr int MaxVisibleCells = 4096;
int positiveModulo(qint64 value, int modulus) { return int(((value % modulus) + modulus) % modulus); }
QVariantMap mergeRecord(const QRect &area, const QString &rowId, const QString &columnId)
{
    return {{"firstRow", area.top()}, {"firstColumn", area.left()}, {"lastRow", area.bottom()},
            {"lastColumn", area.right()}, {"rowId", rowId}, {"columnId", columnId}};
}
}

GridCellSourceExtension *EditableGridModel::extension() const
{
    return m_source ? dynamic_cast<GridCellSourceExtension *>(m_source.data()) : nullptr;
}

void EditableGridModel::setSourceCommits(bool enabled)
{
    if (m_sourceCommits == enabled) return;
    cancelEdit();
    m_sourceCommits = enabled;
    m_overrides.clear(); m_sparseCount = 0;
    m_history.clear(); m_externalSteps.clear(); m_historyCursor = 0;
    ++m_revision;
    emit historyChanged();
    emit cellsChanged();
}

QVariant EditableGridModel::editValue(const QString &row, const QString &column) const
{
    if (!m_rows.contains(row) || !m_columns.contains(column)) return {};
    if (const auto it = m_overrides.constFind(key(row, column)); it != m_overrides.cend()) return it.value();
    if (const auto *hooks = extension()) return hooks->editValue(row, column);
    return m_source ? m_source->value(row, column) : QVariant{};
}

void EditableGridModel::notifySourceChanged(int firstRow, int rowCount, int firstColumn, int columnCount)
{
    if (rows() == 0 || columns() == 0 || rowCount <= 0 || columnCount <= 0) return;
    const int r1 = std::clamp(firstRow, 0, rows() - 1), c1 = std::clamp(firstColumn, 0, columns() - 1);
    const int r2 = std::clamp(int(std::min<qint64>(qint64(firstRow) + rowCount - 1, rows() - 1)), r1, rows() - 1);
    const int c2 = std::clamp(int(std::min<qint64>(qint64(firstColumn) + columnCount - 1, columns() - 1)),
                              c1, columns() - 1);
    ++m_revision;
    emit dataChanged(index(r1, c1), index(r2, c2));
    emit cellsChanged();
}

bool EditableGridModel::mergeInside(const QRect &area) const
{
    return area.isValid() && area.left() >= 0 && area.top() >= 0 && area.bottom() < rows()
        && area.right() < columns() && (area.width() > 1 || area.height() > 1);
}

QVariantMap EditableGridModel::cellRecord(int row, int column, const GridCellSourceExtension *hooks) const
{
    const QString rowId = m_rows.at(row), columnId = m_columns.at(column);
    QVariantMap cell{{"row", row}, {"column", column}, {"rowId", rowId}, {"columnId", columnId},
                     {"value", cellValue(rowId, columnId)}};
    if (hooks) {
        const QVariantMap style = hooks->cellStyle(rowId, columnId);
        if (!style.isEmpty()) cell.insert(QStringLiteral("style"), style);
    }
    return cell;
}

QVariantList EditableGridModel::mergedRegions(int firstRow, int rowCount, int firstColumn, int columnCount) const
{
    const auto *hooks = extension();
    if (!hooks || firstRow < 0 || firstColumn < 0 || rowCount <= 0 || columnCount <= 0) return {};
    QVariantList result;
    for (const QRect &area : hooks->mergedRanges(QRect(firstColumn, firstRow, columnCount, rowCount))) {
        if (!mergeInside(area)) continue;
        result.append(mergeRecord(area, rowIdAt(area.top()), columnIdAt(area.left())));
        if (result.size() >= MaxMergedRegions) break;
    }
    return result;
}

QVariantMap EditableGridModel::mergeAt(int row, int column) const
{
    const auto *hooks = extension();
    if (!hooks || row < 0 || column < 0 || row >= rows() || column >= columns()) return {};
    for (const QRect &area : hooks->mergedRanges(QRect(column, row, 1, 1)))
        if (area.contains(column, row) && mergeInside(area))
            return mergeRecord(area, rowIdAt(area.top()), columnIdAt(area.left()));
    return {};
}

QVariantMap EditableGridModel::mergeBounds(int firstRow, int firstColumn, int lastRow, int lastColumn) const
{
    if (rows() == 0 || columns() == 0) return {};
    const auto row = [this](int v) { return std::clamp(v, 0, rows() - 1); };
    const auto column = [this](int v) { return std::clamp(v, 0, columns() - 1); };
    QRect box(QPoint(column(std::min(firstColumn, lastColumn)), row(std::min(firstRow, lastRow))),
              QPoint(column(std::max(firstColumn, lastColumn)), row(std::max(firstRow, lastRow))));
    // Growing the box can touch further areas; it only grows, so this ends.
    if (const auto *hooks = extension())
        for (bool grown = true; grown;) {
            grown = false;
            for (const QRect &area : hooks->mergedRanges(box))
                if (mergeInside(area) && box.intersects(area) && !box.contains(area)) {
                    box = box.united(area);
                    grown = true;
                }
        }
    return {{"firstRow", box.top()}, {"firstColumn", box.left()}, {"lastRow", box.bottom()},
            {"lastColumn", box.right()}};
}

QVariantList EditableGridModel::layoutCells(qreal viewportWidth, qreal viewportHeight,
                                            qreal scrollX, qreal scrollY) const
{
    QVariantList result;
    const auto *hooks = extension();
    const auto place = [&](QVariantMap cell, const QRect &area, bool frozen) {
        const QRectF first = cellRect(area.top(), area.left(), scrollX, scrollY);
        const QRectF last = cellRect(area.bottom(), area.right(), scrollX, scrollY);
        cell.insert(QStringLiteral("x"), first.x());
        cell.insert(QStringLiteral("y"), first.y());
        cell.insert(QStringLiteral("width"), last.x() + last.width() - first.x());
        cell.insert(QStringLiteral("height"), last.y() + last.height() - first.y());
        cell.insert(QStringLiteral("frozen"), frozen);
        if (area.width() > 1 || area.height() > 1) {
            cell.insert(QStringLiteral("rowSpan"), area.height());
            cell.insert(QStringLiteral("columnSpan"), area.width());
        }
        result.append(cell);
    };
    for (const QVariant &item : visibleRegions(viewportWidth, viewportHeight, scrollX, scrollY)) {
        const QVariantMap region = item.toMap();
        const QRect window(region.value(QStringLiteral("firstColumn")).toInt(),
                           region.value(QStringLiteral("firstRow")).toInt(),
                           region.value(QStringLiteral("columnCount")).toInt(),
                           region.value(QStringLiteral("rowCount")).toInt());
        const bool frozenRow = region.value(QStringLiteral("frozenRow")).toBool();
        const bool frozenColumn = region.value(QStringLiteral("frozenColumn")).toBool();
        // AGENT-NOTE: panes scroll independently, so a merged area crossing a
        // frozen edge is drawn once per pane, clipped to it; the part holding
        // the anchor shows the value, other parts only its presentation.
        const QRect pane(QPoint(frozenColumn ? 0 : m_frozenColumns, frozenRow ? 0 : m_frozenRows),
                         QPoint(frozenColumn ? m_frozenColumns - 1 : columns() - 1,
                                frozenRow ? m_frozenRows - 1 : rows() - 1));
        QVector<bool> covered(qsizetype(window.width()) * window.height(), false);
        QVector<std::pair<QRect, QRect>> parts;
        if (hooks)
            for (const QRect &area : hooks->mergedRanges(window)) {
                const QRect part = area.intersected(pane), shown = part.intersected(window);
                if (!mergeInside(area) || shown.isEmpty()) continue;
                for (int r = shown.top(); r <= shown.bottom(); ++r)
                    for (int c = shown.left(); c <= shown.right(); ++c)
                        covered[qsizetype(r - window.top()) * window.width() + (c - window.left())] = true;
                parts.append({area, part});
            }
        for (int r = window.top(); r <= window.bottom(); ++r)
            for (int c = window.left(); c <= window.right(); ++c) {
                if (covered[qsizetype(r - window.top()) * window.width() + (c - window.left())]) continue;
                if (result.size() >= MaxVisibleCells) return result;
                place(cellRecord(r, c, hooks), QRect(c, r, 1, 1), frozenRow || frozenColumn);
            }
        for (const auto &[area, part] : std::as_const(parts)) {
            if (result.size() >= MaxVisibleCells) return result;
            QVariantMap cell = cellRecord(part.top(), part.left(), hooks);
            if (part.topLeft() != area.topLeft()) {
                const QVariantMap anchor = cellRecord(area.top(), area.left(), hooks);
                cell.insert(QStringLiteral("value"), QVariant());
                cell.remove(QStringLiteral("style"));
                if (anchor.contains(QStringLiteral("style")))
                    cell.insert(QStringLiteral("style"), anchor.value(QStringLiteral("style")));
                cell.insert(QStringLiteral("mergeContinuation"), true);
            }
            place(cell, part, frozenRow || frozenColumn);
        }
    }
    return result;
}

bool EditableGridModel::commitToSource(const QVector<Change> &changes, const QString &label)
{
    auto *hooks = extension();
    if (!hooks) return fail(QStringLiteral("Grid source cannot commit changes"));
    if (changes.isEmpty()) { cancelEdit(); return true; }
    const QVariantList payload = changePayload(changes);
    QString error;
    // AGENT-GUARD: the edit stays open when the source rejects it, so the
    // view keeps the typed text; only an accepted set closes it.
    if (!hooks->commitChanges(label, payload, &error))
        return fail(error.isEmpty() ? QStringLiteral("The grid source rejected the edit") : error);
    cancelEdit();
    emit committed(QUuid::createUuid().toString(QUuid::WithoutBraces), label, payload);
    return true;
}

bool EditableGridModel::fillRange(int firstRow, int firstColumn, int rowCount, int columnCount,
                                  int targetRow, int targetColumn, int targetRowCount, int targetColumnCount)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    if (firstRow < 0 || firstColumn < 0 || rowCount <= 0 || columnCount <= 0
        || qint64(firstRow) + rowCount > rows() || qint64(firstColumn) + columnCount > columns()
        || targetRow < 0 || targetColumn < 0 || targetRowCount <= 0 || targetColumnCount <= 0
        || qint64(targetRow) + targetRowCount > rows() || qint64(targetColumn) + targetColumnCount > columns()
        || qint64(targetRowCount) * targetColumnCount > MaxTransactionCells)
        return fail(QStringLiteral("Invalid fill range"));
    if (m_editing) return fail(QStringLiteral("An edit is already active"));
    if (m_sourceCommits) {
        QString error;
        if (auto *hooks = extension(); hooks
            && hooks->fillCells(QRect(firstColumn, firstRow, columnCount, rowCount),
                                QRect(targetColumn, targetRow, targetColumnCount, targetRowCount), &error))
            return true;
        if (!error.isEmpty()) return fail(error);
    }
    QVariantList matrix;
    for (int r = targetRow; r < targetRow + targetRowCount; ++r) {
        QVariantList line;
        const QString rowId = rowIdAt(firstRow + positiveModulo(qint64(r) - firstRow, rowCount));
        for (int c = targetColumn; c < targetColumn + targetColumnCount; ++c)
            line.append(editValue(rowId, columnIdAt(firstColumn + positiveModulo(qint64(c) - firstColumn, columnCount))));
        matrix.append(QVariant(line));
    }
    return paste(targetRow, targetColumn, matrix);
}

bool EditableGridModel::resizeAxis(int count, bool rowAxis)
{
    auto &axis = rowAxis ? m_rows : m_columns;
    const int old = axis.size();
    if (count == old) return true;
    cancelEdit();
    beginResetModel();
    auto &sizes = rowAxis ? m_rowSizes : m_columnSizes;
    auto &explicitSizes = rowAxis ? m_explicitRowSizes : m_explicitColumnSizes;
    const bool removedIdentity = count < old;
    if (count > old) {
        const QString prefix = rowAxis ? QStringLiteral("r:") : QStringLiteral("c:");
        quint64 &next = rowAxis ? m_nextRow : m_nextColumn;
        const quint64 first = axis.nextFreeGenerated(prefix, next, count - old);
        axis.insertGenerated(old, prefix, first, count - old);
        next = first + quint64(count - old);
        sizes.resize(count, rowAxis ? m_defaultRowSize : m_defaultColumnSize);
    } else {
        for (auto it = explicitSizes.begin(); it != explicitSizes.end();) {
            if (axis.indexOf(*it) >= count) it = explicitSizes.erase(it);
            else ++it;
        }
        axis.remove(count, old - count);
        sizes.resize(count);
    }
    rebuildPositions(rowAxis);
    m_frozenRows = std::min(m_frozenRows, rows());
    m_frozenColumns = std::min(m_frozenColumns, columns());
    reconcile(removedIdentity);
    if (removedIdentity) ++m_structureGeneration;
    ++m_revision;
    endResetModel();
    emit structureChanged(); emit geometryChanged(); emit cellsChanged();
    return true;
}

bool EditableGridModel::insertGenerated(int index, int count, bool rowAxis)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    auto &axis = rowAxis ? m_rows : m_columns;
    if (index < 0 || index > axis.size() || count <= 0)
        return fail(QStringLiteral("Invalid grid insertion index or count"));
    const QString prefix = rowAxis ? QStringLiteral("r:") : QStringLiteral("c:");
    quint64 &next = rowAxis ? m_nextRow : m_nextColumn;
    const quint64 first = axis.nextFreeGenerated(prefix, next, count);
    cancelEdit();
    if (rowAxis) beginInsertRows({}, index, index + count - 1);
    else beginInsertColumns({}, index, index + count - 1);
    axis.insertGenerated(index, prefix, first, count);
    next = first + quint64(count);
    auto &sizes = rowAxis ? m_rowSizes : m_columnSizes;
    sizes.insert(index, count, rowAxis ? m_defaultRowSize : m_defaultColumnSize);
    rebuildPositions(rowAxis);
    ++m_revision;
    if (rowAxis) endInsertRows();
    else endInsertColumns();
    emit structureChanged(); emit geometryChanged(); emit cellsChanged();
    return true;
}
bool EditableGridModel::insertGeneratedRowsAt(int index, int count) { return insertGenerated(index, count, true); }
bool EditableGridModel::insertGeneratedColumnsAt(int index, int count) { return insertGenerated(index, count, false); }

} // namespace QindaTK
