// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <atomic>
#include <utility>

namespace QindaTK {

enum class QueryType { Null, Boolean, Integer, Number, Text, Error };
struct QueryValue {
    QueryType type = QueryType::Null;
    QVariant data;
    QString error;
    static QueryValue null() { return {}; }
    static QueryValue boolean(bool v) { return {QueryType::Boolean, v, {}}; }
    static QueryValue integer(qint64 v) { return {QueryType::Integer, v, {}}; }
    static QueryValue number(double v) { return {QueryType::Number, v, {}}; }
    static QueryValue text(QString v) { return {QueryType::Text, std::move(v), {}}; }
    static QueryValue failure(QString v) { return {QueryType::Error, {}, std::move(v)}; }
};
enum class QueryTruth { False, True, Unknown, Error };
struct QueryField {
    QString id;
    QueryType type = QueryType::Text;
    bool nullable = true;
};
struct QuerySchema {
    QString id;
    QString revision;
    QVector<QueryField> fields;
};
struct QueryRow {
    QString id;
    QHash<QString, QueryValue> values;
};
struct QueryPredicate {
    enum class Kind { Constant, And, Or, Not, Compare, IsNull, IsNotNull };
    enum class Compare { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual, Contains };
    Kind kind = Kind::Constant;
    Compare comparison = Compare::Equal;
    bool constant = true;
    QString fieldId;
    QueryValue literal;
    QString parameterId;
    QVector<QueryPredicate> children;
};
struct QueryMeasure {
    enum class Function { CountRows, CountValues, Sum, Average, Minimum, Maximum };
    QString id;
    QString fieldId;
    Function function = Function::CountRows;
};
struct QuerySort {
    QString fieldId;
    bool ascending = true;
    bool nullsLast = true;
};
struct QueryJoin {
    QString sourceId;
    QString schemaRevision;
    QString sourceRevision;
    QString leftField;
    QString rightField;
    bool leftOuter = false;
};
struct QuerySpec {
    QString id;
    QString schemaId;
    QString schemaRevision;
    QString sourceRevision;
    QueryPredicate filter;
    QHash<QString, QueryValue> parameters;
    QStringList rowFields;
    QStringList columnFields;
    QVector<QueryMeasure> measures;
    QStringList projection;
    QVector<QuerySort> sort;
    QueryJoin join;
};
struct QueryCapabilities {
    QSet<QueryPredicate::Compare> comparisons;
    QSet<QueryMeasure::Function> aggregates;
    bool grouping = false;
    bool pivot = false;
    bool provenance = false;
    bool cancellation = false;
    bool remoteExecution = false;
    bool projection = false;
    bool sorting = false;
    bool joining = false;
    static QueryCapabilities local();
};
struct QueryLimits {
    qsizetype maxScanRows = 100000;
    qsizetype maxGroups = 10000;
    qsizetype maxOutputCells = 100000;
    qsizetype maxProvenanceIds = 100000;
    qsizetype maxJoinedRows = 100000;
    qsizetype maxJoinValues = 500000;
    int maxPredicateDepth = 24;
    int maxPredicateNodes = 256;
};
struct QueryResult {
    enum class Status { Complete, Rejected, Cancelled };
    Status status = Status::Rejected;
    QString diagnostic;
    QString queryId;
    QString sourceRevision;
    QStringList rowIds;
    QStringList columnIds;
    QHash<QString, QString> labels;
    QHash<QString, QString> rowParents;
    // Keyed by row ID + U+001F + column ID. Values are typed; null is distinct
    // from a source error or an empty aggregate.
    QHash<QString, QueryValue> cells;
    QHash<QString, QStringList> provenance;
    // A joined record ID maps to {left source row ID, right source row ID}.
    // A null right ID denotes the unmatched side of a left join.
    QHash<QString, QStringList> joinedLineage;
    qsizetype scannedRows = 0;
    qsizetype matchedRows = 0;
    QueryValue cell(const QString &rowId, const QString &columnId) const;
    QStringList sourceRows(const QString &rowId, const QString &columnId) const;
};

// AGENT-CONTRACT: Providers own snapshots, threading and remote execution.
// Local execution reads a fixed revision; a changed revision rejects the whole
// result. A remote provider may return the same QueryResult contract after
// negotiating capabilities and applying its own bounded/cancelled execution.
class QuerySource {
public:
    virtual ~QuerySource() = default;
    virtual QuerySchema schema() const = 0;
    virtual QString revision() const = 0;
    virtual qsizetype rowCount() const = 0;
    virtual QueryRow rowAt(qsizetype row, const QStringList &requestedFields) const = 0;
    virtual QueryCapabilities capabilities() const = 0;
    virtual QueryResult executeRemote(const QuerySpec &, const QueryLimits &,
                                      const std::atomic_bool *) const
    { return {}; }
};

class QueryEngine {
public:
    static QString validate(const QuerySchema &schema, const QuerySpec &spec,
                            const QueryCapabilities &capabilities,
                            const QueryLimits &limits = {});
    static QueryResult execute(const QuerySource &source, const QuerySpec &spec,
                               const QueryLimits &limits = {},
                               const std::atomic_bool *cancel = nullptr);
    static QueryResult executeJoined(const QuerySource &left, const QuerySource &right,
                                    const QuerySpec &spec, const QueryLimits &limits = {},
                                    const std::atomic_bool *cancel = nullptr);
    static QueryTruth evaluate(const QueryPredicate &predicate, const QueryRow &row,
                               const QuerySpec &spec);
    static QJsonObject toJson(const QuerySpec &spec);
    static bool fromJson(const QJsonObject &json, QuerySpec *spec, QString *error);
private:
    static QueryResult executeDetail(const QuerySource &source, const QuerySpec &spec,
                                     const QueryLimits &limits,
                                     const std::atomic_bool *cancel);
};

QString queryCellKey(const QString &rowId, const QString &columnId);
QString queryAxisId(const QString &axis, const QVector<QueryValue> &values);
QJsonObject queryValueToJson(const QueryValue &value);
bool queryValueFromJson(const QJsonObject &json, QueryValue *value);

} // namespace QindaTK
