// SPDX-License-Identifier: LGPL-3.0-or-later
// Report evaluation and layout (WP-059, docs/forms-reports.md#evaluation).
// Two QueryEngine runs over one SqliteTableSource snapshot: a detail run
// (filter, projection, group keys then sort keys) orders the rows, and a
// grouped run (row dimensions = group fields) supplies every group and grand
// total. Bands are walked from the ordered rows; totals are looked up by the
// engine's stable group IDs, so a printed total is the engine's value.
#include "local_report_spec.h"

#include "sqlite_query_source.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>
#include <QUuid>
#include <utility>

namespace QindaTK::LocalReport {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("QindaTK::LocalReport", text); }

bool sameValue(const QueryValue &a, const QueryValue &b) { return a.type == b.type && a.data == b.data; }

QString failure(const QueryResult &result, const SqliteTableSource &source)
{
    const bool badValue = result.diagnostic.startsWith(QLatin1String("Invalid source value"))
        || result.diagnostic.startsWith(QLatin1String("Aggregate source error"));
    if (badValue && !source.problem().isEmpty()) return source.problem();
    if (result.diagnostic == QLatin1String("Detail output limit exceeded"))
        return tr("Report exceeds the %1-row document bound; refine the filter").arg(MaximumDetailRows);
    return tr("Report query failed: %1").arg(result.diagnostic);
}

QuerySpec baseSpec(const SqliteTableSource &source, const Spec &spec, const QString &id)
{
    const QuerySchema schema = source.schema();
    QuerySpec query;
    query.id = id;
    query.schemaId = schema.id;
    query.schemaRevision = schema.revision;
    query.sourceRevision = source.revision();
    query.filter = spec.filter;
    query.parameters = spec.parameters;
    return query;
}

QString measureColumn(const QString &measureId)
{
    return queryAxisId(QStringLiteral("column"), {}) + QStringLiteral(":measure:") + measureId;
}

QString totalsText(const QString &lead, const QueryResult &sums, const QString &rowId,
                   const QVector<Total> &totals)
{
    const QueryValue count = sums.cell(rowId, measureColumn(QStringLiteral("countRows")));
    QStringList parts{lead, tr("Count %1").arg(count.type == QueryType::Integer ? displayValue(count)
                                                                                : QStringLiteral("0"))};
    for (const Total &total : totals)
        if (total.function != QueryMeasure::Function::CountRows)
            parts.append(total.label + QLatin1Char(' ') + displayValue(sums.cell(rowId, measureColumn(total.id))));
    return parts.join(QStringLiteral(" · "));
}

// Paragraph text the rich-text codec accepts: no paragraph breaks, no
// object anchors and no unpaired surrogates.
QString cleanText(QString text)
{
    if (text.size() > 4000) text = text.left(4000) + QStringLiteral("…");
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'\n' || c == u'\r' || c == QChar::ParagraphSeparator || c == QChar::LineSeparator
            || c == u'\t') text[i] = u' ';
        else if (c.unicode() == 0xFFFC) text[i] = QChar(QChar::ReplacementCharacter);
        else if (c.isHighSurrogate() && (i + 1 >= text.size() || !text.at(i + 1).isLowSurrogate()))
            text[i] = QChar(QChar::ReplacementCharacter);
        else if (c.isLowSurrogate() && (i == 0 || !text.at(i - 1).isHighSurrogate()))
            text[i] = QChar(QChar::ReplacementCharacter);
    }
    return text;
}

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QJsonObject paragraph(const QString &raw, bool bold, const QJsonObject &format = {})
{
    const QString text = cleanText(raw);
    const int formatIndex = bold ? 1 : 0;
    QJsonArray spans;
    if (!text.isEmpty()) spans.append(QJsonArray{text.size(), formatIndex});
    QJsonObject p{{QStringLiteral("id"), newId()}, {QStringLiteral("text"), text},
                  {QStringLiteral("spans"), spans}, {QStringLiteral("mark"), formatIndex}};
    if (!format.isEmpty()) p.insert(QStringLiteral("format"), format);
    return p;
}

QJsonObject table(const QStringList &header, const QVector<QStringList> &rows)
{
    QJsonArray weights, columnIds, rowArray;
    for (int c = 0; c < header.size(); ++c) {
        weights.append(1);
        columnIds.append(newId());
    }
    auto row = [&header](const QStringList &cells, bool bold) {
        QJsonArray cellArray;
        for (int c = 0; c < header.size(); ++c)
            cellArray.append(QJsonObject{{QStringLiteral("id"), newId()},
                                         {QStringLiteral("paragraphs"), QJsonArray{paragraph(cells.value(c), bold)}}});
        return QJsonObject{{QStringLiteral("id"), newId()}, {QStringLiteral("cells"), cellArray}};
    };
    rowArray.append(row(header, true));
    for (const QStringList &cells : rows) rowArray.append(row(cells, false));
    // AGENT-NOTE: headerRows 1 is the page header band: the rich-text
    // paginator repeats it at the top of every continuation page (WP-051).
    return QJsonObject{{QStringLiteral("id"), newId()}, {QStringLiteral("columns"), weights},
                       {QStringLiteral("columnIds"), columnIds}, {QStringLiteral("headerRows"), 1},
                       {QStringLiteral("borderPt"), 0.5}, {QStringLiteral("paddingPt"), 3},
                       {QStringLiteral("rows"), rowArray}};
}
} // namespace

Output evaluate(const SqliteTableSource &source, const Spec &spec)
{
    Output out;
    out.stage = QStringLiteral("query");
    for (const Column &column : spec.columns) out.columnLabels.append(column.label);
    QStringList groupFields;
    for (const Group &group : spec.groups) groupFields.append(group.field);

    QuerySpec detail = baseSpec(source, spec, QStringLiteral("report-detail"));
    for (const Column &column : spec.columns) detail.projection.append(column.field);
    for (const QString &field : groupFields)
        if (!detail.projection.contains(field)) detail.projection.append(field);
    for (const QString &field : groupFields) detail.sort.append({field, true, true});
    for (const QuerySort &entry : spec.sort)
        if (!groupFields.contains(entry.fieldId)) detail.sort.append(entry);
    QueryLimits detailLimits;
    detailLimits.maxGroups = MaximumDetailRows;
    detailLimits.maxOutputCells = detailLimits.maxProvenanceIds = qsizetype(MaximumDetailRows) * 64;
    const QueryResult rows = QueryEngine::execute(source, detail, detailLimits);
    if (rows.status != QueryResult::Status::Complete) {
        out.error = failure(rows, source);
        return out;
    }

    QuerySpec grouped = baseSpec(source, spec, QStringLiteral("report-totals"));
    grouped.rowFields = groupFields;
    grouped.measures.append({QStringLiteral("countRows"), {}, QueryMeasure::Function::CountRows});
    QSet<QString> measureIds{QStringLiteral("countRows")};
    auto addMeasures = [&grouped, &measureIds](const QVector<Total> &totals) {
        for (const Total &total : totals) {
            if (measureIds.contains(total.id)) continue;
            measureIds.insert(total.id);
            grouped.measures.append({total.id, total.field, total.function});
        }
    };
    for (const Group &group : spec.groups) addMeasures(group.totals);
    addMeasures(spec.totals);
    const QueryResult sums = QueryEngine::execute(source, grouped);
    if (sums.status != QueryResult::Status::Complete) {
        out.error = failure(sums, source);
        return out;
    }

    out.bands.append({Band::Kind::ReportHeader, -1, spec.title, {}, false, false});
    QStringList info{tr("Source: main.%1").arg(spec.table), tr("%1 rows").arg(rows.rowIds.size())};
    QStringList parameterIds = spec.parameters.keys();
    parameterIds.sort();
    for (const QString &id : parameterIds)
        info.append(QStringLiteral("%1 = %2").arg(id, displayValue(spec.parameters.value(id))));
    out.bands.append({Band::Kind::ReportInfo, -1, info.join(QStringLiteral(" · ")), {}, false, false});

    const int levels = spec.groups.size();
    QVector<QueryValue> previous;
    bool first = true;
    auto footer = [&](int level) {
        const QString rowId = queryAxisId(QStringLiteral("row"), previous.mid(0, level + 1));
        const Group &group = spec.groups.at(level);
        const QString lead = tr("%1 %2 total").arg(group.label, displayValue(previous.at(level)));
        out.bands.append({Band::Kind::GroupFooter, level, totalsText(lead, sums, rowId, group.totals), {}, false, true});
    };
    for (const QString &rowId : rows.rowIds) {
        QVector<QueryValue> key;
        for (const QString &field : groupFields)
            key.append(rows.cell(rowId, QStringLiteral("field:") + field));
        int changed = first ? 0 : levels;
        for (int level = 0; !first && level < levels; ++level)
            if (!sameValue(key.at(level), previous.at(level))) { changed = level; break; }
        if (!first)
            for (int level = levels - 1; level >= changed; --level) footer(level);
        for (int level = changed; level < levels; ++level) {
            const Group &group = spec.groups.at(level);
            // Only the outermost group that changes may start a page, so a
            // header never lands on the page before its first inner group.
            const bool breakPage = group.pageBreakBefore && !first && level == changed;
            out.bands.append({Band::Kind::GroupHeader, level,
                              QStringLiteral("%1: %2").arg(group.label, displayValue(key.at(level))), {},
                              breakPage, false});
            if (level == levels - 1) ++out.groupCount;
        }
        if (spec.detail) {
            QStringList cells;
            for (const Column &column : spec.columns)
                cells.append(displayValue(rows.cell(rowId, QStringLiteral("field:") + column.field)));
            out.bands.append({Band::Kind::Detail, levels - 1, {}, cells, false, false});
        }
        previous = key;
        first = false;
    }
    if (!first)
        for (int level = levels - 1; level >= 0; --level) footer(level);
    out.detailRows = rows.rowIds.size();
    if (rows.rowIds.isEmpty())
        out.bands.append({Band::Kind::Empty, -1, tr("No rows match this report."), {}, false, false});
    if (levels > 0 || !spec.totals.isEmpty()) {
        const QString grand = levels > 0 ? QStringLiteral("total:rows") : queryAxisId(QStringLiteral("row"), {});
        out.bands.append({Band::Kind::ReportFooter, -1, totalsText(tr("Report total"), sums, grand, spec.totals),
                          {}, false, true});
    }
    if (!spec.footer.isEmpty())
        out.bands.append({Band::Kind::ReportFooter, -1, spec.footer, {}, false, false});
    out.stage.clear();
    return out;
}

QJsonObject documentJson(const Output &output, const Spec &spec, const QString &documentId)
{
    QJsonArray blocks;
    QVector<QStringList> pending;
    auto flush = [&]() {
        if (pending.isEmpty()) return;
        blocks.append(QJsonObject{{QStringLiteral("table"), table(output.columnLabels, pending)}});
        pending.clear();
    };
    auto add = [&blocks](const QString &text, bool bold, const QJsonObject &format) {
        blocks.append(QJsonObject{{QStringLiteral("paragraph"), paragraph(text, bold, format)}});
    };
    for (const Band &band : output.bands) {
        if (band.kind == Band::Kind::Detail) {
            pending.append(band.cells);
            continue;
        }
        flush();
        switch (band.kind) {
        case Band::Kind::ReportHeader:
            add(band.text, false, {{QStringLiteral("heading"), 1}, {QStringLiteral("spaceAfterPt"), 4}});
            break;
        case Band::Kind::ReportInfo:
            add(band.text, false, {{QStringLiteral("spaceAfterPt"), 8}});
            break;
        case Band::Kind::GroupHeader: {
            QJsonObject format{{QStringLiteral("heading"), qMin(6, 2 + band.level)},
                               {QStringLiteral("keepWithNext"), true}, {QStringLiteral("spaceBeforePt"), 8}};
            if (band.level > 0) format.insert(QStringLiteral("indentStartPt"), 12 * band.level);
            if (band.pageBreakBefore) format.insert(QStringLiteral("pageBreakBefore"), true);
            add(band.text, false, format);
            break;
        }
        case Band::Kind::GroupFooter:
            add(band.text, true, {{QStringLiteral("spaceBeforePt"), 3}, {QStringLiteral("spaceAfterPt"), 6},
                                  {QStringLiteral("indentStartPt"), 12 * qMax(0, band.level)}});
            break;
        case Band::Kind::ReportFooter:
        case Band::Kind::Empty:
            add(band.text, band.strong, {{QStringLiteral("spaceBeforePt"), 8}});
            break;
        case Band::Kind::Detail:
            break;
        }
    }
    flush();
    // AGENT-GUARD: a qindatk.richtext body must end with a paragraph; a
    // report that ends in a detail table gets an empty closing paragraph.
    if (blocks.isEmpty() || !blocks.last().toObject().contains(QStringLiteral("paragraph")))
        add(QString(), false, {});

    double width = 595.276, height = 841.89;
    if (spec.paperId == QLatin1String("letter")) { width = 612; height = 792; }
    else if (spec.paperId == QLatin1String("legal")) { width = 612; height = 1008; }
    else if (spec.paperId == QLatin1String("a5")) { width = 419.528; height = 595.276; }
    if (spec.landscape) std::swap(width, height);
    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("qindatk.richtext")}, {QStringLiteral("version"), 1},
        {QStringLiteral("documentId"), documentId},
        {QStringLiteral("page"), QJsonObject{{QStringLiteral("widthPt"), width}, {QStringLiteral("heightPt"), height},
                                             {QStringLiteral("marginsPt"), QJsonArray{48, 42, 48, 42}}}},
        {QStringLiteral("defaults"), QJsonObject{{QStringLiteral("family"), QStringLiteral("sans-serif")},
                                                 {QStringLiteral("sizePt"), 9}}},
        {QStringLiteral("formats"), QJsonArray{QJsonObject{}, QJsonObject{{QStringLiteral("weight"), 700}}}},
        {QStringLiteral("blocks"), blocks}};
}

} // namespace QindaTK::LocalReport
