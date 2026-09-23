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
    const auto &oldIds = rowAxis ? m_rows : m_columns;
    if (index < 0 || index > oldIds.size() || ids.isEmpty())
        return fail(QStringLiteral("Invalid grid insertion index or empty IDs"));
    QSet<QString> seen;
    const auto &indexes = rowAxis ? m_rowIndexes : m_columnIndexes;
    for (const QString &id : ids) {
        if (id.isEmpty() || id.contains(separator) || seen.contains(id) || indexes.contains(id))
            return fail(QStringLiteral("Inserted grid IDs must be unique, nonempty and new"));
        seen.insert(id);
    }
    QStringList nextIds;
    nextIds.reserve(oldIds.size() + ids.size());
    for (int i = 0; i < index; ++i) nextIds.append(oldIds.at(i));
    nextIds.append(ids);
    for (int i = index; i < oldIds.size(); ++i) nextIds.append(oldIds.at(i));
    const auto &oldSizes = rowAxis ? m_rowSizes : m_columnSizes;
    QVector<qreal> nextSizes;
    nextSizes.reserve(nextIds.size());
    for (int i = 0; i < index; ++i) nextSizes.append(oldSizes.at(i));
    for (qsizetype i = 0; i < ids.size(); ++i)
        nextSizes.append(rowAxis ? m_defaultRowSize : m_defaultColumnSize);
    for (int i = index; i < oldSizes.size(); ++i) nextSizes.append(oldSizes.at(i));
    cancelEdit();
    if (rowAxis) beginInsertRows({}, index, index + ids.size() - 1);
    else beginInsertColumns({}, index, index + ids.size() - 1);
    (rowAxis ? m_rows : m_columns) = std::move(nextIds);
    (rowAxis ? m_rowSizes : m_columnSizes) = std::move(nextSizes);
    rebuildIndexes(rowAxis);
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
    const auto &ids = rowAxis ? m_rows : m_columns;
    if (index < 0 || count <= 0 || index > ids.size() || count > ids.size() - index)
        return fail(QStringLiteral("Invalid grid removal range"));
    QSet<QString> removed;
    for (int i = index; i < index + count; ++i) removed.insert(ids.at(i));
    cancelEdit();
    if (rowAxis) beginRemoveRows({}, index, index + count - 1);
    else beginRemoveColumns({}, index, index + count - 1);
    auto &mutableIds = rowAxis ? m_rows : m_columns;
    auto &sizes = rowAxis ? m_rowSizes : m_columnSizes;
    mutableIds.remove(index, count);
    sizes.remove(index, count);
    auto &explicitSizes = rowAxis ? m_explicitRowSizes : m_explicitColumnSizes;
    for (const QString &id : removed) explicitSizes.remove(id);
    rebuildIndexes(rowAxis);
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
