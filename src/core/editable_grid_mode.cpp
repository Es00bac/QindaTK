// SPDX-License-Identifier: LGPL-3.0-or-later
#include "editable_grid_model.h"
#include <utility>

namespace QindaTK {
void EditableGridModel::setSource(GridCellSource *source)
{
    if (m_source == source) return;
    cancelEdit();
    m_source = source;
    m_overrides.clear(); m_sparseCount = 0;
    m_history.clear(); m_externalSteps.clear(); m_historyCursor = 0;
    ++m_revision; ++m_structureGeneration;
    emit cellsChanged(); emit historyChanged();
    if (rows() && columns()) emit dataChanged(index(0, 0), index(rows()-1, columns()-1));
}
void EditableGridModel::setExternalUndo(bool enabled)
{
    if (m_externalUndo == enabled) return;
    cancelEdit();
    m_externalUndo = enabled;
    m_history.clear(); m_externalSteps.clear(); m_historyCursor = 0;
    emit historyChanged();
}
void EditableGridModel::setReadOnly(bool enabled)
{
    if (m_readOnly == enabled) return;
    cancelEdit();
    m_readOnly = enabled;
    m_history.clear(); m_externalSteps.clear(); m_historyCursor = 0;
    emit readOnlyChanged(); emit historyChanged();
    if (rows() && columns()) emit dataChanged(index(0, 0), index(rows()-1, columns()-1));
}
void EditableGridModel::setValidator(std::function<QString(const QString &, const QString &, const QVariant &)> validator)
{ m_validator = std::move(validator); }
} // namespace QindaTK
