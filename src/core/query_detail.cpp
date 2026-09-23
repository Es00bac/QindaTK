// SPDX-License-Identifier: LGPL-3.0-or-later
#include "query_core.h"
#include <algorithm>
#include <cmath>

namespace QindaTK {
int queryCompareValues(const QueryValue &, const QueryValue &, bool *);
namespace {
void predicateFields(const QueryPredicate &p, QSet<QString> *fields)
{
    if (!p.fieldId.isEmpty()) fields->insert(p.fieldId);
    for (const auto &child : p.children) predicateFields(child, fields);
}
bool valid(const QueryValue &value, const QueryField &field)
{
    if (value.type == QueryType::Error) return false;
    if (value.type == QueryType::Null) return field.nullable;
    if (value.type == QueryType::Number && !std::isfinite(value.data.toDouble())) return false;
    const bool numeric = (field.type == QueryType::Integer || field.type == QueryType::Number)
                         && (value.type == QueryType::Integer || value.type == QueryType::Number);
    return numeric || value.type == field.type;
}
}

QueryResult QueryEngine::executeDetail(const QuerySource &source, const QuerySpec &spec,
                                       const QueryLimits &limits, const std::atomic_bool *cancel)
{
    QueryResult result;
    auto reject = [](const QString &why) { QueryResult r; r.diagnostic = why; return r; };
    auto cancelled = []() { QueryResult r; r.status = QueryResult::Status::Cancelled;
                            r.diagnostic = QStringLiteral("Query cancelled"); return r; };
    const QuerySchema schema = source.schema();
    QHash<QString, QueryField> fields;
    for (const auto &field : schema.fields) fields.insert(field.id, field);
    QSet<QString> requestedSet;
    predicateFields(spec.filter, &requestedSet);
    for (const QString &field : spec.projection) requestedSet.insert(field);
    for (const auto &entry : spec.sort) requestedSet.insert(entry.fieldId);
    QStringList requested = requestedSet.values(); requested.sort();
    const qsizetype count = source.rowCount();
    if (count < 0 || count > limits.maxScanRows) return reject(QStringLiteral("Scan limit exceeded"));
    QVector<QueryRow> accepted;
    QSet<QString> seen;
    for (qsizetype i = 0; i < count; ++i) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return cancelled();
        if (i % 256 == 0 && (source.revision() != spec.sourceRevision
            || source.schema().revision != spec.schemaRevision))
            return reject(QStringLiteral("Source or schema changed during refresh"));
        const QueryRow row = source.rowAt(i, requested);
        ++result.scannedRows;
        if (row.id.isEmpty() || seen.contains(row.id))
            return reject(QStringLiteral("Missing or duplicate source row ID"));
        seen.insert(row.id);
        for (const QString &field : requested)
            if (!row.values.contains(field) || !valid(row.values.value(field), fields.value(field)))
                return reject(QStringLiteral("Invalid source value: %1").arg(field));
        const auto truth = evaluate(spec.filter, row, spec);
        if (truth == QueryTruth::Error) return reject(QStringLiteral("Filter source error"));
        if (truth == QueryTruth::True) accepted.append(row);
        if (accepted.size() > limits.maxGroups
            || accepted.size() * spec.projection.size() > limits.maxOutputCells
            || accepted.size() * spec.projection.size() > limits.maxProvenanceIds)
            return reject(QStringLiteral("Detail output limit exceeded"));
    }
    if (source.revision() != spec.sourceRevision || source.schema().revision != spec.schemaRevision)
        return reject(QStringLiteral("Source or schema changed during refresh"));
    std::stable_sort(accepted.begin(), accepted.end(), [&spec](const QueryRow &a, const QueryRow &b) {
        for (const auto &entry : spec.sort) {
            const auto left = a.values.value(entry.fieldId), right = b.values.value(entry.fieldId);
            if (left.type == QueryType::Null || right.type == QueryType::Null) {
                if (left.type == right.type) continue;
                return entry.nullsLast ? right.type == QueryType::Null : left.type == QueryType::Null;
            }
            bool ok = false;
            const int order = queryCompareValues(left, right, &ok);
            if (ok && order != 0) return entry.ascending ? order < 0 : order > 0;
        }
        return a.id < b.id;
    });
    for (const QString &field : spec.projection) {
        const QString id = QStringLiteral("field:") + field;
        result.columnIds.append(id); result.labels.insert(id, field);
    }
    for (const auto &row : accepted) {
        result.rowIds.append(row.id); result.labels.insert(row.id, row.id);
        for (const QString &field : spec.projection) {
            const QString key = queryCellKey(row.id, QStringLiteral("field:") + field);
            result.cells.insert(key, row.values.value(field));
            result.provenance.insert(key, {row.id});
        }
    }
    result.matchedRows = accepted.size();
    result.status = QueryResult::Status::Complete;
    result.queryId = spec.id; result.sourceRevision = spec.sourceRevision;
    return result;
}
} // namespace QindaTK
