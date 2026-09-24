// SPDX-License-Identifier: LGPL-3.0-or-later
#include "editable_grid_model.h"

#include <QSet>
#include <algorithm>

namespace QindaTK {
namespace {
const QChar separator(0x1f);
}

// AGENT-GUARD (docs/editable-grid.md): insertion keeps history because every
// existing ID survives. Removal retires all local and external receipts so
// no old command can replay against a changed structure.
bool EditableGridModel::insertIds(int index, const QStringList &ids, bool rowAxis)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    auto &axis = rowAxis ? m_rows : m_columns;
    if (index < 0 || index > axis.size() || ids.isEmpty())
        return fail(QStringLiteral("Invalid grid insertion index or empty IDs"));
    QSet<QString> seen;
    for (const QString &id : ids) {
        if (id.isEmpty() || id.contains(separator) || seen.contains(id) || axis.contains(id))
            return fail(QStringLiteral("Inserted grid IDs must be unique, nonempty and new"));
        seen.insert(id);
    }
    cancelEdit();
    if (rowAxis) beginInsertRows({}, index, index + ids.size() - 1);
    else beginInsertColumns({}, index, index + ids.size() - 1);
    axis.insert(index, ids);
    auto &sizes = rowAxis ? m_rowSizes : m_columnSizes;
    sizes.insert(index, ids.size(), rowAxis ? m_defaultRowSize : m_defaultColumnSize);
    rebuildPositions(rowAxis);
    ++m_revision;
    if (rowAxis) endInsertRows();
    else endInsertColumns();
    emit structureChanged();
    emit geometryChanged();
    emit cellsChanged();
    return true;
}

bool EditableGridModel::removeIds(int index, int count, bool rowAxis)
{
    if (m_readOnly) return fail(QStringLiteral("Grid is read-only"));
    auto &ids = rowAxis ? m_rows : m_columns;
    if (index < 0 || count <= 0 || index > ids.size() || count > ids.size() - index)
        return fail(QStringLiteral("Invalid grid removal range"));
    cancelEdit();
    if (rowAxis) beginRemoveRows({}, index, index + count - 1);
    else beginRemoveColumns({}, index, index + count - 1);
    auto &explicitSizes = rowAxis ? m_explicitRowSizes : m_explicitColumnSizes;
    for (auto it = explicitSizes.begin(); it != explicitSizes.end();) {
        const int at = ids.indexOf(*it);
        if (at >= index && at < index + count) it = explicitSizes.erase(it);
        else ++it;
    }
    auto &sizes = rowAxis ? m_rowSizes : m_columnSizes;
    ids.remove(index, count);
    sizes.remove(index, count);
    rebuildPositions(rowAxis);
    m_frozenRows = std::min(m_frozenRows, rows());
    m_frozenColumns = std::min(m_frozenColumns, columns());
    ++m_structureGeneration;
    ++m_revision;
    if (rowAxis) endRemoveRows();
    else endRemoveColumns();
    reconcile(true);
    emit structureChanged();
    emit geometryChanged();
    emit cellsChanged();
    return true;
}

bool EditableGridModel::insertRowsAt(int index, const QStringList &ids)
{ return insertIds(index, ids, true); }
bool EditableGridModel::insertColumnsAt(int index, const QStringList &ids)
{ return insertIds(index, ids, false); }
bool EditableGridModel::removeRowsAt(int index, int count)
{ return removeIds(index, count, true); }
bool EditableGridModel::removeColumnsAt(int index, int count)
{ return removeIds(index, count, false); }

} // namespace QindaTK
