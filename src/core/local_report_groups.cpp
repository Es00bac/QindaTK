// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_report_groups.h"
#include <QDate>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace QindaTK::LocalReport {
QueryValue groupValue(const QueryValue &value, const Group &group)
{
    if (value.type == QueryType::Null || value.type == QueryType::Error || group.intervalBy == QLatin1String("value")) return value;
    if (group.intervalBy == QLatin1String("prefix")) {
        const auto points = value.data.toString().toUcs4();
        return QueryValue::text(QString::fromUcs4(points.constData(), qMin(qsizetype(group.intervalSize), points.size())));
    }
    if (group.intervalBy == QLatin1String("range")) {
        bool ok = false;
        const double number = value.data.toDouble(&ok);
        if (!ok || !std::isfinite(number)) return QueryValue::failure(QStringLiteral("Invalid numeric group value"));
        return QueryValue::number(std::floor(number / group.intervalSize) * group.intervalSize);
    }
    const QDate date = QDate::fromString(value.data.toString().left(10), Qt::ISODate);
    if (!date.isValid()) return QueryValue::failure(QStringLiteral("Invalid date group value"));
    if (group.intervalBy == QLatin1String("year")) return QueryValue::integer(date.year());
    if (group.intervalBy == QLatin1String("quarter"))
        return QueryValue::text(QStringLiteral("%1-Q%2").arg(date.year()).arg((date.month() - 1) / 3 + 1));
    if (group.intervalBy == QLatin1String("month")) return QueryValue::text(date.toString(QStringLiteral("yyyy-MM")));
    if (group.intervalBy == QLatin1String("week")) {
        int year;
        const int week = date.weekNumber(&year);
        return QueryValue::text(QStringLiteral("%1-W%2").arg(year).arg(week, 2, 10, QLatin1Char('0')));
    }
    return QueryValue::text(date.toString(Qt::ISODate));
}
namespace {
QString readSection(const QVariantMap &map, Section *section)
{
    section->newPage = map.value(QStringLiteral("newPage"), QStringLiteral("none")).toString();
    section->keepTogether = map.value(QStringLiteral("keepTogether")).toBool();
    if (!QStringList{QStringLiteral("none"), QStringLiteral("before"), QStringLiteral("after"), QStringLiteral("both")}.contains(section->newPage))
        return QStringLiteral("Invalid report section newPage value");
    return {};
}
}
GroupSource::GroupSource(const QuerySource &source, const QVector<Group> &groups)
    : m_source(source), m_groups(groups), m_schema(source.schema())
{
    QSet<QString> occupied;
    for (const QueryField &field : m_schema.fields) occupied.insert(field.id);
    for (int i = 0; i < groups.size(); ++i) {
        QString id = QStringLiteral("__report_group_%1").arg(i);
        while (occupied.contains(id)) id.prepend(QLatin1Char('_'));
        occupied.insert(id);
        m_keys.append(id);
        QueryType type = QueryType::Text;
        if (groups[i].intervalBy == QLatin1String("value")) {
            for (const QueryField &field : m_schema.fields)
                if (field.id == groups[i].field) { type = field.type; break; }
        } else if (groups[i].intervalBy == QLatin1String("range")) type = QueryType::Number;
        else if (groups[i].intervalBy == QLatin1String("year")) type = QueryType::Integer;
        m_schema.fields.append({id, type, true});
    }
}
QueryRow GroupSource::rowAt(qsizetype index, const QStringList &fields) const
{
    QStringList original;
    for (const QString &field : fields) if (!m_keys.contains(field)) original.append(field);
    for (const Group &group : m_groups) if (!original.contains(group.field)) original.append(group.field);
    QueryRow row = m_source.rowAt(index, original);
    for (int i = 0; i < m_groups.size(); ++i)
        if (fields.contains(m_keys[i])) row.values.insert(m_keys[i], groupValue(row.values.value(m_groups[i].field), m_groups[i]));
    return row;
}
QString parseGroupOptions(const QVariantMap &map, Group *group)
{
    const QVariantMap interval = map.value(QStringLiteral("interval")).toMap();
    group->intervalBy = interval.value(QStringLiteral("by"), QStringLiteral("value")).toString();
    group->intervalSize = interval.value(QStringLiteral("size"), 1).toDouble();
    if (!QStringList{QStringLiteral("value"), QStringLiteral("prefix"), QStringLiteral("year"), QStringLiteral("quarter"),
        QStringLiteral("month"), QStringLiteral("week"), QStringLiteral("day"), QStringLiteral("range")}.contains(group->intervalBy))
        return QStringLiteral("Unknown report grouping interval");
    if (!std::isfinite(group->intervalSize) || group->intervalSize <= 0
        || (group->intervalBy == QLatin1String("prefix") && (group->intervalSize > 4000 || std::floor(group->intervalSize) != group->intervalSize)))
        return QStringLiteral("Grouping interval size must be positive (prefix: 1..4000 characters)");
    group->descending = map.value(QStringLiteral("descending")).toBool();
    group->keepTogether = map.value(QStringLiteral("keepTogether"), QStringLiteral("no")).toString();
    if (!QStringList{QStringLiteral("no"), QStringLiteral("wholeGroup"), QStringLiteral("withFirstDetail")}.contains(group->keepTogether))
        return QStringLiteral("Invalid group keepTogether value");
    QString error = readSection(map.value(QStringLiteral("header")).toMap(), &group->header);
    return error.isEmpty() ? readSection(map.value(QStringLiteral("footer")).toMap(), &group->footer) : error;
}
QString parseReportOptions(const QVariantMap &map, const QuerySchema &schema, Spec *spec)
{
    for (const auto &entry : {qMakePair(QStringLiteral("reportHeader"), &spec->reportHeader),
                             qMakePair(QStringLiteral("detailSection"), &spec->detailSection),
                             qMakePair(QStringLiteral("reportFooter"), &spec->reportFooter)}) {
        const QString error = readSection(map.value(entry.first).toMap(), entry.second);
        if (!error.isEmpty()) return error;
    }
    for (const QVariant &value : map.value(QStringLiteral("runningSums")).toList()) {
        const QVariantMap run = value.toMap();
        RunningSum sum{run.value(QStringLiteral("field")).toString(), run.value(QStringLiteral("over"), QStringLiteral("all")).toString(),
                       run.value(QStringLiteral("label")).toString()};
        if (sum.over != QLatin1String("all") && sum.over != QLatin1String("group")) return QStringLiteral("Running sum scope must be group or all");
        bool numeric = sum.field.isEmpty();
        for (const QueryField &field : schema.fields)
            if (field.id == sum.field) numeric = field.type == QueryType::Number || field.type == QueryType::Integer;
        if (!numeric) return QStringLiteral("Running sum needs a numeric field");
        if (sum.label.isEmpty()) sum.label = sum.field.isEmpty() ? QStringLiteral("Running count") : QStringLiteral("Running sum of ") + sum.field;
        spec->runningSums.append(sum);
    }
    if (spec->runningSums.size() + spec->columns.size() > 64) return QStringLiteral("Report has too many detail columns");
    return {};
}
void sortGroupRows(QueryResult *rows, const QStringList &keys, const QVector<Group> &groups)
{
    std::stable_sort(rows->rowIds.begin(), rows->rowIds.end(), [&](const QString &a, const QString &b) {
        for (int i = 0; i < keys.size(); ++i) {
            const auto left = rows->cell(a, QStringLiteral("field:") + keys[i]);
            const auto right = rows->cell(b, QStringLiteral("field:") + keys[i]);
            if (left.type == QueryType::Null || right.type == QueryType::Null) {
                if (left.type != right.type) return left.type != QueryType::Null;
                continue;
            }
            int comparison = 0;
            if (left.type == QueryType::Integer) comparison = left.data.toLongLong() < right.data.toLongLong() ? -1 : left.data.toLongLong() > right.data.toLongLong() ? 1 : 0;
            else if (left.type == QueryType::Number) comparison = left.data.toDouble() < right.data.toDouble() ? -1 : left.data.toDouble() > right.data.toDouble() ? 1 : 0;
            else comparison = QString::compare(left.data.toString(), right.data.toString());
            if (comparison != 0) return groups[i].descending ? comparison > 0 : comparison < 0;
        }
        return false;
    });
}
}
