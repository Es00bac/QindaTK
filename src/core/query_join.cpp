// SPDX-License-Identifier: LGPL-3.0-or-later
#include "query_core.h"
#include <QJsonDocument>
#include <QSet>
#include <cmath>

namespace QindaTK {
namespace {
class JoinedRows final : public QuerySource {
public:
    JoinedRows(QuerySchema schema, QString revision, QVector<QueryRow> rows)
        : m_schema(std::move(schema)), m_revision(std::move(revision)), m_rows(std::move(rows)) {}
    QuerySchema schema() const override { return m_schema; }
    QString revision() const override { return m_revision; }
    qsizetype rowCount() const override { return m_rows.size(); }
    QueryRow rowAt(qsizetype i, const QStringList &requested) const override
    {
        QueryRow row{m_rows.at(i).id, {}};
        for (const auto &field : requested)
            if (m_rows.at(i).values.contains(field)) row.values.insert(field, m_rows.at(i).values.value(field));
        return row;
    }
    QueryCapabilities capabilities() const override { return QueryCapabilities::local(); }
private:
    QuerySchema m_schema;
    QString m_revision;
    QVector<QueryRow> m_rows;
};
QString valueKey(const QueryValue &value)
{ return QString::fromUtf8(QJsonDocument(queryValueToJson(value)).toJson(QJsonDocument::Compact)); }
QString encoded(const QString &text)
{ return QString::number(text.size()) + QLatin1Char(':') + text; }
QString joinedId(const QString &source, const QString &left, const QString &right, bool matched)
{
    // AGENT-GUARD: length-prefixed IDs and a tagged null side are injective;
    // a literal empty right ID cannot alias a left-join unmatched record.
    return QStringLiteral("join:") + encoded(source) + encoded(left)
           + (matched ? QStringLiteral("R") + encoded(right) : QStringLiteral("N"));
}
bool validKey(const QueryValue &value, const QueryField &field)
{
    if (value.type == QueryType::Error) return false;
    if (value.type == QueryType::Null) return field.nullable;
    if (value.type == QueryType::Number && !std::isfinite(value.data.toDouble())) return false;
    return value.type == field.type;
}
}

QueryResult QueryEngine::executeJoined(const QuerySource &left, const QuerySource &right,
                                       const QuerySpec &spec, const QueryLimits &limits,
                                       const std::atomic_bool *cancel)
{
    auto reject = [](const QString &why) { QueryResult r; r.diagnostic = why; return r; };
    auto cancelled = []() { QueryResult r; r.status = QueryResult::Status::Cancelled;
                            r.diagnostic = QStringLiteral("Query cancelled"); return r; };
    if (cancel && cancel->load(std::memory_order_relaxed)) return cancelled();
    const QuerySchema a = left.schema(), b = right.schema();
    const auto join = spec.join;
    if (join.sourceId.isEmpty() || join.sourceId != b.id || join.sourceId == a.id
        || join.schemaRevision != b.revision || join.sourceRevision != right.revision()
        || spec.schemaId != a.id || spec.schemaRevision != a.revision
        || spec.sourceRevision != left.revision())
        return reject(QStringLiteral("Stale or mismatched join source/schema"));
    const auto aCaps = left.capabilities(), bCaps = right.capabilities();
    if (!aCaps.joining || !bCaps.joining || !aCaps.cancellation || !bCaps.cancellation)
        return reject(QStringLiteral("Join capability unavailable"));
    QHash<QString, QueryField> aFields, bFields;
    for (const auto &field : a.fields) aFields.insert(field.id, field);
    for (const auto &field : b.fields) bFields.insert(field.id, field);
    if (!aFields.contains(join.leftField) || !bFields.contains(join.rightField)
        || aFields.value(join.leftField).type != bFields.value(join.rightField).type)
        return reject(QStringLiteral("Join keys must exist and have the same declared type"));
    QuerySchema merged = a;
    for (auto field : b.fields) {
        field.id = b.id + QLatin1Char('.') + field.id;
        field.nullable = field.nullable || join.leftOuter;
        if (aFields.contains(field.id)) return reject(QStringLiteral("Joined field ID collision"));
        merged.fields.append(field);
    }
    QuerySpec runSpec = spec;
    runSpec.join = {};
    const QString error = validate(merged, runSpec, QueryCapabilities::local(), limits);
    if (!error.isEmpty()) return reject(error);
    const qsizetype aCount = left.rowCount(), bCount = right.rowCount();
    if (aCount < 0 || bCount < 0 || aCount > limits.maxScanRows || bCount > limits.maxScanRows
        || aCount * a.fields.size() + bCount * b.fields.size() > limits.maxJoinValues)
        return reject(QStringLiteral("Join input limit exceeded"));
    QStringList aRequested, bRequested;
    for (const auto &field : a.fields) aRequested.append(field.id);
    for (const auto &field : b.fields) bRequested.append(field.id);
    QHash<QString, QVector<QueryRow>> index;
    QSet<QString> rightIds, leftIds;
    for (qsizetype i = 0; i < bCount; ++i) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return cancelled();
        if ((i % 128) == 0 && (right.revision() != join.sourceRevision
            || right.schema().revision != join.schemaRevision))
            return reject(QStringLiteral("Join source changed during scan"));
        const auto row = right.rowAt(i, bRequested);
        if (row.id.isEmpty() || rightIds.contains(row.id))
            return reject(QStringLiteral("Missing or duplicate joined row ID"));
        rightIds.insert(row.id);
        for (const auto &field : b.fields)
            if (!row.values.contains(field.id) || !validKey(row.values.value(field.id), field))
                return reject(QStringLiteral("Invalid joined source value"));
        const auto key = row.values.value(join.rightField);
        if (key.type != QueryType::Null) index[valueKey(key)].append(row);
    }
    QVector<QueryRow> joined;
    QHash<QString, QStringList> lineage;
    auto append = [&](const QueryRow &aRow, const QueryRow *bRow) {
        QueryRow mergedRow{joinedId(b.id, aRow.id, bRow ? bRow->id : QString(), bool(bRow)),
                           aRow.values};
        for (const auto &field : b.fields)
            mergedRow.values.insert(b.id + QLatin1Char('.') + field.id,
                                    bRow ? bRow->values.value(field.id) : QueryValue::null());
        lineage.insert(mergedRow.id, {aRow.id, bRow ? bRow->id : QString()});
        joined.append(std::move(mergedRow));
    };
    for (qsizetype i = 0; i < aCount; ++i) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return cancelled();
        if ((i % 128) == 0 && (left.revision() != spec.sourceRevision
            || left.schema().revision != spec.schemaRevision
            || right.revision() != join.sourceRevision
            || right.schema().revision != join.schemaRevision))
            return reject(QStringLiteral("Join source changed during scan"));
        const auto row = left.rowAt(i, aRequested);
        if (row.id.isEmpty() || leftIds.contains(row.id))
            return reject(QStringLiteral("Missing or duplicate source row ID"));
        leftIds.insert(row.id);
        for (const auto &field : a.fields)
            if (!row.values.contains(field.id) || !validKey(row.values.value(field.id), field))
                return reject(QStringLiteral("Invalid source value"));
        const auto key = row.values.value(join.leftField);
        const auto matches = key.type == QueryType::Null ? QVector<QueryRow>{}
                            : index.value(valueKey(key));
        for (const auto &match : matches) append(row, &match);
        if (matches.isEmpty() && join.leftOuter) append(row, nullptr);
        if (joined.size() > limits.maxJoinedRows || joined.size() > limits.maxScanRows)
            return reject(QStringLiteral("Join output limit exceeded"));
    }
    if (left.revision() != spec.sourceRevision || right.revision() != join.sourceRevision
        || left.schema().revision != spec.schemaRevision
        || right.schema().revision != join.schemaRevision)
        return reject(QStringLiteral("Join source changed during scan"));
    JoinedRows source(std::move(merged), spec.sourceRevision, std::move(joined));
    QueryResult result = execute(source, runSpec, limits, cancel);
    if (result.status == QueryResult::Status::Complete) {
        for (const auto &id : result.rowIds) {
            const auto pair = lineage.value(id);
            if (pair.size() == 2)
                result.labels.insert(id, pair.at(0) + QStringLiteral(" / ")
                                          + (pair.at(1).isEmpty() ? QStringLiteral("(unmatched)")
                                                                  : pair.at(1)));
        }
        result.joinedLineage = std::move(lineage);
    }
    return result;
}
} // namespace QindaTK
