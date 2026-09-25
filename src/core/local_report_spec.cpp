// SPDX-License-Identifier: LGPL-3.0-or-later
// Report definition parsing (WP-059, docs/forms-reports.md#report-definition).
// The filter, parameters and sort are the WP-029 query JSON, parsed by
// QueryEngine::fromJson and validated by QueryEngine::validate, so a report
// accepts exactly what FilterBar and QueryBuilder produce.
#include "local_report_spec.h"

#include "sqlite_query_source.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QMetaType>
#include <QSet>
#include <cmath>
#include <optional>

namespace QindaTK::LocalReport {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("QindaTK::LocalReport", text); }

const QStringList functionNames = {QStringLiteral("countRows"), QStringLiteral("countValues"),
                                   QStringLiteral("sum"), QStringLiteral("average"),
                                   QStringLiteral("minimum"), QStringLiteral("maximum")};

QString totalLabel(QueryMeasure::Function function, const QString &field)
{
    switch (function) {
    case QueryMeasure::Function::CountRows: return tr("Count");
    case QueryMeasure::Function::CountValues: return tr("Count of %1").arg(field);
    case QueryMeasure::Function::Sum: return tr("Sum of %1").arg(field);
    case QueryMeasure::Function::Average: return tr("Average of %1").arg(field);
    case QueryMeasure::Function::Minimum: return tr("Minimum of %1").arg(field);
    case QueryMeasure::Function::Maximum: return tr("Maximum of %1").arg(field);
    }
    return field;
}

// The type of the first field compared with `parameterId`, if any.
std::optional<QueryType> comparedType(const QueryPredicate &p, const QString &parameterId,
                                      const QHash<QString, QueryField> &fields)
{
    if (p.kind == QueryPredicate::Kind::Compare && p.parameterId == parameterId && fields.contains(p.fieldId))
        return fields.value(p.fieldId).type;
    for (const QueryPredicate &child : p.children)
        if (const auto type = comparedType(child, parameterId, fields)) return type;
    return std::nullopt;
}

bool parseTotals(const QVariantList &list, const QHash<QString, QueryField> &fields,
                 QVector<Total> *out, QString *error)
{
    QSet<QString> seen;
    for (const QVariant &entry : list) {
        const QVariantMap map = entry.toMap();
        Total total;
        if (!functionFromName(map.value(QStringLiteral("function")).toString(), &total.function)) {
            *error = tr("Unknown report total function: %1").arg(map.value(QStringLiteral("function")).toString());
            return false;
        }
        if (total.function != QueryMeasure::Function::CountRows) {
            total.field = map.value(QStringLiteral("field")).toString();
            if (!fields.contains(total.field)) {
                *error = tr("Report total field %1 is not in the live schema").arg(total.field);
                return false;
            }
            const QueryType type = fields.value(total.field).type;
            const bool numeric = type == QueryType::Integer || type == QueryType::Number;
            if (!numeric && (total.function == QueryMeasure::Function::Sum
                             || total.function == QueryMeasure::Function::Average)) {
                *error = tr("%1 needs a numeric column").arg(totalLabel(total.function, total.field));
                return false;
            }
        }
        total.id = total.field.isEmpty() ? functionName(total.function)
                                         : functionName(total.function) + QLatin1Char(':') + total.field;
        total.label = map.value(QStringLiteral("label")).toString().trimmed();
        if (total.label.isEmpty()) total.label = totalLabel(total.function, total.field);
        if (seen.contains(total.id)) continue;
        seen.insert(total.id);
        out->append(total);
    }
    return true;
}

QString numberText(double n)
{
    if (std::isfinite(n) && std::floor(n) == n && std::fabs(n) < 1e15)
        return QString::number(qint64(n));
    return QString::number(n, 'f', 2);
}
} // namespace

QString defaultPageFooter() { return tr("Page {page} of {pages}"); }

bool functionFromName(const QString &name, QueryMeasure::Function *function)
{
    const int index = functionNames.indexOf(name);
    if (index < 0) return false;
    *function = QueryMeasure::Function(index);
    return true;
}

QString functionName(QueryMeasure::Function function)
{
    return functionNames.value(int(function));
}

QString displayValue(const QueryValue &value)
{
    switch (value.type) {
    case QueryType::Null: return QStringLiteral("—");
    case QueryType::Boolean: return value.data.toBool() ? tr("Yes") : tr("No");
    case QueryType::Integer: return QString::number(value.data.toLongLong());
    case QueryType::Number: return numberText(value.data.toDouble());
    case QueryType::Text: return value.data.toString();
    case QueryType::Error: break;
    }
    return QStringLiteral("#ERR");
}

bool convert(const QVariant &value, QueryType type, QueryValue *out)
{
    if (!value.isValid() || value.isNull()) { *out = QueryValue::null(); return true; }
    const int id = value.metaType().id();
    const bool isBool = id == QMetaType::Bool;
    const bool isString = id == QMetaType::QString;
    const bool isNumber = !isBool && !isString && value.canConvert<double>();
    const QString text = value.toString().trimmed();
    switch (type) {
    case QueryType::Text:
        *out = QueryValue::text(value.toString());
        return true;
    case QueryType::Integer: {
        bool ok = false;
        qint64 n = 0;
        if (isNumber) {
            bool converted = false;
            const double d = value.toDouble(&converted);
            ok = converted && std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 9.0e18;
            n = qint64(d);
            if (id == QMetaType::LongLong || id == QMetaType::Int) n = value.toLongLong();
        } else if (isString) {
            n = text.toLongLong(&ok);
        }
        if (ok) *out = QueryValue::integer(n);
        return ok;
    }
    case QueryType::Number: {
        bool ok = false;
        const double d = isNumber ? value.toDouble(&ok) : isString ? text.toDouble(&ok) : 0.0;
        ok = ok && std::isfinite(d);
        if (ok) *out = QueryValue::number(d);
        return ok;
    }
    case QueryType::Boolean: {
        const QString lower = text.toLower();
        if (isBool) { *out = QueryValue::boolean(value.toBool()); return true; }
        if (lower == QLatin1String("true") || lower == QLatin1String("yes") || lower == QLatin1String("1")) {
            *out = QueryValue::boolean(true);
            return true;
        }
        if (lower == QLatin1String("false") || lower == QLatin1String("no") || lower == QLatin1String("0")) {
            *out = QueryValue::boolean(false);
            return true;
        }
        return false;
    }
    case QueryType::Null:
    case QueryType::Error:
        break;
    }
    return false;
}

QString parse(const QVariantMap &report, const QuerySchema &schema, const QVariantMap &parameterValues,
              Spec *result, QString *stage)
{
    *stage = QStringLiteral("definition");
    QHash<QString, QueryField> fields;
    for (const QueryField &field : schema.fields) fields.insert(field.id, field);
    Spec spec;
    spec.table = report.value(QStringLiteral("tableName")).toString();
    spec.title = report.value(QStringLiteral("headerText")).toString().trimmed();
    if (spec.title.isEmpty()) spec.title = tr("Data report");
    spec.footer = report.value(QStringLiteral("footerText")).toString().trimmed();
    // A design without the key gets page numbers; "" turns the band off.
    spec.pageHeader = report.value(QStringLiteral("pageHeaderText")).toString().trimmed();
    spec.pageFooter = report.contains(QStringLiteral("pageFooterText"))
        ? report.value(QStringLiteral("pageFooterText")).toString().trimmed() : defaultPageFooter();
    if (spec.pageHeader.size() > 500 || spec.pageFooter.size() > 500)
        return tr("Page header and footer text is limited to 500 characters");

    // Detail band: named columns, or every reportable column.
    const QVariantList columns = report.value(QStringLiteral("columns")).toList();
    if (columns.size() > 64) return tr("A report has at most 64 columns");
    QSet<QString> seen;
    for (const QVariant &entry : columns) {
        const bool plain = entry.metaType().id() == QMetaType::QString;
        const QVariantMap map = plain ? QVariantMap{{QStringLiteral("field"), entry}} : entry.toMap();
        Column column{map.value(QStringLiteral("field")).toString(), map.value(QStringLiteral("label")).toString().trimmed()};
        if (!fields.contains(column.field)) return tr("Report column %1 is not in the live schema").arg(column.field);
        if (seen.contains(column.field)) return tr("Report column %1 is listed twice").arg(column.field);
        seen.insert(column.field);
        if (column.label.isEmpty()) column.label = column.field;
        spec.columns.append(column);
    }
    if (spec.columns.isEmpty())
        for (const QueryField &field : schema.fields) spec.columns.append({field.id, field.id});
    if (spec.columns.isEmpty()) return tr("Report table has no reportable columns");

    // Groups. The WP-059 first cut stored one groupField and a SUM field and
    // printed one summary line per group; such a design still reads that way.
    QVariantList groups = report.value(QStringLiteral("groups")).toList();
    const QString legacyGroup = report.value(QStringLiteral("groupField")).toString();
    const QString legacySum = report.value(QStringLiteral("sumField")).toString();
    QVariantList legacyTotals;
    if (!legacySum.isEmpty()) legacyTotals.append(QVariantMap{{QStringLiteral("field"), legacySum}, {QStringLiteral("function"), QStringLiteral("sum")}});
    const bool legacy = !report.contains(QStringLiteral("groups")) && !legacyGroup.isEmpty();
    if (legacy) groups = {QVariantMap{{QStringLiteral("field"), legacyGroup}, {QStringLiteral("totals"), legacyTotals}}};
    else if (!legacySum.isEmpty() && legacyGroup.isEmpty() && !report.contains(QStringLiteral("groups")))
        return tr("Report total requires a group field");
    spec.detail = legacy ? false : report.value(QStringLiteral("detail"), true).toBool();
    if (groups.size() > MaximumGroups) return tr("A report has at most %1 group levels").arg(MaximumGroups);
    seen.clear();
    for (const QVariant &entry : groups) {
        const QVariantMap map = entry.toMap();
        Group group;
        group.field = map.value(QStringLiteral("field")).toString();
        if (!fields.contains(group.field)) return tr("Report group %1 is not in the live schema").arg(group.field);
        if (seen.contains(group.field)) return tr("Report groups by %1 twice").arg(group.field);
        seen.insert(group.field);
        group.label = map.value(QStringLiteral("label")).toString().trimmed();
        if (group.label.isEmpty()) group.label = group.field;
        group.pageBreakBefore = map.value(QStringLiteral("pageBreakBefore")).toBool();
        QString error;
        if (!parseTotals(map.value(QStringLiteral("totals")).toList(), fields, &group.totals, &error)) return error;
        spec.groups.append(group);
    }
    QString error;
    if (!parseTotals(legacy ? legacyTotals : report.value(QStringLiteral("totals")).toList(), fields, &spec.totals, &error))
        return error;

    // Filter, parameters and sort: the WP-029 query JSON.
    const QVariantMap query = report.value(QStringLiteral("query")).toMap();
    QJsonArray sort;
    for (const QVariant &entry : query.value(QStringLiteral("sort")).toList()) {
        const QVariantMap map = entry.toMap();
        sort.append(QJsonObject{{QStringLiteral("field"), map.value(QStringLiteral("field")).toString()},
                                {QStringLiteral("ascending"), map.value(QStringLiteral("ascending"), true).toBool()},
                                {QStringLiteral("nullsLast"), map.value(QStringLiteral("nullsLast"), true).toBool()}});
    }
    const QVariantMap filter = query.value(QStringLiteral("filter")).toMap();
    QJsonObject json{{QStringLiteral("version"), 1}, {QStringLiteral("id"), QStringLiteral("report")},
                     {QStringLiteral("schemaId"), schema.id}, {QStringLiteral("schemaRevision"), schema.revision},
                     {QStringLiteral("sourceRevision"), QStringLiteral("definition")},
                     {QStringLiteral("filter"), filter.isEmpty()
                          ? QJsonObject{{QStringLiteral("kind"), QStringLiteral("constant")}, {QStringLiteral("value"), true}}
                          : QJsonObject::fromVariantMap(filter)},
                     {QStringLiteral("parameters"), QJsonObject::fromVariantMap(query.value(QStringLiteral("parameters")).toMap())},
                     {QStringLiteral("sort"), sort}};
    QuerySpec parsed;
    QString why;
    if (!QueryEngine::fromJson(json, &parsed, &why)) return tr("Report query: %1").arg(why);
    spec.filter = parsed.filter;
    spec.parameters = parsed.parameters;
    spec.sort = parsed.sort;

    const QString legacyFilter = report.value(QStringLiteral("filterField")).toString();
    if (filter.isEmpty() && !legacyFilter.isEmpty()) {
        if (!fields.contains(legacyFilter)) return tr("Report filter %1 is not in the live schema").arg(legacyFilter);
        spec.filter = {};
        spec.filter.kind = QueryPredicate::Kind::Compare;
        spec.filter.comparison = QueryPredicate::Compare::Equal;
        spec.filter.fieldId = legacyFilter;
        spec.filter.parameterId = QStringLiteral("filterValue");
        const QVariant raw = report.value(QStringLiteral("filterValue"));
        const QueryType type = fields.value(legacyFilter).type;
        QueryValue value;
        *stage = QStringLiteral("parameters");
        if (raw.toString().isEmpty() && type != QueryType::Text) value = QueryValue::null();
        else if (!convert(raw, type, &value))
            return tr("Filter value for %1 is not a valid %2").arg(legacyFilter, SqliteTableSource::typeName(type));
        spec.parameters.insert(spec.filter.parameterId, value);
    }

    // Run-time parameter values, converted to the type they are compared with.
    *stage = QStringLiteral("parameters");
    for (auto it = parameterValues.cbegin(); it != parameterValues.cend(); ++it) {
        if (!spec.parameters.contains(it.key())) return tr("Unknown report parameter %1").arg(it.key());
        const QueryType declared = spec.parameters.value(it.key()).type;
        const QueryType type = comparedType(spec.filter, it.key(), fields)
                                   .value_or(declared == QueryType::Null ? QueryType::Text : declared);
        QueryValue value;
        if (!convert(it.value(), type, &value))
            return tr("Parameter %1 expects a %2 value").arg(it.key(), SqliteTableSource::typeName(type));
        spec.parameters.insert(it.key(), value);
    }
    QuerySpec check;
    check.id = QStringLiteral("report-check");
    check.schemaId = schema.id;
    check.schemaRevision = schema.revision;
    check.sourceRevision = QStringLiteral("definition");
    check.filter = spec.filter;
    check.parameters = spec.parameters;
    check.projection = {spec.columns.first().field};
    const QString invalid = QueryEngine::validate(schema, check, QueryCapabilities::local());
    if (!invalid.isEmpty()) {
        if (!invalid.contains(QLatin1String("arameter"))) *stage = QStringLiteral("definition");
        return tr("Report filter: %1").arg(invalid);
    }

    *stage = QStringLiteral("definition");
    QStringList sortKeys;
    for (const Group &group : std::as_const(spec.groups)) sortKeys.append(group.field);
    for (const QuerySort &entry : std::as_const(spec.sort))
        if (!sortKeys.contains(entry.fieldId)) sortKeys.append(entry.fieldId);
    if (sortKeys.size() > 4) return tr("A report sorts by at most four columns, groups included");

    const QVariantMap page = report.value(QStringLiteral("page")).toMap();
    spec.paperId = page.value(QStringLiteral("paperId"), QStringLiteral("a4")).toString();
    if (!QStringList{QStringLiteral("a4"), QStringLiteral("a5"), QStringLiteral("letter"), QStringLiteral("legal")}.contains(spec.paperId))
        return tr("Unsupported report paper %1").arg(spec.paperId);
    spec.landscape = page.value(QStringLiteral("orientation")).toString() == QLatin1String("landscape");
    stage->clear();
    *result = std::move(spec);
    return {};
}

} // namespace QindaTK::LocalReport
