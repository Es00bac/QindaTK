// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "query_core.h"

#include <QJsonObject>
#include <QVariantMap>

namespace QindaTK {
class SqliteTableSource;

// Report definition, evaluation and document layout (WP-059,
// docs/forms-reports.md#report-definition). A saved report map is parsed
// against the live QuerySchema into a ReportSpec; evaluate() runs it through
// the WP-029 QueryEngine and returns bands; documentJson() lays the bands out
// as a qindatk.richtext document that preview, PDF and printers share.
namespace LocalReport {

struct Total {
    QString id;       // QueryMeasure id: "<function>:<field>" or "countRows"
    QString field;    // empty for countRows
    QueryMeasure::Function function = QueryMeasure::Function::CountRows;
    QString label;    // "Sum of amount"
};
struct Column {
    QString field;
    QString label;
};
struct Group {
    QString field;
    QString label;
    bool pageBreakBefore = false; // every group of this level but the first starts a page
    QVector<Total> totals;        // printed in the group footer after the row count
};
struct Spec {
    QString table;
    QString title;
    QString footer;
    QVector<Column> columns;       // detail band
    QVector<Group> groups;         // outermost first, at most MaximumGroups
    QVector<Total> totals;         // report footer
    bool detail = true;            // false: a summary of group headers and footers
    QueryPredicate filter;         // WP-029 predicate; constant true when absent
    QHash<QString, QueryValue> parameters;
    QVector<QuerySort> sort;       // after the group keys (4 keys in all)
    QString paperId = QStringLiteral("a4");
    bool landscape = false;
};

// One printed band instance, in document order.
struct Band {
    enum class Kind { ReportHeader, ReportInfo, GroupHeader, Detail, GroupFooter, ReportFooter, Empty };
    Kind kind = Kind::Detail;
    int level = -1;               // group level for group bands
    QString text;                 // header/footer text
    QStringList cells;            // detail cells, one per column
    bool pageBreakBefore = false;
    bool strong = false;          // totals print bold
};
struct Output {
    QStringList columnLabels;
    QVector<Band> bands;
    int detailRows = 0;
    int groupCount = 0;           // groups of the innermost level
    QString error;
    QString stage;                // "definition", "parameters", "query" or "layout"
};

constexpr int MaximumGroups = 3;
constexpr int MaximumDetailRows = 2000;

// Parses a report map (docs/forms-reports.md#report-definition) against the
// live schema. `parameterValues` overrides the declared parameter values and
// is converted to each parameter's declared type. Returns an error message
// (empty on success) and sets *stage to "definition" or "parameters".
QString parse(const QVariantMap &report, const QuerySchema &schema,
              const QVariantMap &parameterValues, Spec *spec, QString *stage);
Output evaluate(const SqliteTableSource &source, const Spec &spec);
// A complete qindatk.richtext root: tables with one repeated header row per
// detail run, heading paragraphs for group headers and page breaks for
// pageBreakBefore groups. `documentId` keeps the live document's identity.
QJsonObject documentJson(const Output &output, const Spec &spec, const QString &documentId);

// WP-029 aggregate names ("countRows", "sum", ...) and their labels.
bool functionFromName(const QString &name, QueryMeasure::Function *function);
QString functionName(QueryMeasure::Function function);
QString displayValue(const QueryValue &value);
// A plain host value as `type`: numbers from numeric strings, booleans from
// true/false/1/0, text from anything; false when it cannot be converted.
bool convert(const QVariant &value, QueryType type, QueryValue *out);

} // namespace LocalReport
} // namespace QindaTK
