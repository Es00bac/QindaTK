// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "editable_grid_model.h"
#include "query_core.h"
#include <memory>

namespace QindaTK {

// GUI-thread adapter over the grid's stable IDs and sparse/lazy cell source.
// The host supplies accurate typed field metadata and its schema revision.
class GridQuerySource final : public QuerySource {
public:
    GridQuerySource(EditableGridModel *grid, QuerySchema schema);
    QuerySchema schema() const override;
    QString revision() const override;
    qsizetype rowCount() const override;
    QueryRow rowAt(qsizetype row, const QStringList &requestedFields) const override;
    QueryCapabilities capabilities() const override { return QueryCapabilities::local(); }
private:
    QPointer<EditableGridModel> m_grid;
    QuerySchema m_schema;
};

// Snapshot adapter for TableView/EditableGridModel's visible-window API.
// The pivot result remains authoritative and read-only; a host must not route
// grid edit commands into this presentation model.
std::unique_ptr<EditableGridModel> queryResultAsGrid(const QueryResult &result);

} // namespace QindaTK
