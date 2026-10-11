// SPDX-License-Identifier: LGPL-3.0-or-later
// LocalDataDesignDocument report edits (docs/forms-reports.md#design-document).
// Each builds the next whole design and records it through commit(), which
// checks the structure first, so a refused edit changes nothing.
#include "local_data_design_document.h"

#include "local_report_spec.h"

#include <QJsonArray>
#include <QMetaType>
#include <QSet>

namespace QindaTK {
namespace {
QVariantMap mapAt(const QVariantMap &map, const QString &key) { return map.value(key).toMap(); }
}

QVariantMap LocalDataDesignDocument::editableReport(QVariantMap *next) const
{
    *next = design();
    QVariantMap report = mapAt(*next, QStringLiteral("report"));
    migrateLegacyReport(&report, mapAt(mapAt(*next, QStringLiteral("binding")), QStringLiteral("types")));
    return report;
}

bool LocalDataDesignDocument::setReportColumns(const QStringList &fields)
{
    QVariantMap next;
    QVariantMap report = editableReport(&next);
    const QVariantList current = report.value(QStringLiteral("columns")).toList();
    QVariantList columns;
    QSet<QString> seen;
    for (const QString &field : fields) {
        if (!boundFields().contains(field)) return fail(tr("Column %1 is not bound").arg(field));
        if (seen.contains(field)) return fail(tr("Column %1 is listed twice").arg(field));
        seen.insert(field);
        QString label = field;
        for (const QVariant &entry : current)
            if (entry.metaType().id() != QMetaType::QString
                && entry.toMap().value(QStringLiteral("field")).toString() == field)
                label = entry.toMap().value(QStringLiteral("label"), field).toString();
        columns.append(QVariantMap{{QStringLiteral("field"), field}, {QStringLiteral("label"), label}});
    }
    report.insert(QStringLiteral("columns"), columns);
    next.insert(QStringLiteral("report"), report);
    return commit(next, tr("Set report columns"));
}

bool LocalDataDesignDocument::setReportDetail(bool detail)
{
    QVariantMap next;
    QVariantMap report = editableReport(&next);
    report.insert(QStringLiteral("detail"), detail);
    next.insert(QStringLiteral("report"), report);
    return commit(next, detail ? tr("Show detail rows") : tr("Summarize groups"));
}

bool LocalDataDesignDocument::setReportGroups(const QVariantList &groups)
{
    if (groups.size() > LocalReport::MaximumGroups)
        return fail(tr("A report has at most %1 group levels").arg(LocalReport::MaximumGroups));
    QVariantMap next;
    QVariantMap report = editableReport(&next);
    QVariantList normalized;
    for (const QVariant &entry : groups) {
        const bool plain = entry.metaType().id() == QMetaType::QString;
        const QVariantMap group = plain ? QVariantMap{{QStringLiteral("field"), entry}} : entry.toMap();
        const QString field = group.value(QStringLiteral("field")).toString();
        if (field.isEmpty()) return fail(tr("Report groups need a field"));
        for (const QVariant &total : group.value(QStringLiteral("totals")).toList()) {
            QueryMeasure::Function function;
            if (!LocalReport::functionFromName(total.toMap().value(QStringLiteral("function")).toString(), &function))
                return fail(tr("Unknown total function in group %1").arg(field));
        }
        // Keep unknown extension fields and repeated source columns: each
        // level may use a different interval, label or pagination policy.
        QVariantMap out = group;
        normalized.append(out);
    }
    report.insert(QStringLiteral("groups"), normalized);
    next.insert(QStringLiteral("report"), report);
    return commit(next, tr("Set report groups"));
}

bool LocalDataDesignDocument::setReportTotal(const QString &groupField, const QString &field,
                                             const QString &function, bool enabled)
{
    QueryMeasure::Function parsed;
    if (!LocalReport::functionFromName(function, &parsed)) return fail(tr("Unknown total function %1").arg(function));
    const QString totalField = parsed == QueryMeasure::Function::CountRows ? QString() : field;
    if (parsed != QueryMeasure::Function::CountRows && !boundFields().contains(field))
        return fail(tr("Field %1 is not bound").arg(field));
    QVariantMap next;
    QVariantMap report = editableReport(&next);
    auto edit = [&](QVariantList totals) {
        QVariantList out;
        bool present = false;
        for (const QVariant &entry : totals) {
            const QVariantMap total = entry.toMap();
            const bool same = total.value(QStringLiteral("function")).toString() == function
                && total.value(QStringLiteral("field")).toString() == totalField;
            present = present || same;
            if (!same || enabled) out.append(entry);
        }
        if (enabled && !present)
            out.append(QVariantMap{{QStringLiteral("field"), totalField}, {QStringLiteral("function"), function}});
        return out;
    };
    if (groupField.isEmpty()) {
        report.insert(QStringLiteral("totals"), edit(report.value(QStringLiteral("totals")).toList()));
    } else {
        QVariantList groups = report.value(QStringLiteral("groups")).toList();
        const int i = indexOfField(groups, QStringLiteral("field"), groupField);
        if (i < 0) return fail(tr("The report is not grouped by %1").arg(groupField));
        QVariantMap group = groups.at(i).toMap();
        group.insert(QStringLiteral("totals"), edit(group.value(QStringLiteral("totals")).toList()));
        groups[i] = group;
        report.insert(QStringLiteral("groups"), groups);
    }
    next.insert(QStringLiteral("report"), report);
    return commit(next, enabled ? tr("Add report total") : tr("Remove report total"));
}

bool LocalDataDesignDocument::setGroupPageBreak(const QString &groupField, bool pageBreakBefore)
{
    QVariantMap next;
    QVariantMap report = editableReport(&next);
    QVariantList groups = report.value(QStringLiteral("groups")).toList();
    const int i = indexOfField(groups, QStringLiteral("field"), groupField);
    if (i < 0) return fail(tr("The report is not grouped by %1").arg(groupField));
    QVariantMap group = groups.at(i).toMap();
    group.insert(QStringLiteral("pageBreakBefore"), pageBreakBefore);
    groups[i] = group;
    report.insert(QStringLiteral("groups"), groups);
    next.insert(QStringLiteral("report"), report);
    return commit(next, pageBreakBefore ? tr("Start groups on a new page") : tr("Run groups on"));
}

bool LocalDataDesignDocument::setReportFilter(const QVariantMap &filter, const QVariantMap &parameters)
{
    QVariantMap next;
    QVariantMap report = editableReport(&next);
    QVariantMap query = mapAt(report, QStringLiteral("query"));
    if (filter.isEmpty()) {
        query.remove(QStringLiteral("filter"));
        query.remove(QStringLiteral("parameters"));
    } else {
        // The WP-029 parser is the one authority on predicate and parameter shape.
        const QJsonObject json{{QStringLiteral("version"), 1}, {QStringLiteral("id"), QStringLiteral("design")},
                               {QStringLiteral("filter"), QJsonObject::fromVariantMap(filter)},
                               {QStringLiteral("parameters"), QJsonObject::fromVariantMap(parameters)}};
        QuerySpec parsed;
        QString why;
        if (!QueryEngine::fromJson(json, &parsed, &why)) return fail(tr("Invalid report filter: %1").arg(why));
        query.insert(QStringLiteral("filter"), filter);
        query.insert(QStringLiteral("parameters"), parameters);
    }
    if (query.isEmpty()) report.remove(QStringLiteral("query"));
    else report.insert(QStringLiteral("query"), query);
    next.insert(QStringLiteral("report"), report);
    return commit(next, filter.isEmpty() ? tr("Clear report filter") : tr("Set report filter"));
}

bool LocalDataDesignDocument::setReportPage(const QString &paperId, const QString &orientation)
{
    if (!QStringList{QStringLiteral("a4"), QStringLiteral("a5"), QStringLiteral("letter"), QStringLiteral("legal")}.contains(paperId)
        || (orientation != QLatin1String("portrait") && orientation != QLatin1String("landscape")))
        return fail(tr("Unsupported report page %1 %2").arg(paperId, orientation));
    QVariantMap next;
    QVariantMap report = editableReport(&next);
    report.insert(QStringLiteral("page"), QVariantMap{{QStringLiteral("paperId"), paperId},
                                                     {QStringLiteral("orientation"), orientation}});
    next.insert(QStringLiteral("report"), report);
    return commit(next, tr("Set report page"));
}
} // namespace QindaTK
