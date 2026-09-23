// SPDX-License-Identifier: LGPL-3.0-or-later
#include "query_core.h"
#include <QSet>
#include <cmath>

namespace QindaTK {
int queryCompareValues(const QueryValue &, const QueryValue &, bool *);
namespace {
struct AggregateState {
    qint64 count = 0;
    long double sum = 0;
    QueryValue extremum;
    bool hasExtremum = false;
    bool accept(const QueryMeasure &measure, const QueryValue &value)
    {
        using Function = QueryMeasure::Function;
        if (measure.function == Function::CountRows) { ++count; return true; }
        if (value.type == QueryType::Error) return false;
        if (value.type == QueryType::Null) return true;
        ++count;
        if (measure.function == Function::Sum || measure.function == Function::Average) {
            if (value.type != QueryType::Integer && value.type != QueryType::Number)
                return false;
            const double n = value.data.toDouble();
            if (!std::isfinite(n)) return false;
            sum += static_cast<long double>(n);
            return std::isfinite(sum);
        }
        if (measure.function == Function::Minimum || measure.function == Function::Maximum) {
            if (!hasExtremum) { extremum = value; hasExtremum = true; return true; }
            bool ok = false;
            const int order = queryCompareValues(value, extremum, &ok);
            if (!ok) return false;
            if ((measure.function == Function::Minimum && order < 0)
                || (measure.function == Function::Maximum && order > 0)) extremum = value;
        }
        return true;
    }
    QueryValue finish(const QueryMeasure &measure) const
    {
        using Function = QueryMeasure::Function;
        if (measure.function == Function::CountRows || measure.function == Function::CountValues)
            return QueryValue::integer(count);
        if (measure.function == Function::Sum)
            return count ? QueryValue::number(static_cast<double>(sum)) : QueryValue::null();
        if (measure.function == Function::Average)
            return count ? QueryValue::number(static_cast<double>(sum / count)) : QueryValue::null();
        return hasExtremum ? extremum : QueryValue::null();
    }
};

QString displayValue(const QueryValue &value)
{
    if (value.type == QueryType::Null) return QStringLiteral("(null)");
    if (value.type == QueryType::Boolean) return value.data.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return value.data.toString();
}

bool axisValues(const QueryRow &row, const QStringList &fields,
                const QHash<QString, QueryField> &schema, QVector<QueryValue> *values)
{
    values->clear();
    for (const QString &field : fields) {
        if (!row.values.contains(field)) return false;
        const QueryValue value = row.values.value(field);
        if (value.type == QueryType::Error || (value.type == QueryType::Null && !schema.value(field).nullable))
            return false;
        const QueryType expected = schema.value(field).type;
        if (value.type != QueryType::Null && value.type != expected
            && !((expected == QueryType::Integer || expected == QueryType::Number)
                 && (value.type == QueryType::Integer || value.type == QueryType::Number)))
            return false;
        values->append(value);
    }
    return true;
}

bool rowHasValidTypes(const QueryRow &row, const QStringList &requested,
                      const QHash<QString, QueryField> &schema)
{
    for (const QString &id : requested) {
        if (!row.values.contains(id) || !schema.contains(id)) return false;
        const auto value = row.values.value(id);
        const auto field = schema.value(id);
        if (value.type == QueryType::Error
            || (value.type == QueryType::Null && !field.nullable)) return false;
        if (value.type == QueryType::Null) continue;
        const bool numeric = (field.type == QueryType::Integer || field.type == QueryType::Number)
                             && (value.type == QueryType::Integer || value.type == QueryType::Number);
        if (value.type != field.type && !numeric) return false;
        if (value.type == QueryType::Number && !std::isfinite(value.data.toDouble())) return false;
    }
    return true;
}

QString axisLabel(const QVector<QueryValue> &values)
{
    if (values.isEmpty()) return QStringLiteral("All");
    QStringList parts;
    for (const auto &value : values) parts.append(displayValue(value));
    return parts.join(QStringLiteral(" / "));
}

void predicateFields(const QueryPredicate &predicate, QSet<QString> *ids)
{
    if (!predicate.fieldId.isEmpty()) ids->insert(predicate.fieldId);
    for (const auto &child : predicate.children) predicateFields(child, ids);
}
} // namespace

QueryValue QueryResult::cell(const QString &rowId, const QString &columnId) const
{ return cells.value(queryCellKey(rowId, columnId)); }
QStringList QueryResult::sourceRows(const QString &rowId, const QString &columnId) const
{ return provenance.value(queryCellKey(rowId, columnId)); }

QueryResult QueryEngine::execute(const QuerySource &source, const QuerySpec &spec,
                                 const QueryLimits &limits, const std::atomic_bool *cancel)
{
    QueryResult result;
    auto reject = [&result](const QString &reason) {
        result = QueryResult{}; result.diagnostic = reason; return result;
    };
    auto cancelled = [&result]() {
        result = QueryResult{}; result.status = QueryResult::Status::Cancelled;
        result.diagnostic = QStringLiteral("Query cancelled"); return result;
    };
    if (cancel && cancel->load(std::memory_order_relaxed)) return cancelled();
    const QuerySchema schema = source.schema();
    const QueryCapabilities caps = source.capabilities();
    const QString error = validate(schema, spec, caps, limits);
    if (!error.isEmpty()) return reject(error);
    if (source.revision() != spec.sourceRevision) return reject(QStringLiteral("Stale source revision"));
    if (caps.remoteExecution) {
        result = source.executeRemote(spec, limits, cancel);
        if (cancel && cancel->load(std::memory_order_relaxed)) return cancelled();
        const QuerySchema finalSchema = source.schema();
        if (source.revision() != spec.sourceRevision || result.sourceRevision != spec.sourceRevision
            || result.queryId != spec.id
            || finalSchema.id != spec.schemaId || finalSchema.revision != spec.schemaRevision)
            return reject(QStringLiteral("Stale remote result"));
        if (result.rowIds.size() > limits.maxGroups || result.columnIds.size() > limits.maxGroups
            || result.cells.size() > limits.maxOutputCells)
            return reject(QStringLiteral("Remote result exceeds limits"));
        qsizetype provenanceIds = 0;
        for (auto it = result.provenance.cbegin(); it != result.provenance.cend(); ++it) {
            provenanceIds += it.value().size();
            if (!result.cells.contains(it.key()) || provenanceIds > limits.maxProvenanceIds)
                return reject(QStringLiteral("Remote provenance exceeds limits"));
        }
        return result;
    }
    const qsizetype count = source.rowCount();
    if (count < 0 || count > limits.maxScanRows) return reject(QStringLiteral("Scan limit exceeded"));
    QHash<QString, QueryField> fields;
    for (const auto &field : schema.fields) fields.insert(field.id, field);
    QSet<QString> required;
    predicateFields(spec.filter, &required);
    for (const auto &id : spec.rowFields + spec.columnFields) required.insert(id);
    for (const auto &measure : spec.measures)
        if (measure.function != QueryMeasure::Function::CountRows) required.insert(measure.fieldId);
    QStringList requested = required.values();
    requested.sort();
    QSet<QString> seenSourceIds, seenRows, seenColumns;
    QHash<QString, AggregateState> states;
    QHash<QString, int> cellMeasure;
    qsizetype provenanceIds = 0;
    const QString totalRow = QStringLiteral("total:rows");
    const QString totalColumn = QStringLiteral("total:columns");
    const QString emptyRow = queryAxisId(QStringLiteral("row"), {});
    const QString emptyColumn = queryAxisId(QStringLiteral("column"), {});
    for (qsizetype i = 0; i < count; ++i) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return cancelled();
        if ((i % 256) == 0) {
            const QuerySchema currentSchema = source.schema();
            if (source.revision() != spec.sourceRevision || currentSchema.id != spec.schemaId
                || currentSchema.revision != spec.schemaRevision)
                return reject(QStringLiteral("Source or schema changed during refresh"));
        }
        const QueryRow row = source.rowAt(i, requested);
        ++result.scannedRows;
        if (row.id.isEmpty() || seenSourceIds.contains(row.id))
            return reject(QStringLiteral("Missing or duplicate source row ID"));
        seenSourceIds.insert(row.id);
        if (!rowHasValidTypes(row, requested, fields))
            return reject(QStringLiteral("Invalid source value"));
        const QueryTruth truth = evaluate(spec.filter, row, spec);
        if (truth == QueryTruth::Error) return reject(QStringLiteral("Filter source error"));
        if (truth != QueryTruth::True) continue;
        ++result.matchedRows;
        QVector<QueryValue> rowValues, columnValues;
        if (!axisValues(row, spec.rowFields, fields, &rowValues)
            || !axisValues(row, spec.columnFields, fields, &columnValues))
            return reject(QStringLiteral("Invalid dimension value"));
        const QString rowId = queryAxisId(QStringLiteral("row"), rowValues);
        const QString columnId = queryAxisId(QStringLiteral("column"), columnValues);
        const QStringList rowTargets = spec.rowFields.isEmpty()
                                           ? QStringList{rowId} : QStringList{rowId, totalRow};
        const QStringList columnTargets = spec.columnFields.isEmpty()
                                              ? QStringList{columnId} : QStringList{columnId, totalColumn};
        for (const QString &target : rowTargets) {
            if (!seenRows.contains(target)) {
                seenRows.insert(target); result.rowIds.append(target);
                result.labels.insert(target, target == totalRow ? QStringLiteral("Total") : axisLabel(rowValues));
            }
        }
        for (const QString &target : columnTargets) {
            if (!seenColumns.contains(target)) {
                seenColumns.insert(target);
                result.labels.insert(target, target == totalColumn ? QStringLiteral("Total")
                                                                     : axisLabel(columnValues));
                for (const auto &measure : spec.measures) {
                    const QString id = target + QStringLiteral(":measure:") + measure.id;
                    result.columnIds.append(id);
                    result.labels.insert(id, result.labels.value(target) + QStringLiteral(" / ") + measure.id);
                }
            }
        }
        if (seenRows.size() > limits.maxGroups || seenColumns.size() > limits.maxGroups)
            return reject(QStringLiteral("Group limit exceeded"));
        for (const QString &r : rowTargets) for (const QString &c : columnTargets)
            for (int m = 0; m < spec.measures.size(); ++m) {
                const auto &measure = spec.measures.at(m);
                const QString key = queryCellKey(r, c + QStringLiteral(":measure:") + measure.id);
                if (!states.contains(key) && states.size() >= limits.maxOutputCells)
                    return reject(QStringLiteral("Output cell limit exceeded"));
                const QueryValue value = measure.function == QueryMeasure::Function::CountRows
                                             ? QueryValue::null() : row.values.value(measure.fieldId,
                                                                                    QueryValue::failure(QStringLiteral("Missing field")));
                if (!states[key].accept(measure, value))
                    return reject(QStringLiteral("Aggregate source error: %1").arg(measure.fieldId));
                if (++provenanceIds > limits.maxProvenanceIds)
                    return reject(QStringLiteral("Provenance limit exceeded"));
                result.provenance[key].append(row.id);
                cellMeasure.insert(key, m);
            }
    }
    const QuerySchema finalSchema = source.schema();
    if (source.revision() != spec.sourceRevision || finalSchema.id != spec.schemaId
        || finalSchema.revision != spec.schemaRevision)
        return reject(QStringLiteral("Source or schema changed during refresh"));
    if (cancel && cancel->load(std::memory_order_relaxed)) return cancelled();
    // An ungrouped empty result still has one aggregate row and one measure
    // column. COUNT is zero; SUM/AVG/MIN/MAX are null.
    if (result.rowIds.isEmpty() && spec.rowFields.isEmpty()) {
        result.rowIds.append(emptyRow); result.labels.insert(emptyRow, QStringLiteral("All"));
    }
    if (seenColumns.isEmpty() && spec.columnFields.isEmpty()) {
        for (const auto &measure : spec.measures) {
            const QString id = emptyColumn + QStringLiteral(":measure:") + measure.id;
            result.columnIds.append(id); result.labels.insert(id, QStringLiteral("All / ") + measure.id);
        }
    }
    if (result.rowIds.size() * result.columnIds.size() > limits.maxOutputCells)
        return reject(QStringLiteral("Output matrix limit exceeded"));
    if (result.rowIds.removeAll(totalRow)) result.rowIds.append(totalRow);
    if (spec.columnFields.size()) {
        for (const auto &measure : spec.measures) {
            const QString id = totalColumn + QStringLiteral(":measure:") + measure.id;
            if (result.columnIds.removeAll(id)) result.columnIds.append(id);
        }
    }
    for (const QString &r : result.rowIds) for (const QString &c : result.columnIds) {
        const QString key = queryCellKey(r, c);
        const int measure = cellMeasure.value(key, result.columnIds.indexOf(c) % spec.measures.size());
        const QueryValue value = states.value(key).finish(spec.measures.at(measure));
        if (value.type == QueryType::Number && !std::isfinite(value.data.toDouble()))
            return reject(QStringLiteral("Aggregate overflow"));
        result.cells.insert(key, value);
    }
    result.status = QueryResult::Status::Complete;
    result.queryId = spec.id;
    result.sourceRevision = spec.sourceRevision;
    return result;
}
} // namespace QindaTK
