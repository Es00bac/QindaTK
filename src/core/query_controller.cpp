// SPDX-License-Identifier: LGPL-3.0-or-later
#include "query_controller.h"
#include "query_grid_adapter.h"
#include <QFutureWatcher>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

namespace QindaTK {
namespace {
class SnapshotSource final : public QuerySource {
public:
    SnapshotSource(QuerySchema schema, QString revision, const QVector<QueryRow> &rows)
        : m_schema(std::move(schema)), m_revision(std::move(revision)), m_rows(rows) {}
    QuerySchema schema() const override { return m_schema; }
    QString revision() const override { return m_revision; }
    qsizetype rowCount() const override { return m_rows.size(); }
    QueryRow rowAt(qsizetype index, const QStringList &) const override { return m_rows.at(index); }
    QueryCapabilities capabilities() const override { return QueryCapabilities::local(); }
private:
    QuerySchema m_schema;
    QString m_revision;
    const QVector<QueryRow> &m_rows;
};

void collectPredicateFields(const QueryPredicate &predicate, QSet<QString> *fields)
{
    if (!predicate.fieldId.isEmpty()) fields->insert(predicate.fieldId);
    for (const auto &child : predicate.children) collectPredicateFields(child, fields);
}
} // namespace

struct QueryController::Run {
    QuerySpec spec;
    QuerySchema schema, joinedSchema;
    QString sourceRevision, joinedRevision;
    QStringList fields, joinedFields;
    QVector<QueryRow> rows, joinedRows;
    qsizetype cursor = 0, joinedCursor = 0;
    std::atomic_bool cancelled{false};
};

QueryController::QueryController(QObject *parent) : QObject(parent) {}
QueryController::~QueryController()
{ if (m_run) m_run->cancelled.store(true, std::memory_order_relaxed); }

void QueryController::setSource(EditableGridModel *source)
{
    if (m_source == source) return;
    if (m_source) disconnect(m_source, nullptr, this, nullptr);
    invalidate({});
    m_source = source;
    if (m_source) {
        connect(m_source, &EditableGridModel::cellsChanged, this, [this]() {
            invalidate(QStringLiteral("Source changed during query"));
        });
        connect(m_source, &QObject::destroyed, this, [this]() {
            m_source = nullptr; invalidate(QStringLiteral("Query source was destroyed"));
            emit sourceChanged();
        });
    }
    emit sourceChanged();
}

void QueryController::setJoinSource(EditableGridModel *source)
{
    if (m_joinSource == source) return;
    if (m_joinSource) disconnect(m_joinSource, nullptr, this, nullptr);
    invalidate({}); m_joinSource = source;
    if (m_joinSource) {
        connect(m_joinSource, &EditableGridModel::cellsChanged, this, [this]() {
            invalidate(QStringLiteral("Join source changed during query"));
        });
        connect(m_joinSource, &QObject::destroyed, this, [this]() {
            m_joinSource = nullptr; invalidate(QStringLiteral("Join source was destroyed"));
            emit joinSourceChanged();
        });
    }
    emit joinSourceChanged();
}

void QueryController::setSchema(const QVariantMap &schema)
{
    if (m_schemaMap == schema) return;
    invalidate({}); m_schemaMap = schema; emit schemaChanged();
}

void QueryController::setJoinSchema(const QVariantMap &schema)
{
    if (m_joinSchemaMap == schema) return;
    invalidate({}); m_joinSchemaMap = schema; emit joinSchemaChanged();
}

void QueryController::setQuery(const QVariantMap &query)
{
    if (m_queryMap == query) return;
    invalidate({}); m_queryMap = query; emit queryChanged();
}

void QueryController::setMaxSnapshotValues(int count)
{
    if (count < 1 || count > 1000000 || count == m_maxSnapshotValues) return;
    invalidate({}); m_maxSnapshotValues = count; emit maxSnapshotValuesChanged();
}

void QueryController::setStatus(const QString &status, const QString &diagnostic)
{
    if (m_status == status && m_diagnostic == diagnostic) return;
    m_status = status; m_diagnostic = diagnostic; emit statusChanged();
}

void QueryController::clearResult()
{
    EditableGridModel *old = m_resultModel;
    m_resultModel = nullptr; m_result = {};
    m_rowLabels.clear(); m_columnLabels.clear(); m_rowParents.clear();
    if (old) old->deleteLater();
    emit resultChanged();
}

void QueryController::invalidate(const QString &reason)
{
    const bool wasRunning = m_status == QStringLiteral("running");
    if (m_run) m_run->cancelled.store(true, std::memory_order_relaxed);
    m_run.reset(); ++m_generation;
    setStatus(reason.isEmpty() ? QStringLiteral("idle") : QStringLiteral("rejected"), reason);
    clearResult();
    if (wasRunning && !reason.isEmpty()) emit rejected(m_requestId, reason);
}

void QueryController::cancel()
{
    if (m_status != QStringLiteral("running")) return;
    if (m_run) m_run->cancelled.store(true, std::memory_order_relaxed);
    m_run.reset(); ++m_generation;
    setStatus(QStringLiteral("cancelled"), QStringLiteral("Query cancelled"));
    clearResult();
}

bool QueryController::parseSchema(const QVariantMap &map, QuerySchema *schema, QString *error)
{
    auto fail = [error](const QString &why) { if (error) *error = why; return false; };
    if (!schema) return fail(QStringLiteral("No schema destination"));
    QuerySchema parsed;
    parsed.id = map.value(QStringLiteral("id")).toString();
    parsed.revision = map.value(QStringLiteral("revision")).toString();
    const QVariantList entries = map.value(QStringLiteral("fields")).toList();
    if (parsed.id.isEmpty() || parsed.revision.isEmpty() || entries.isEmpty()
        || entries.size() > 256) return fail(QStringLiteral("Invalid schema metadata"));
    QSet<QString> seen;
    for (const auto &entry : entries) {
        const QVariantMap item = entry.toMap();
        QueryField field;
        field.id = item.value(QStringLiteral("id")).toString();
        const QString type = item.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("boolean")) field.type = QueryType::Boolean;
        else if (type == QStringLiteral("integer")) field.type = QueryType::Integer;
        else if (type == QStringLiteral("number")) field.type = QueryType::Number;
        else if (type == QStringLiteral("text")) field.type = QueryType::Text;
        else return fail(QStringLiteral("Unknown field type: %1").arg(type));
        field.nullable = item.value(QStringLiteral("nullable"), true).toBool();
        if (field.id.isEmpty() || seen.contains(field.id))
            return fail(QStringLiteral("Duplicate or empty field ID"));
        seen.insert(field.id); parsed.fields.append(field);
    }
    *schema = std::move(parsed); if (error) error->clear(); return true;
}

QStringList QueryController::requestedFields(const QuerySpec &spec)
{
    QSet<QString> fields;
    collectPredicateFields(spec.filter, &fields);
    for (const auto &id : spec.rowFields + spec.columnFields) fields.insert(id);
    for (const auto &id : spec.projection) fields.insert(id);
    for (const auto &entry : spec.sort) fields.insert(entry.fieldId);
    for (const auto &measure : spec.measures)
        if (measure.function != QueryMeasure::Function::CountRows) fields.insert(measure.fieldId);
    QStringList result = fields.values(); result.sort(); return result;
}

bool QueryController::refresh()
{
    invalidate({});
    m_requestId = m_queryMap.value(QStringLiteral("id")).toString();
    emit statusChanged();
    auto fail = [this](const QString &reason) {
        setStatus(QStringLiteral("rejected"), reason);
        emit rejected(m_requestId, reason);
        return false;
    };
    if (!m_source) return fail(QStringLiteral("No query source"));
    QuerySchema schema; QString error;
    if (!parseSchema(m_schemaMap, &schema, &error)) return fail(error);
    QuerySpec spec;
    if (!QueryEngine::fromJson(QJsonObject::fromVariantMap(m_queryMap), &spec, &error))
        return fail(error);
    m_requestId = spec.id; emit statusChanged();
    QuerySchema joinedSchema;
    const bool joins = !spec.join.sourceId.isEmpty();
    QuerySchema effective = schema;
    if (joins) {
        if (!m_joinSource) return fail(QStringLiteral("No join source"));
        if (!parseSchema(m_joinSchemaMap, &joinedSchema, &error)) return fail(error);
        if (joinedSchema.id != spec.join.sourceId || joinedSchema.revision != spec.join.schemaRevision
            || m_joinSource->revision() != spec.join.sourceRevision)
            return fail(QStringLiteral("Stale joined source or schema"));
        for (auto field : joinedSchema.fields) {
            field.id = joinedSchema.id + QLatin1Char('.') + field.id;
            field.nullable |= spec.join.leftOuter;
            effective.fields.append(field);
        }
    }
    const QString validation = QueryEngine::validate(effective, spec, QueryCapabilities::local());
    if (!validation.isEmpty()) return fail(validation);
    if (m_source->revision() != spec.sourceRevision)
        return fail(QStringLiteral("Stale source revision"));
    GridQuerySource gridSource(m_source, schema);
    if (gridSource.schema().revision != schema.revision)
        return fail(QStringLiteral("Schema fields no longer match source"));
    QStringList fields = requestedFields(spec), joinedFields;
    if (joins) {
        GridQuerySource joinedGrid(m_joinSource, joinedSchema);
        if (joinedGrid.schema().revision != joinedSchema.revision)
            return fail(QStringLiteral("Joined schema fields no longer match source"));
        fields.clear();
        for (const auto &field : schema.fields) fields.append(field.id);
        for (const auto &field : joinedSchema.fields) joinedFields.append(field.id);
    }
    const qsizetype count = gridSource.rowCount();
    const QueryLimits limits;
    const qsizetype joinedCount = joins ? m_joinSource->rowCount() : 0;
    if (count < 0 || count > limits.maxScanRows || joinedCount < 0
        || joinedCount > limits.maxScanRows
        || count * fields.size() + joinedCount * joinedFields.size()
           > (joins ? std::min<qsizetype>(m_maxSnapshotValues, limits.maxJoinValues)
                    : m_maxSnapshotValues))
        return fail(QStringLiteral("Snapshot limit exceeded"));
    auto run = std::make_shared<Run>();
    run->spec = std::move(spec); run->schema = std::move(schema);
    run->sourceRevision = m_source->revision(); run->fields = fields;
    run->joinedSchema = joinedSchema; run->joinedRevision = joins ? m_joinSource->revision() : QString();
    run->joinedFields = joinedFields;
    run->rows.reserve(count); run->joinedRows.reserve(joinedCount);
    m_run = run;
    const quint64 generation = ++m_generation;
    setStatus(QStringLiteral("running"));
    QTimer::singleShot(0, this, [this, generation]() { collect(generation); });
    return true;
}

void QueryController::collect(quint64 generation)
{
    if (generation != m_generation || !m_run || !m_source) return;
    const auto run = m_run;
    if (run->cancelled.load(std::memory_order_relaxed)) return;
    if (m_source->revision() != run->sourceRevision) {
        invalidate(QStringLiteral("Source changed during query")); return;
    }
    if (!run->spec.join.sourceId.isEmpty()
        && (!m_joinSource || m_joinSource->revision() != run->joinedRevision)) {
        invalidate(QStringLiteral("Join source changed during query")); return;
    }
    GridQuerySource gridSource(m_source, run->schema);
    if (gridSource.schema().revision != run->schema.revision) {
        invalidate(QStringLiteral("Schema changed during query")); return;
    }
    if (run->cursor < gridSource.rowCount()) {
        const qsizetype end = std::min(run->cursor + 128, gridSource.rowCount());
        for (; run->cursor < end; ++run->cursor)
            run->rows.append(gridSource.rowAt(run->cursor, run->fields));
    } else if (!run->spec.join.sourceId.isEmpty()) {
        GridQuerySource joinedGrid(m_joinSource, run->joinedSchema);
        if (joinedGrid.schema().revision != run->joinedSchema.revision) {
            invalidate(QStringLiteral("Join schema changed during query")); return;
        }
        const qsizetype end = std::min(run->joinedCursor + 128, joinedGrid.rowCount());
        for (; run->joinedCursor < end; ++run->joinedCursor)
            run->joinedRows.append(joinedGrid.rowAt(run->joinedCursor, run->joinedFields));
    }
    if (m_source->revision() != run->sourceRevision) {
        invalidate(QStringLiteral("Source changed during query")); return;
    }
    if (run->cursor < gridSource.rowCount()
        || (!run->spec.join.sourceId.isEmpty() && m_joinSource
            && run->joinedCursor < m_joinSource->rowCount())) {
        QTimer::singleShot(0, this, [this, generation]() { collect(generation); });
        return;
    }
    auto *watcher = new QFutureWatcher<QueryResult>(this);
    connect(watcher, &QFutureWatcher<QueryResult>::finished, this,
            [this, watcher, generation]() {
        const QueryResult result = watcher->result();
        watcher->deleteLater();
        publish(generation, result);
    });
    watcher->setFuture(QtConcurrent::run([run]() {
        const SnapshotSource source(run->schema, run->sourceRevision, run->rows);
        if (run->spec.join.sourceId.isEmpty())
            return QueryEngine::execute(source, run->spec, {}, &run->cancelled);
        const SnapshotSource joined(run->joinedSchema, run->joinedRevision, run->joinedRows);
        return QueryEngine::executeJoined(source, joined, run->spec, {}, &run->cancelled);
    }));
}

void QueryController::publish(quint64 generation, const QueryResult &result)
{
    if (generation != m_generation || !m_run || !m_source) return;
    const auto run = m_run;
    if (run->cancelled.load(std::memory_order_relaxed)) return;
    if (m_source->revision() != run->sourceRevision
        || (!run->spec.join.sourceId.isEmpty()
            && (!m_joinSource || m_joinSource->revision() != run->joinedRevision))
        || result.queryId != run->spec.id
        || result.sourceRevision != run->sourceRevision) {
        invalidate(QStringLiteral("Stale query result")); return;
    }
    if (result.status != QueryResult::Status::Complete) {
        const QString reason = result.diagnostic.isEmpty()
                                   ? QStringLiteral("Query rejected") : result.diagnostic;
        invalidate(reason); return;
    }
    auto grid = queryResultAsGrid(result);
    if (!grid) { invalidate(QStringLiteral("Cannot present query result")); return; }
    grid->setParent(this);
    m_resultModel = grid.release();
    m_result = result;
    for (const auto &id : result.rowIds) m_rowLabels.insert(id, result.labels.value(id));
    for (const auto &id : result.columnIds) m_columnLabels.insert(id, result.labels.value(id));
    for (auto it = result.rowParents.cbegin(); it != result.rowParents.cend(); ++it)
        m_rowParents.insert(it.key(), it.value());
    m_run.reset();
    emit resultChanged();
    setStatus(QStringLiteral("complete"));
    emit completed(m_requestId);
}

QVariant QueryController::cellValue(const QString &rowId, const QString &columnId) const
{ return m_resultModel ? m_resultModel->cellValue(rowId, columnId) : QVariant{}; }
QStringList QueryController::sourceRows(const QString &rowId, const QString &columnId) const
{ return m_resultModel ? m_result.sourceRows(rowId, columnId) : QStringList{}; }

QVariantList QueryController::sourceReferences(const QString &rowId, const QString &columnId) const
{
    QVariantList refs;
    if (!m_resultModel) return refs;
    for (const auto &id : m_result.sourceRows(rowId, columnId)) {
        const auto pair = m_result.joinedLineage.value(id);
        if (pair.size() == 2) {
            refs.append(QVariantMap{{"sourceId", m_schemaMap.value("id")}, {"rowId", pair.at(0)}});
            if (!pair.at(1).isEmpty())
                refs.append(QVariantMap{{"sourceId", m_joinSchemaMap.value("id")},
                                        {"rowId", pair.at(1)}});
        } else refs.append(QVariantMap{{"sourceId", m_schemaMap.value("id")}, {"rowId", id}});
    }
    return refs;
}
} // namespace QindaTK
