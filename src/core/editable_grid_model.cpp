// SPDX-License-Identifier: LGPL-3.0-or-later
#include "editable_grid_model.h"

#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr int ValueRole = Qt::UserRole + 1;
constexpr int RowIdRole = Qt::UserRole + 2;
constexpr int ColumnIdRole = Qt::UserRole + 3;
constexpr int MaxVisibleCells = 4096;
constexpr int MaxTransactionCells = 65536;
const QChar separator(0x1f);
}

EditableGridModel::EditableGridModel(QObject *parent) : QAbstractTableModel(parent) {}
int EditableGridModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_rows.size(); }
int EditableGridModel::columnCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_columns.size(); }
QVariant EditableGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rows() || index.column() >= columns()) return {};
    if (role == Qt::DisplayRole || role == Qt::EditRole || role == ValueRole)
        return cellValue(m_rows.at(index.row()), m_columns.at(index.column()));
    if (role == RowIdRole) return m_rows.at(index.row());
    if (role == ColumnIdRole) return m_columns.at(index.column());
    return {};
}
bool EditableGridModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    if (!index.isValid() || index.row() >= rows() || index.column() >= columns()
        || (role != Qt::EditRole && role != ValueRole)) return false;
    const bool started = !m_editing;
    if (started && !beginEdit()) return false;
    if (!stageCell(m_rows.at(index.row()), m_columns.at(index.column()), value)) {
        if (started) cancelEdit();
        return false;
    }
    return started ? commitEdit() : true;
}
QVariant EditableGridModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole) return {};
    return orientation == Qt::Horizontal ? columnIdAt(section) : rowIdAt(section);
}
QHash<int, QByteArray> EditableGridModel::roleNames() const
{ return {{ValueRole, "value"}, {RowIdRole, "rowId"}, {ColumnIdRole, "columnId"}}; }
Qt::ItemFlags EditableGridModel::flags(const QModelIndex &index) const
{ return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                           | (m_readOnly ? Qt::NoItemFlags : Qt::ItemIsEditable) : Qt::NoItemFlags; }

bool EditableGridModel::fail(const QString &reason)
{ m_lastError = reason; emit rejected(reason); return false; }
QString EditableGridModel::key(const QString &row, const QString &column) const
{ return row + separator + column; }
void EditableGridModel::rebuildPositions(bool rowAxis)
{
    const auto &sizes = rowAxis ? m_rowSizes : m_columnSizes;
    auto &positions = rowAxis ? m_rowPositions : m_columnPositions;
    positions.resize(sizes.size() + 1); positions[0] = 0;
    for (int i = 0; i < sizes.size(); ++i) positions[i+1] = positions[i] + sizes[i];
}
void EditableGridModel::reconcile(bool invalidateHistory)
{
    for (auto it = m_overrides.begin(); it != m_overrides.end();) {
        const qsizetype split = it.key().indexOf(separator);
        if (split < 0 || !m_rows.contains(it.key().left(split))
            || !m_columns.contains(it.key().mid(split + 1))) it = m_overrides.erase(it);
        else ++it;
    }
    m_sparseCount = m_overrides.size();
    auto old = m_ranges;
    m_ranges.erase(std::remove_if(m_ranges.begin(), m_ranges.end(), [this](const Range &r) {
        return !m_rows.contains(r.ar) || !m_rows.contains(r.br)
            || !m_columns.contains(r.ac) || !m_columns.contains(r.bc);
    }), m_ranges.end());
    if (old.size() != m_ranges.size()) emit selectionChanged();
    if (invalidateHistory) {
        m_history.clear(); m_externalSteps.clear(); m_historyCursor = 0;
        emit historyChanged();
    }
}
bool EditableGridModel::setIds(QStringList ids, bool rowAxis)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    QSet<QString> seen;
    for (const QString &id : ids) {
        if (id.isEmpty() || id.contains(separator) || seen.contains(id))
            return fail(QStringLiteral("Grid section IDs must be unique, nonempty and cannot contain U+001F"));
        seen.insert(id);
    }
    auto &axis = rowAxis ? m_rows : m_columns;
    const QStringList oldIds = axis.toList();
    if (oldIds == ids) return true;
    cancelEdit();
    bool removedIdentity = false;
    for (const auto &oldId : oldIds)
        if (!seen.contains(oldId)) { removedIdentity = true; break; }
    const auto oldSizes = rowAxis ? m_rowSizes : m_columnSizes;
    QHash<QString, qreal> sizeById;
    sizeById.reserve(oldIds.size());
    for (int i = 0; i < oldIds.size(); ++i) sizeById.insert(oldIds[i], oldSizes[i]);
    beginResetModel();
    axis.assign(ids);
    auto &sizes = rowAxis ? m_rowSizes : m_columnSizes;
    auto &explicitSizes = rowAxis ? m_explicitRowSizes : m_explicitColumnSizes;
    for (auto it = explicitSizes.begin(); it != explicitSizes.end();) {
        if (!seen.contains(*it)) it = explicitSizes.erase(it);
        else ++it;
    }
    sizes.resize(ids.size());
    for (int i = 0; i < ids.size(); ++i)
        sizes[i] = sizeById.value(ids[i], rowAxis ? m_defaultRowSize : m_defaultColumnSize);
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
bool EditableGridModel::setRowIds(const QStringList &ids) { return setIds(ids, true); }
bool EditableGridModel::setColumnIds(const QStringList &ids) { return setIds(ids, false); }
bool EditableGridModel::setDimensions(int rowCount, int columnCount)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    if (rowCount < 0 || columnCount < 0) return fail(QStringLiteral("Negative grid dimensions"));
    return resizeAxis(rowCount, true) && resizeAxis(columnCount, false);
}
QString EditableGridModel::rowIdAt(int i) const { return m_rows.at(i); }
QString EditableGridModel::columnIdAt(int i) const { return m_columns.at(i); }
int EditableGridModel::rowIndex(const QString &id) const { return m_rows.indexOf(id); }
int EditableGridModel::columnIndex(const QString &id) const { return m_columns.indexOf(id); }
QVariant EditableGridModel::cellValue(const QString &row, const QString &column) const
{
    if (!m_rows.contains(row) || !m_columns.contains(column)) return {};
    auto it = m_overrides.constFind(key(row, column));
    return it != m_overrides.cend() ? it.value() : m_source ? m_source->value(row, column) : QVariant{};
}
QVariantList EditableGridModel::visibleCells(int firstRow, int rowSpan, int firstColumn, int columnSpan) const
{
    if (firstRow < 0 || firstColumn < 0 || rowSpan < 0 || columnSpan < 0
        || firstRow > rows() || firstColumn > columns()
        || rowSpan > rows()-firstRow || columnSpan > columns()-firstColumn
        || qint64(rowSpan)*columnSpan > MaxVisibleCells) return {};
    QVariantList result;
    result.reserve(rowSpan * columnSpan);
    const GridCellSourceExtension *hooks = extension();
    for (int r = firstRow; r < firstRow + rowSpan; ++r)
        for (int c = firstColumn; c < firstColumn + columnSpan; ++c) result.append(cellRecord(r, c, hooks));
    return result;
}

bool EditableGridModel::resizeId(const QString &id, qreal size, bool rowAxis)
{
    if (!std::isfinite(size) || size < 1 || size > 100000)
        return fail(QStringLiteral("Grid section size must be finite and between 1 and 100000"));
    const int i = rowAxis ? rowIndex(id) : columnIndex(id);
    if (i < 0) return fail(QStringLiteral("Unknown grid section ID"));
    auto &sizes = rowAxis ? m_rowSizes : m_columnSizes;
    if (sizes[i] == size) return true;
    sizes[i] = size; rebuildPositions(rowAxis);
    (rowAxis ? m_explicitRowSizes : m_explicitColumnSizes).insert(id);
    if (rowAxis) emit headerDataChanged(Qt::Vertical, i, i);
    else emit headerDataChanged(Qt::Horizontal, i, i);
    emit geometryChanged();
    if (rowAxis) emit rowResized(id, size);
    else emit columnResized(id, size);
    return true;
}
bool EditableGridModel::requestRowResize(const QString &id, qreal size)
{ return resizeId(id, size, true); }
bool EditableGridModel::requestColumnResize(const QString &id, qreal size)
{ return resizeId(id, size, false); }
bool EditableGridModel::moveId(const QString &id, const QString &beforeId, bool rowAxis)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    auto &ids = rowAxis ? m_rows : m_columns;
    auto &sizes = rowAxis ? m_rowSizes : m_columnSizes;
    const int from = rowAxis ? rowIndex(id) : columnIndex(id);
    const int before = beforeId.isEmpty() ? ids.size() : (rowAxis ? rowIndex(beforeId) : columnIndex(beforeId));
    if (from < 0 || before < 0 || (beforeId == id))
        return fail(QStringLiteral("Invalid grid section move"));
    const int to = before > from ? before - 1 : before;
    if (to == from) return true;
    cancelEdit();
    beginResetModel();
    ids.move(from, to); sizes.move(from, to);
    rebuildPositions(rowAxis);
    ++m_revision;
    endResetModel();
    emit structureChanged(); emit geometryChanged(); emit cellsChanged();
    if (rowAxis) emit rowMoved(id, to);
    else emit columnMoved(id, to);
    return true;
}
bool EditableGridModel::requestRowMove(const QString &id, const QString &beforeId)
{ return moveId(id, beforeId, true); }
bool EditableGridModel::requestColumnMove(const QString &id, const QString &beforeId)
{ return moveId(id, beforeId, false); }
void EditableGridModel::setFrozenRows(int count)
{ count = std::clamp(count, 0, rows()); if (m_frozenRows != count) { m_frozenRows = count; emit geometryChanged(); } }
void EditableGridModel::setFrozenColumns(int count)
{ count = std::clamp(count, 0, columns()); if (m_frozenColumns != count) { m_frozenColumns = count; emit geometryChanged(); } }
bool EditableGridModel::setDefaultSize(qreal size, bool rowAxis)
{
    if (!std::isfinite(size) || size < 1 || size > 100000)
        return fail(QStringLiteral("Grid default section size must be finite and between 1 and 100000"));
    auto &current = rowAxis ? m_defaultRowSize : m_defaultColumnSize;
    if (current == size) return true;
    current = size;
    const auto &ids = rowAxis ? m_rows : m_columns;
    const auto &explicitSizes = rowAxis ? m_explicitRowSizes : m_explicitColumnSizes;
    auto &sizes = rowAxis ? m_rowSizes : m_columnSizes;
    // O(sections + explicit sizes): generated IDs are never materialized.
    QVector<std::pair<int, qreal>> kept;
    kept.reserve(explicitSizes.size());
    for (const QString &id : explicitSizes)
        if (const int i = ids.indexOf(id); i >= 0) kept.append({i, sizes[i]});
    std::fill(sizes.begin(), sizes.end(), size);
    for (const auto &[i, explicitSize] : kept) sizes[i] = explicitSize;
    rebuildPositions(rowAxis); emit geometryChanged(); return true;
}
void EditableGridModel::setDefaultRowSize(qreal size) { setDefaultSize(size, true); }
void EditableGridModel::setDefaultColumnSize(qreal size) { setDefaultSize(size, false); }

int EditableGridModel::indexAt(const QVector<qreal> &positions, qreal coordinate) const
{
    if (positions.size() < 2 || coordinate < 0 || coordinate >= positions.last()) return -1;
    return int(std::upper_bound(positions.cbegin(), positions.cend(), coordinate) - positions.cbegin()) - 1;
}
QRectF EditableGridModel::cellRect(int row, int column, qreal scrollX, qreal scrollY) const
{
    if (row < 0 || row >= rows() || column < 0 || column >= columns()) return {};
    return {m_columnPositions[column] - (column < m_frozenColumns ? 0 : scrollX),
            m_rowPositions[row] - (row < m_frozenRows ? 0 : scrollY),
            m_columnSizes[column], m_rowSizes[row]};
}
QVariantMap EditableGridModel::hitTest(qreal x, qreal y, qreal scrollX, qreal scrollY) const
{
    if (!std::isfinite(x) || !std::isfinite(y)) return {};
    const qreal frozenWidth = m_columnPositions.value(m_frozenColumns);
    const qreal frozenHeight = m_rowPositions.value(m_frozenRows);
    const int c = indexAt(m_columnPositions, x + (x < frozenWidth ? 0 : scrollX));
    const int r = indexAt(m_rowPositions, y + (y < frozenHeight ? 0 : scrollY));
    if (r < 0 || c < 0 || (x >= frozenWidth && c < m_frozenColumns)
        || (y >= frozenHeight && r < m_frozenRows)) return {};
    return {{"row", r}, {"column", c}, {"rowId", m_rows.at(r)}, {"columnId", m_columns.at(c)}};
}
QVariantMap EditableGridModel::region(int firstRow, int lastRow, int firstColumn, int lastColumn,
                                      bool frozenRow, bool frozenColumn) const
{
    if (firstRow > lastRow || firstColumn > lastColumn) return {};
    return {{"firstRow", firstRow}, {"rowCount", lastRow-firstRow+1},
            {"firstColumn", firstColumn}, {"columnCount", lastColumn-firstColumn+1},
            {"frozenRow", frozenRow}, {"frozenColumn", frozenColumn}};
}
QVariantList EditableGridModel::visibleRegions(qreal viewportWidth, qreal viewportHeight,
                                                qreal scrollX, qreal scrollY) const
{
    if (!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight)
        || !std::isfinite(scrollX) || !std::isfinite(scrollY)
        || viewportWidth <= 0 || viewportHeight <= 0 || scrollX < 0 || scrollY < 0)
        return {};
    const qreal fw = m_columnPositions.value(m_frozenColumns), fh = m_rowPositions.value(m_frozenRows);
    const auto visibleEnd = [this](const QVector<qreal> &positions, qreal extent) {
        if (positions.size() < 2 || extent <= 0) return -1;
        return indexAt(positions, std::min(positions.last() - 0.001, extent - 0.001));
    };
    int vr = indexAt(m_rowPositions, fh + scrollY);
    int vc = indexAt(m_columnPositions, fw + scrollX);
    int er = visibleEnd(m_rowPositions, viewportHeight + scrollY);
    int ec = visibleEnd(m_columnPositions, viewportWidth + scrollX);
    if (viewportHeight <= fh || vr < 0) { vr = rows(); er = -1; }
    if (viewportWidth <= fw || vc < 0) { vc = columns(); ec = -1; }
    vr = std::max(vr, m_frozenRows); vc = std::max(vc, m_frozenColumns);
    QVariantList result;
    auto add = [&result, this](int a, int b, int c, int d, bool fr, bool fc) {
        auto v = region(a,b,c,d,fr,fc); if (!v.isEmpty()) result.append(v);
    };
    add(0, m_frozenRows-1, 0, m_frozenColumns-1, true, true);
    add(0, m_frozenRows-1, vc, ec, true, false);
    add(vr, er, 0, m_frozenColumns-1, false, true);
    add(vr, er, vc, ec, false, false);
    return result;
}

QVariantList EditableGridModel::ranges() const
{
    QVariantList result;
    for (const auto &r : m_ranges)
        result.append(QVariantMap{{"anchorRowId", r.ar}, {"anchorColumnId", r.ac},
                                  {"activeRowId", r.br}, {"activeColumnId", r.bc},
                                  {"firstRow", std::min(rowIndex(r.ar), rowIndex(r.br))},
                                  {"lastRow", std::max(rowIndex(r.ar), rowIndex(r.br))},
                                  {"firstColumn", std::min(columnIndex(r.ac), columnIndex(r.bc))},
                                  {"lastColumn", std::max(columnIndex(r.ac), columnIndex(r.bc))}});
    return result;
}
bool EditableGridModel::setRange(const QString &ar, const QString &ac,
                                 const QString &br, const QString &bc, bool append)
{
    if (rowIndex(ar) < 0 || rowIndex(br) < 0 || columnIndex(ac) < 0 || columnIndex(bc) < 0)
        return fail(QStringLiteral("Unknown range endpoint ID"));
    if (!append) m_ranges.clear();
    if (m_ranges.size() >= 1024) return fail(QStringLiteral("Too many discontiguous ranges"));
    m_ranges.append({ar,ac,br,bc}); emit selectionChanged(); return true;
}
void EditableGridModel::clearRanges()
{ if (!m_ranges.isEmpty()) { m_ranges.clear(); emit selectionChanged(); } }

bool EditableGridModel::validateCell(const QString &row, const QString &column, const QVariant &value)
{
    if (rowIndex(row) < 0 || columnIndex(column) < 0) return fail(QStringLiteral("Unknown cell ID"));
    QString error = m_source ? m_source->validate(row, column, value) : QString{};
    if (error.isEmpty() && m_validator) error = m_validator(row, column, value);
    return error.isEmpty() ? true : fail(error);
}
bool EditableGridModel::beginEdit(const QString &label)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    if (m_editing) return fail(QStringLiteral("An edit is already active"));
    m_pending.clear(); m_label = label; m_editing = true; emit editingChanged(); return true;
}
bool EditableGridModel::stageCell(const QString &row, const QString &column, const QVariant &candidate)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    if (!m_editing) return fail(QStringLiteral("No active grid edit"));
    if (!validateCell(row, column, candidate)) return false;
    auto it = std::find_if(m_pending.begin(), m_pending.end(), [&](const Change &v) {
        return v.row == row && v.column == column;
    });
    if (it != m_pending.end()) { it->after = candidate; return true; }
    if (m_pending.size() >= MaxTransactionCells) return fail(QStringLiteral("Grid edit exceeds 65536 cells"));
    const auto existing = m_overrides.constFind(key(row,column));
    // Source-owned cells compare against what an editor shows, so replacing a
    // computed display value with an equal literal is still a change (G-1).
    m_pending.append({row, column, m_sourceCommits ? editValue(row, column) : cellValue(row, column),
                      candidate, existing != m_overrides.cend()});
    return true;
}
bool EditableGridModel::paste(int firstRow, int firstColumn, const QVariantList &rowsData)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    if (firstRow < 0 || firstColumn < 0 || firstRow > rows() || firstColumn > columns()
        || rowsData.size() > rows()-firstRow) return fail(QStringLiteral("Paste outside grid"));
    const bool started = !m_editing;
    if (started && !beginEdit(QStringLiteral("Paste cells"))) return false;
    const auto saved = m_pending;
    int width = -1;
    for (int r = 0; r < rowsData.size(); ++r) {
        if (rowsData[r].metaType().id() != QMetaType::QVariantList) {
            m_pending = saved; if (started) cancelEdit();
            return fail(QStringLiteral("Paste rows must be value lists"));
        }
        const auto values = rowsData[r].toList();
        if (values.size() > columns()-firstColumn || (width >= 0 && width != values.size())) {
            m_pending = saved; if (started) cancelEdit();
            return fail(QStringLiteral("Paste must be rectangular and inside grid"));
        }
        width = values.size();
        for (int c = 0; c < values.size(); ++c)
            if (!stageCell(m_rows.at(firstRow+r), m_columns.at(firstColumn+c), values[c])) {
                m_pending = saved; if (started) cancelEdit(); return false;
            }
    }
    return started ? commitEdit() : true;
}
bool EditableGridModel::fill(int firstRow, int firstColumn, int rowSpan, int columnSpan,
                             int targetRow, int targetColumn)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    if (m_sourceCommits)
        return fillRange(firstRow, firstColumn, rowSpan, columnSpan, targetRow, targetColumn, rowSpan, columnSpan);
    if (firstRow < 0 || firstColumn < 0 || targetRow < 0 || targetColumn < 0
        || rowSpan <= 0 || columnSpan <= 0 || targetRow + rowSpan > rows()
        || targetColumn + columnSpan > columns() || firstRow + rowSpan > rows()
        || firstColumn + columnSpan > columns() || qint64(rowSpan)*columnSpan > MaxTransactionCells)
        return fail(QStringLiteral("Invalid fill range"));
    QVariantList snapshot;
    for (int r = 0; r < rowSpan; ++r) {
        QVariantList line;
        for (int c = 0; c < columnSpan; ++c)
            line.append(editValue(m_rows.at(firstRow+r), m_columns.at(firstColumn+c)));
        snapshot.append(QVariant(line));
    }
    return paste(targetRow, targetColumn, snapshot);
}
void EditableGridModel::cancelEdit()
{ if (m_editing) { m_pending.clear(); m_editing = false; emit editingChanged(); } }
QVariantList EditableGridModel::changePayload(const QVector<Change> &changes) const
{
    QVariantList result;
    for (const auto &v : changes)
        result.append(QVariantMap{{"rowId", v.row}, {"columnId", v.column},
                                  {"before", v.before}, {"after", v.after},
                                  {"hadOverride", v.hadOverride},
                                  {"structureGeneration", QString::number(m_structureGeneration)}});
    return result;
}
bool EditableGridModel::applyChanges(const QVector<Change> &changes, bool reverse)
{
    for (const auto &v : changes) {
        if (rowIndex(v.row) < 0 || columnIndex(v.column) < 0)
            return fail(QStringLiteral("Grid history references removed IDs"));
        if (cellValue(v.row, v.column) != (reverse ? v.after : v.before))
            return fail(QStringLiteral("Grid history conflicts with current cell value"));
        if (!validateCell(v.row, v.column, reverse ? v.before : v.after)) return false;
    }
    int minR = rows(), minC = columns(), maxR = -1, maxC = -1;
    for (const auto &v : changes) {
        const QString address = key(v.row, v.column);
        if (reverse && !v.hadOverride) m_overrides.remove(address);
        else m_overrides.insert(address, reverse ? v.before : v.after);
        const int r = rowIndex(v.row), c = columnIndex(v.column);
        minR = std::min(minR, r); maxR = std::max(maxR, r);
        minC = std::min(minC, c); maxC = std::max(maxC, c);
    }
    m_sparseCount = m_overrides.size();
    if (maxR >= 0) { ++m_revision; emit dataChanged(index(minR,minC), index(maxR,maxC)); emit cellsChanged(); }
    return true;
}
bool EditableGridModel::commitEdit()
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    if (!m_editing) return fail(QStringLiteral("No active grid edit"));
    QVector<Change> changes;
    for (const auto &v : m_pending) if (v.before != v.after) changes.append(v);
    if (m_sourceCommits) return commitToSource(changes, m_label);
    if (!applyChanges(changes, false)) return false;
    const QString label = m_label;
    cancelEdit();
    if (changes.isEmpty()) return true;
    Step step{QUuid::createUuid().toString(QUuid::WithoutBraces), label, changes};
    if (m_externalUndo) m_externalSteps.insert(step.id, step);
    else {
        m_history.resize(m_historyCursor);
        m_history.append(step); m_historyCursor = m_history.size();
    }
    emit historyChanged(); emit committed(step.id, label, changePayload(changes));
    return true;
}
bool EditableGridModel::undo()
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    if (m_externalUndo || m_sourceCommits) return fail(QStringLiteral("Host owns grid undo"));
    if (m_editing || !canUndo()) return fail(QStringLiteral("Grid undo unavailable"));
    if (!applyChanges(m_history[m_historyCursor-1].changes, true)) return false;
    --m_historyCursor; emit historyChanged(); return true;
}
bool EditableGridModel::redo()
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    if (m_externalUndo || m_sourceCommits) return fail(QStringLiteral("Host owns grid redo"));
    if (m_editing || !canRedo()) return fail(QStringLiteral("Grid redo unavailable"));
    if (!applyChanges(m_history[m_historyCursor].changes, false)) return false;
    ++m_historyCursor; emit historyChanged(); return true;
}
bool EditableGridModel::applyHistoryChanges(const QString &transactionId,
                                            const QVariantList &changes, bool reverse,
                                            const QString &expectedRevision)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    if (!m_externalUndo || m_editing) return fail(QStringLiteral("Host grid history is unavailable"));
    auto it = m_externalSteps.constFind(transactionId);
    if (it == m_externalSteps.cend()) return fail(QStringLiteral("Unknown or retired grid transaction ID"));
    if (expectedRevision != revision()) return fail(QStringLiteral("Stale grid revision"));
    if (changes != changePayload(it->changes)) return fail(QStringLiteral("Grid history payload mismatch"));
    return applyChanges(it->changes, reverse);
}
bool EditableGridModel::releaseHistoryTransaction(const QString &transactionId)
{ return m_externalSteps.remove(transactionId) > 0; }

} // namespace QindaTK
