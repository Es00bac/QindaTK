// SPDX-License-Identifier: LGPL-3.0-or-later
#include "query_core.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace QindaTK {
namespace {
const QStringList types = {"null", "boolean", "integer", "number", "text", "error"};
const QStringList kinds = {"constant", "and", "or", "not", "compare", "isNull", "isNotNull"};
const QStringList comparisonNames = {"eq", "ne", "lt", "le", "gt", "ge", "contains"};
const QStringList functionNames = {"countRows", "countValues", "sum", "average", "minimum", "maximum"};

QJsonObject predicateToJson(const QueryPredicate &p)
{
    QJsonObject out{{"kind", kinds.at(int(p.kind))}};
    if (p.kind == QueryPredicate::Kind::Constant) out["value"] = p.constant;
    if (p.kind == QueryPredicate::Kind::Compare || p.kind == QueryPredicate::Kind::IsNull
        || p.kind == QueryPredicate::Kind::IsNotNull) out["field"] = p.fieldId;
    if (p.kind == QueryPredicate::Kind::Compare) {
        out["op"] = comparisonNames.at(int(p.comparison));
        if (p.parameterId.isEmpty()) out["literal"] = queryValueToJson(p.literal);
        else out["parameter"] = p.parameterId;
    }
    if (!p.children.isEmpty()) {
        QJsonArray children;
        for (const auto &child : p.children) children.append(predicateToJson(child));
        out["children"] = children;
    }
    return out;
}

bool predicateFromJson(const QJsonObject &json, QueryPredicate *p, int depth)
{
    if (!p || depth > 24) return false;
    const int k = kinds.indexOf(json.value("kind").toString());
    if (k < 0) return false;
    p->kind = QueryPredicate::Kind(k);
    p->children.clear(); p->fieldId.clear(); p->parameterId.clear();
    if (p->kind == QueryPredicate::Kind::Constant) {
        if (!json.value("value").isBool()) return false;
        p->constant = json.value("value").toBool();
    }
    if (p->kind == QueryPredicate::Kind::Compare || p->kind == QueryPredicate::Kind::IsNull
        || p->kind == QueryPredicate::Kind::IsNotNull) {
        p->fieldId = json.value("field").toString();
        if (p->fieldId.isEmpty()) return false;
    }
    if (p->kind == QueryPredicate::Kind::Compare) {
        const int op = comparisonNames.indexOf(json.value("op").toString());
        if (op < 0) return false;
        p->comparison = QueryPredicate::Compare(op);
        const bool hasParameter = json.contains("parameter");
        if (hasParameter == json.contains("literal")) return false;
        if (hasParameter) p->parameterId = json.value("parameter").toString();
        else if (!queryValueFromJson(json.value("literal").toObject(), &p->literal)) return false;
        if (hasParameter && p->parameterId.isEmpty()) return false;
    }
    const QJsonArray children = json.value("children").toArray();
    if (children.size() > 256) return false;
    for (const QJsonValue &entry : children) {
        if (!entry.isObject()) return false;
        QueryPredicate child;
        if (!predicateFromJson(entry.toObject(), &child, depth + 1)) return false;
        p->children.append(std::move(child));
    }
    return true;
}

QString validatePredicate(const QueryPredicate &p, const QHash<QString, QueryField> &fields,
                          const QuerySpec &spec, const QueryCapabilities &caps,
                          const QueryLimits &limits, int depth, int *nodes)
{
    if (++*nodes > limits.maxPredicateNodes || depth > limits.maxPredicateDepth)
        return QStringLiteral("Predicate complexity limit exceeded");
    using Kind = QueryPredicate::Kind;
    if (p.kind == Kind::And || p.kind == Kind::Or) {
        if (p.children.isEmpty()) return QStringLiteral("Empty logical group");
    } else if (p.kind == Kind::Not) {
        if (p.children.size() != 1) return QStringLiteral("NOT requires one child");
    } else if (!p.children.isEmpty()) return QStringLiteral("Unexpected predicate children");
    if (p.kind == Kind::Compare || p.kind == Kind::IsNull || p.kind == Kind::IsNotNull) {
        if (!fields.contains(p.fieldId)) return QStringLiteral("Unknown field: %1").arg(p.fieldId);
    }
    if (p.kind == Kind::Compare) {
        if (!caps.comparisons.contains(p.comparison)) return QStringLiteral("Comparison unsupported");
        if (!p.parameterId.isEmpty() && !spec.parameters.contains(p.parameterId))
            return QStringLiteral("Missing parameter: %1").arg(p.parameterId);
        const QueryValue value = p.parameterId.isEmpty() ? p.literal : spec.parameters.value(p.parameterId);
        const QueryType fieldType = fields.value(p.fieldId).type;
        const bool numeric = fieldType == QueryType::Integer || fieldType == QueryType::Number;
        if (value.type == QueryType::Error) return QStringLiteral("Error parameter");
        if (value.type == QueryType::Number && !std::isfinite(value.data.toDouble()))
            return QStringLiteral("Non-finite parameter");
        if (value.type != QueryType::Null && value.type != fieldType
            && !(numeric && (value.type == QueryType::Integer || value.type == QueryType::Number)))
            return QStringLiteral("Parameter type mismatch: %1").arg(p.fieldId);
        if (p.comparison == QueryPredicate::Compare::Contains && fieldType != QueryType::Text)
            return QStringLiteral("Contains requires text");
    }
    for (const auto &child : p.children) {
        const QString error = validatePredicate(child, fields, spec, caps, limits, depth + 1, nodes);
        if (!error.isEmpty()) return error;
    }
    return {};
}
} // namespace

QString queryCellKey(const QString &rowId, const QString &columnId)
{ return rowId + QChar(0x1f) + columnId; }

QString queryAxisId(const QString &axis, const QVector<QueryValue> &values)
{
    QJsonArray data;
    for (const auto &value : values) data.append(queryValueToJson(value));
    const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    return axis + QLatin1Char(':') + QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject queryValueToJson(const QueryValue &value)
{
    QJsonObject out{{"type", types.at(int(value.type))}};
    switch (value.type) {
    case QueryType::Boolean: out["value"] = value.data.toBool(); break;
    case QueryType::Integer: out["value"] = QString::number(value.data.toLongLong()); break;
    case QueryType::Number: out["value"] = value.data.toDouble(); break;
    case QueryType::Text: out["value"] = value.data.toString(); break;
    case QueryType::Error: out["message"] = value.error; break;
    case QueryType::Null: break;
    }
    return out;
}

bool queryValueFromJson(const QJsonObject &json, QueryValue *value)
{
    if (!value) return false;
    const int t = types.indexOf(json.value("type").toString());
    if (t < 0) return false;
    value->type = QueryType(t); value->data.clear(); value->error.clear();
    switch (value->type) {
    case QueryType::Null: return true;
    case QueryType::Boolean:
        if (!json.value("value").isBool()) return false;
        value->data = json.value("value").toBool(); return true;
    case QueryType::Integer: {
        bool ok = false;
        const qint64 n = json.value("value").toString().toLongLong(&ok);
        if (!ok) return false;
        value->data = n; return true;
    }
    case QueryType::Number: {
        if (!json.value("value").isDouble()) return false;
        const double n = json.value("value").toDouble();
        if (!std::isfinite(n)) return false;
        value->data = n; return true;
    }
    case QueryType::Text:
        if (!json.value("value").isString()) return false;
        value->data = json.value("value").toString(); return true;
    case QueryType::Error:
        value->error = json.value("message").toString(); return !value->error.isEmpty();
    }
    return false;
}

QueryCapabilities QueryCapabilities::local()
{
    QueryCapabilities caps;
    for (int i = 0; i < comparisonNames.size(); ++i)
        caps.comparisons.insert(QueryPredicate::Compare(i));
    for (int i = 0; i < functionNames.size(); ++i)
        caps.aggregates.insert(QueryMeasure::Function(i));
    caps.grouping = caps.pivot = caps.provenance = caps.cancellation = true;
    caps.projection = caps.sorting = caps.joining = true;
    return caps;
}

QJsonObject QueryEngine::toJson(const QuerySpec &spec)
{
    QJsonObject out{{"version", 1}, {"id", spec.id}, {"schemaId", spec.schemaId},
                    {"schemaRevision", spec.schemaRevision},
                    {"sourceRevision", spec.sourceRevision},
                    {"filter", predicateToJson(spec.filter)}};
    QJsonObject params;
    for (auto it = spec.parameters.cbegin(); it != spec.parameters.cend(); ++it)
        params[it.key()] = queryValueToJson(it.value());
    out["parameters"] = params;
    QJsonArray rows, columns, measures, projection, sort;
    for (const QString &id : spec.rowFields) rows.append(id);
    for (const QString &id : spec.columnFields) columns.append(id);
    for (const auto &measure : spec.measures)
        measures.append(QJsonObject{{"id", measure.id}, {"field", measure.fieldId},
                                    {"function", functionNames.at(int(measure.function))}});
    out["rows"] = rows; out["columns"] = columns; out["measures"] = measures;
    for (const QString &field : spec.projection) projection.append(field);
    for (const auto &entry : spec.sort)
        sort.append(QJsonObject{{"field", entry.fieldId}, {"ascending", entry.ascending},
                                {"nullsLast", entry.nullsLast}});
    if (!projection.isEmpty()) out["projection"] = projection;
    if (!sort.isEmpty()) out["sort"] = sort;
    if (!spec.join.sourceId.isEmpty())
        out["join"] = QJsonObject{{"sourceId", spec.join.sourceId},
                                   {"schemaRevision", spec.join.schemaRevision},
                                   {"sourceRevision", spec.join.sourceRevision},
                                   {"leftField", spec.join.leftField},
                                   {"rightField", spec.join.rightField},
                                   {"leftOuter", spec.join.leftOuter}};
    return out;
}

bool QueryEngine::fromJson(const QJsonObject &json, QuerySpec *spec, QString *error)
{
    auto fail = [error](const QString &why) { if (error) *error = why; return false; };
    if (!spec || json.value("version").toInt() != 1
        || QJsonDocument(json).toJson(QJsonDocument::Compact).size() > 1048576)
        return fail(QStringLiteral("Unsupported or oversized query"));
    QuerySpec parsed;
    parsed.id = json.value("id").toString();
    parsed.schemaId = json.value("schemaId").toString();
    parsed.schemaRevision = json.value("schemaRevision").toString();
    parsed.sourceRevision = json.value("sourceRevision").toString();
    if (!predicateFromJson(json.value("filter").toObject(), &parsed.filter, 0))
        return fail(QStringLiteral("Invalid predicate"));
    const auto params = json.value("parameters").toObject();
    if (params.size() > 256) return fail(QStringLiteral("Too many parameters"));
    for (auto it = params.begin(); it != params.end(); ++it) {
        QueryValue value;
        if (it.key().isEmpty() || !queryValueFromJson(it.value().toObject(), &value))
            return fail(QStringLiteral("Invalid parameter"));
        parsed.parameters.insert(it.key(), value);
    }
    for (const auto &entry : json.value("rows").toArray()) {
        if (!entry.isString()) return fail(QStringLiteral("Invalid row field"));
        parsed.rowFields.append(entry.toString());
    }
    for (const auto &entry : json.value("columns").toArray()) {
        if (!entry.isString()) return fail(QStringLiteral("Invalid column field"));
        parsed.columnFields.append(entry.toString());
    }
    for (const auto &entry : json.value("measures").toArray()) {
        if (!entry.isObject()) return fail(QStringLiteral("Invalid measure"));
        const auto obj = entry.toObject();
        const int fn = functionNames.indexOf(obj.value("function").toString());
        if (fn < 0) return fail(QStringLiteral("Unknown aggregate"));
        parsed.measures.append({obj.value("id").toString(), obj.value("field").toString(),
                                QueryMeasure::Function(fn)});
    }
    if (json.contains("projection") && !json.value("projection").isArray())
        return fail(QStringLiteral("Invalid projection"));
    for (const auto &entry : json.value("projection").toArray()) {
        if (!entry.isString()) return fail(QStringLiteral("Invalid projection field"));
        parsed.projection.append(entry.toString());
    }
    if (json.contains("sort") && !json.value("sort").isArray())
        return fail(QStringLiteral("Invalid sort"));
    for (const auto &entry : json.value("sort").toArray()) {
        if (!entry.isObject()) return fail(QStringLiteral("Invalid sort entry"));
        const auto item = entry.toObject();
        if (!item.value("field").isString() || !item.value("ascending").isBool()
            || !item.value("nullsLast").isBool()) return fail(QStringLiteral("Invalid sort entry"));
        parsed.sort.append({item.value("field").toString(), item.value("ascending").toBool(),
                            item.value("nullsLast").toBool()});
    }
    if (json.contains("join")) {
        if (!json.value("join").isObject()) return fail(QStringLiteral("Invalid join"));
        const auto item = json.value("join").toObject();
        if (!item.value("sourceId").isString() || !item.value("schemaRevision").isString()
            || !item.value("sourceRevision").isString() || !item.value("leftField").isString()
            || !item.value("rightField").isString() || !item.value("leftOuter").isBool())
            return fail(QStringLiteral("Invalid join"));
        parsed.join = {item.value("sourceId").toString(), item.value("schemaRevision").toString(),
                       item.value("sourceRevision").toString(), item.value("leftField").toString(),
                       item.value("rightField").toString(), item.value("leftOuter").toBool()};
    }
    if (parsed.rowFields.size() > 16 || parsed.columnFields.size() > 16
        || parsed.measures.size() > 64 || parsed.projection.size() > 64
        || parsed.sort.size() > 4) return fail(QStringLiteral("Query shape limit exceeded"));
    *spec = std::move(parsed);
    if (error) error->clear();
    return true;
}

QString QueryEngine::validate(const QuerySchema &schema, const QuerySpec &spec,
                              const QueryCapabilities &caps, const QueryLimits &limits)
{
    if (spec.id.isEmpty()) return QStringLiteral("Missing query request ID");
    if (spec.schemaId.isEmpty() || spec.schemaId != schema.id
        || spec.schemaRevision.isEmpty() || spec.schemaRevision != schema.revision)
        return QStringLiteral("Stale or mismatched schema");
    if (spec.sourceRevision.isEmpty()) return QStringLiteral("Missing source revision");
    for (auto it = spec.parameters.cbegin(); it != spec.parameters.cend(); ++it)
        if (it.key().isEmpty() || it.value().type == QueryType::Error
            || (it.value().type == QueryType::Number
                && !std::isfinite(it.value().data.toDouble())))
            return QStringLiteral("Invalid parameter value");
    QHash<QString, QueryField> fields;
    for (const auto &field : schema.fields) {
        if (field.id.isEmpty() || fields.contains(field.id) || field.type == QueryType::Error
            || field.type == QueryType::Null) return QStringLiteral("Invalid source schema");
        fields.insert(field.id, field);
    }
    QSet<QString> axes, measureIds;
    for (const QString &id : spec.rowFields + spec.columnFields) {
        if (!fields.contains(id) || axes.contains(id)) return QStringLiteral("Invalid axis field");
        axes.insert(id);
    }
    if ((!spec.rowFields.isEmpty() || !spec.columnFields.isEmpty()) && !caps.grouping)
        return QStringLiteral("Grouping unsupported");
    if (!spec.columnFields.isEmpty() && !caps.pivot) return QStringLiteral("Pivot unsupported");
    if (!caps.provenance || !caps.cancellation) return QStringLiteral("Required backend capability missing");
    const bool detail = !spec.projection.isEmpty();
    if (detail && (!caps.projection || !spec.measures.isEmpty()
                   || !spec.rowFields.isEmpty() || !spec.columnFields.isEmpty()))
        return QStringLiteral("Detail projection cannot contain pivot measures or axes");
    if (!detail && (spec.measures.isEmpty() || spec.measures.size() > 64))
        return QStringLiteral("Invalid measure count");
    if (spec.projection.size() > 64 || spec.sort.size() > 4)
        return QStringLiteral("Projection or sort limit exceeded");
    QSet<QString> projected;
    for (const QString &id : spec.projection) {
        if (!fields.contains(id) || projected.contains(id))
            return QStringLiteral("Invalid projection field");
        projected.insert(id);
    }
    if (!spec.sort.isEmpty() && (!detail || !caps.sorting))
        return QStringLiteral("Sort requires supported detail projection");
    QSet<QString> sorted;
    for (const auto &entry : spec.sort) {
        if (!fields.contains(entry.fieldId) || sorted.contains(entry.fieldId))
            return QStringLiteral("Invalid sort field");
        sorted.insert(entry.fieldId);
    }
    if (!spec.join.sourceId.isEmpty() && !caps.joining)
        return QStringLiteral("Join unsupported");
    for (const auto &measure : spec.measures) {
        if (measure.id.isEmpty() || measure.id.contains(QChar(0x1f)) || measureIds.contains(measure.id)
            || !caps.aggregates.contains(measure.function)) return QStringLiteral("Invalid measure");
        measureIds.insert(measure.id);
        if (measure.function != QueryMeasure::Function::CountRows && !fields.contains(measure.fieldId))
            return QStringLiteral("Unknown measure field");
        if (measure.function == QueryMeasure::Function::Sum
            || measure.function == QueryMeasure::Function::Average) {
            const auto type = fields.value(measure.fieldId).type;
            if (type != QueryType::Integer && type != QueryType::Number)
                return QStringLiteral("Numeric aggregate requires numeric field");
        }
    }
    if (limits.maxScanRows < 0 || limits.maxGroups < 1 || limits.maxOutputCells < 1
        || limits.maxProvenanceIds < 0 || limits.maxJoinedRows < 1
        || limits.maxJoinValues < 1 || limits.maxPredicateDepth < 1
        || limits.maxPredicateNodes < 1) return QStringLiteral("Invalid query limits");
    int nodes = 0;
    return validatePredicate(spec.filter, fields, spec, caps, limits, 0, &nodes);
}
} // namespace QindaTK
