// SPDX-License-Identifier: LGPL-3.0-or-later
#include "query_grid_adapter.h"
#include <QMetaType>
#include <cmath>

namespace QindaTK {
namespace {
QueryValue typedValue(const QVariant &value, QueryType type)
{
    if (!value.isValid() || value.isNull()) return QueryValue::null();
    const int actual = value.metaType().id();
    switch (type) {
    case QueryType::Boolean:
        return actual == QMetaType::Bool ? QueryValue::boolean(value.toBool())
                                         : QueryValue::failure(QStringLiteral("Expected boolean"));
    case QueryType::Integer:
        if (actual == QMetaType::Int || actual == QMetaType::LongLong
            || actual == QMetaType::UInt || actual == QMetaType::ULongLong) {
            bool ok = false; const qint64 n = value.toLongLong(&ok);
            if (ok) return QueryValue::integer(n);
        }
        return QueryValue::failure(QStringLiteral("Expected integer"));
    case QueryType::Number:
        if (actual == QMetaType::Int || actual == QMetaType::LongLong
            || actual == QMetaType::UInt || actual == QMetaType::ULongLong
            || actual == QMetaType::Float || actual == QMetaType::Double) {
            bool ok = false; const double n = value.toDouble(&ok);
            if (ok && std::isfinite(n)) return QueryValue::number(n);
        }
        return QueryValue::failure(QStringLiteral("Expected finite number"));
    case QueryType::Text:
        return actual == QMetaType::QString ? QueryValue::text(value.toString())
                                            : QueryValue::failure(QStringLiteral("Expected text"));
    case QueryType::Error: case QueryType::Null: break;
    }
    return QueryValue::failure(QStringLiteral("Invalid field type"));
}

class ResultCellSource final : public GridCellSource {
public:
    ResultCellSource(QueryResult result, QObject *parent)
        : GridCellSource(parent), m_result(std::move(result)) {}
    QVariant value(const QString &rowId, const QString &columnId) const override
    {
        const QueryValue value = m_result.cell(rowId, columnId);
        if (value.type == QueryType::Null || value.type == QueryType::Error) return {};
        return value.data;
    }
private:
    QueryResult m_result;
};
} // namespace

GridQuerySource::GridQuerySource(EditableGridModel *grid, QuerySchema schema)
    : m_grid(grid), m_schema(std::move(schema)) {}

QuerySchema GridQuerySource::schema() const
{
    QuerySchema result = m_schema;
    if (!m_grid) { result.revision.clear(); return result; }
    for (const auto &field : result.fields)
        if (m_grid->columnIndex(field.id) < 0) { result.revision.clear(); break; }
    return result;
}

QString GridQuerySource::revision() const
{ return m_grid ? m_grid->revision() : QString{}; }

qsizetype GridQuerySource::rowCount() const
{ return m_grid ? m_grid->rows() : -1; }

QueryRow GridQuerySource::rowAt(qsizetype row, const QStringList &requestedFields) const
{
    QueryRow result;
    if (!m_grid || row < 0 || row >= m_grid->rows()) return result;
    result.id = m_grid->rowIdAt(int(row));
    QHash<QString, QueryField> fields;
    for (const auto &field : m_schema.fields) fields.insert(field.id, field);
    for (const QString &id : requestedFields) {
        if (!fields.contains(id) || m_grid->columnIndex(id) < 0) {
            result.values.insert(id, QueryValue::failure(QStringLiteral("Missing column")));
            continue;
        }
        result.values.insert(id, typedValue(m_grid->cellValue(result.id, id), fields.value(id).type));
    }
    return result;
}

std::unique_ptr<EditableGridModel> queryResultAsGrid(const QueryResult &result)
{
    if (result.status != QueryResult::Status::Complete) return {};
    auto grid = std::make_unique<EditableGridModel>();
    if (!grid->setRowIds(result.rowIds) || !grid->setColumnIds(result.columnIds)) return {};
    grid->setSource(new ResultCellSource(result, grid.get()));
    grid->setReadOnly(true);
    return grid;
}
} // namespace QindaTK
