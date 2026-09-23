// SPDX-License-Identifier: LGPL-3.0-or-later
#include "query_core.h"
#include "query_grid_adapter.h"
#include <QJsonDocument>
#include <QtTest>
#include <algorithm>

using namespace QindaTK;
using Kind = QueryPredicate::Kind;
using Compare = QueryPredicate::Compare;
using Function = QueryMeasure::Function;

class FixtureSource : public QuerySource {
public:
    mutable QuerySchema metadata{"sales", "schema-1", {{"region", QueryType::Text, false},
                                                {"product", QueryType::Text, false},
                                                {"amount", QueryType::Number, true}}};
    QString version = QStringLiteral("rev-1");
    QVector<QueryRow> rows;
    mutable std::atomic_bool *cancelAfterTwo = nullptr;
    bool changeSchemaAfterTwo = false;
    QuerySchema schema() const override { return metadata; }
    QString revision() const override { return version; }
    qsizetype rowCount() const override { return rows.size(); }
    QueryCapabilities capabilities() const override { return QueryCapabilities::local(); }
    QueryRow rowAt(qsizetype index, const QStringList &requested) const override
    {
        if (cancelAfterTwo && index == 2) cancelAfterTwo->store(true);
        if (changeSchemaAfterTwo && index == 2) metadata.revision = QStringLiteral("schema-2");
        QueryRow result{rows.at(index).id, {}};
        for (const QString &field : requested) result.values.insert(field, rows.at(index).values.value(field));
        return result;
    }
};

static QueryRow row(const char *id, const char *region, const char *product,
                    QueryValue amount)
{
    return {QString::fromLatin1(id), {{"region", QueryValue::text(QString::fromLatin1(region))},
                                      {"product", QueryValue::text(QString::fromLatin1(product))},
                                      {"amount", amount}}};
}

static FixtureSource fixture()
{
    FixtureSource source;
    source.rows = {row("r1", "East", "A", QueryValue::number(10)),
                   row("r2", "East", "A", QueryValue::number(20)),
                   row("r3", "East", "B", QueryValue::null()),
                   row("r4", "West", "A", QueryValue::number(5)),
                   row("r5", "West", "B", QueryValue::number(15))};
    return source;
}

static QuerySpec spec()
{
    QuerySpec query;
    query.id = QStringLiteral("known-answer");
    query.schemaId = QStringLiteral("sales");
    query.schemaRevision = QStringLiteral("schema-1");
    query.sourceRevision = QStringLiteral("rev-1");
    query.rowFields = {QStringLiteral("region")};
    query.columnFields = {QStringLiteral("product")};
    query.measures = {{"rows", {}, Function::CountRows},
                      {"values", "amount", Function::CountValues},
                      {"sum", "amount", Function::Sum},
                      {"average", "amount", Function::Average},
                      {"minimum", "amount", Function::Minimum},
                      {"maximum", "amount", Function::Maximum}};
    return query;
}

static QString rowId(const char *region)
{ return queryAxisId(QStringLiteral("row"), {QueryValue::text(QString::fromLatin1(region))}); }
static QString columnId(const char *product, const char *measure)
{
    return queryAxisId(QStringLiteral("column"), {QueryValue::text(QString::fromLatin1(product))})
           + QStringLiteral(":measure:") + QString::fromLatin1(measure);
}
static QString totalColumn(const char *measure)
{ return QStringLiteral("total:columns:measure:") + QString::fromLatin1(measure); }

class QueryCoreTest : public QObject {
    Q_OBJECT
private slots:
    void pivotTotalsAndProvenance();
    void typedFilterNullAndSerialization();
    void staleSchemaCapabilityAndCancellation();
    void gridSourceAndResultAdapter();
};

void QueryCoreTest::pivotTotalsAndProvenance()
{
    auto source = fixture();
    const auto query = spec();
    const auto result = QueryEngine::execute(source, query);
    QCOMPARE(result.status, QueryResult::Status::Complete);
    QCOMPARE(result.queryId, query.id);
    QCOMPARE(result.scannedRows, qsizetype(5));
    QCOMPARE(result.matchedRows, qsizetype(5));
    QCOMPARE(result.rowIds.size(), 3);
    QCOMPARE(result.columnIds.size(), 18);
    QCOMPARE(result.rowIds.last(), QStringLiteral("total:rows"));
    QCOMPARE(result.cell(rowId("East"), columnId("A", "sum")).data.toDouble(), 30.0);
    QCOMPARE(result.cell(rowId("East"), columnId("A", "average")).data.toDouble(), 15.0);
    QCOMPARE(result.cell(rowId("East"), columnId("A", "minimum")).data.toDouble(), 10.0);
    QCOMPARE(result.cell(rowId("East"), columnId("A", "maximum")).data.toDouble(), 20.0);
    QCOMPARE(result.cell(rowId("East"), columnId("B", "rows")).data.toLongLong(), 1LL);
    QCOMPARE(result.cell(rowId("East"), columnId("B", "values")).data.toLongLong(), 0LL);
    QCOMPARE(result.cell(rowId("East"), columnId("B", "sum")).type, QueryType::Null);
    QCOMPARE(result.cell(QStringLiteral("total:rows"), totalColumn("rows")).data.toLongLong(), 5LL);
    QCOMPARE(result.cell(QStringLiteral("total:rows"), totalColumn("sum")).data.toDouble(), 50.0);
    QCOMPARE(result.cell(QStringLiteral("total:rows"), totalColumn("average")).data.toDouble(), 12.5);
    QCOMPARE(result.sourceRows(rowId("East"), columnId("A", "sum")),
             (QStringList{"r1", "r2"}));
    auto reversed = fixture();
    std::reverse(reversed.rows.begin(), reversed.rows.end());
    const auto rerun = QueryEngine::execute(reversed, query);
    QCOMPARE(rerun.status, QueryResult::Status::Complete);
    QVERIFY(rerun.rowIds.contains(rowId("East")));
    QVERIFY(rerun.columnIds.contains(columnId("A", "sum")));
    QCOMPARE(rerun.cell(rowId("East"), columnId("A", "sum")).data.toDouble(), 30.0);
}

void QueryCoreTest::typedFilterNullAndSerialization()
{
    auto source = fixture();
    auto query = spec();
    query.filter.kind = Kind::Compare;
    query.filter.fieldId = QStringLiteral("amount");
    query.filter.comparison = Compare::GreaterEqual;
    query.filter.parameterId = QStringLiteral("floor");
    query.parameters.insert("floor", QueryValue::number(10));
    QJsonObject json = QueryEngine::toJson(query);
    QuerySpec decoded; QString error;
    QVERIFY(QueryEngine::fromJson(json, &decoded, &error));
    QCOMPARE(QJsonDocument(QueryEngine::toJson(decoded)).toJson(QJsonDocument::Compact),
             QJsonDocument(json).toJson(QJsonDocument::Compact));
    const auto result = QueryEngine::execute(source, decoded);
    QCOMPARE(result.status, QueryResult::Status::Complete);
    QCOMPARE(result.matchedRows, qsizetype(3));
    QCOMPARE(result.cell(QStringLiteral("total:rows"), totalColumn("sum")).data.toDouble(), 45.0);
    query.parameters.insert("floor", QueryValue::null());
    QCOMPARE(QueryEngine::evaluate(query.filter, source.rows.at(0), query), QueryTruth::Unknown);
    QCOMPARE(QueryEngine::execute(source, query).matchedRows, qsizetype(0));
    query.filter.kind = Kind::IsNull;
    query.filter.parameterId.clear();
    QCOMPARE(QueryEngine::execute(source, query).matchedRows, qsizetype(1));
    query.filter.kind = Kind::Compare;
    query.filter.parameterId = QStringLiteral("floor");
    query.parameters.insert("floor", QueryValue::integer(9007199254740993LL));
    QVERIFY(QueryEngine::fromJson(QueryEngine::toJson(query), &decoded, &error));
    QCOMPARE(decoded.parameters.value("floor").data.toLongLong(), 9007199254740993LL);
    json["filter"] = QJsonObject{{"kind", "compare"}, {"field", "amount"}, {"op", "gt"}};
    QVERIFY(!QueryEngine::fromJson(json, &decoded, &error));
}

void QueryCoreTest::staleSchemaCapabilityAndCancellation()
{
    auto source = fixture(); auto query = spec();
    source.metadata.revision = QStringLiteral("schema-2");
    QCOMPARE(QueryEngine::execute(source, query).status, QueryResult::Status::Rejected);
    source.metadata.revision = QStringLiteral("schema-1");
    source.version = QStringLiteral("rev-2");
    QCOMPARE(QueryEngine::execute(source, query).status, QueryResult::Status::Rejected);
    source.version = QStringLiteral("rev-1");
    query.filter.kind = Kind::Compare; query.filter.fieldId = QStringLiteral("amount");
    query.filter.comparison = Compare::Contains; query.filter.literal = QueryValue::text("10");
    QVERIFY(!QueryEngine::validate(source.schema(), query, source.capabilities()).isEmpty());
    query = spec();
    QueryLimits limits; limits.maxScanRows = 4;
    QCOMPARE(QueryEngine::execute(source, query, limits).status, QueryResult::Status::Rejected);
    limits.maxScanRows = 5; limits.maxOutputCells = 3;
    QCOMPARE(QueryEngine::execute(source, query, limits).status, QueryResult::Status::Rejected);
    limits = {};
    std::atomic_bool cancel{true};
    QCOMPARE(QueryEngine::execute(source, query, limits, &cancel).status, QueryResult::Status::Cancelled);
    cancel.store(false); source.cancelAfterTwo = &cancel;
    const auto midRun = QueryEngine::execute(source, query, limits, &cancel);
    QCOMPARE(midRun.status, QueryResult::Status::Cancelled);
    QVERIFY(midRun.cells.isEmpty());
    QVERIFY(midRun.provenance.isEmpty());
    source.cancelAfterTwo = nullptr;
    source.changeSchemaAfterTwo = true;
    const auto changedMidRun = QueryEngine::execute(source, query);
    QCOMPARE(changedMidRun.status, QueryResult::Status::Rejected);
    QVERIFY(changedMidRun.cells.isEmpty());
}

void QueryCoreTest::gridSourceAndResultAdapter()
{
    EditableGridModel grid;
    QVERIFY(grid.setRowIds({"r1", "r2"}));
    QVERIFY(grid.setColumnIds({"region", "amount"}));
    QVERIFY(grid.beginEdit());
    QVERIFY(grid.stageCell("r1", "region", QStringLiteral("East")));
    QVERIFY(grid.stageCell("r1", "amount", 10.0));
    QVERIFY(grid.stageCell("r2", "region", QStringLiteral("West")));
    QVERIFY(grid.stageCell("r2", "amount", 5.0));
    QVERIFY(grid.commitEdit());
    QuerySchema schema{"grid-sales", "fields-1", {{"region", QueryType::Text, false},
                                                  {"amount", QueryType::Number, false}}};
    GridQuerySource source(&grid, schema);
    QuerySpec query;
    query.id = QStringLiteral("grid-refresh-1");
    query.schemaId = schema.id; query.schemaRevision = schema.revision;
    query.sourceRevision = grid.revision(); query.rowFields = {"region"};
    query.measures = {{"sum", "amount", Function::Sum}};
    const auto result = QueryEngine::execute(source, query);
    QCOMPARE(result.status, QueryResult::Status::Complete);
    const auto resultGrid = queryResultAsGrid(result);
    QVERIFY(resultGrid);
    QVERIFY(resultGrid->readOnly());
    QCOMPARE(resultGrid->rowCount(), 3);
    QCOMPARE(resultGrid->columnCount(), 1);
    const QString col = result.columnIds.first();
    QCOMPARE(resultGrid->cellValue(rowId("East"), col).toDouble(), 10.0);
    QCOMPARE(result.sourceRows(rowId("East"), col), (QStringList{"r1"}));
    QVERIFY(!(resultGrid->flags(resultGrid->index(0, 0)) & Qt::ItemIsEditable));
    QVERIFY(!resultGrid->setData(resultGrid->index(0, 0), 99.0));
    QVERIFY(!resultGrid->beginEdit());
    QVERIFY(!resultGrid->stageCell(rowId("East"), col, 99.0));
    QVERIFY(!resultGrid->paste(0, 0, {QVariantList{99.0}}));
    QVERIFY(!resultGrid->fill(0, 0, 1, 1, 1, 0));
    QVERIFY(!resultGrid->undo());
    QVERIFY(!resultGrid->applyHistoryChanges(QString(), {}, true, resultGrid->revision()));
    QCOMPARE(resultGrid->cellValue(rowId("East"), col).toDouble(), 10.0);
    QVERIFY(grid.setData(grid.index(1, 1), 7.0));
    QCOMPARE(QueryEngine::execute(source, query).status, QueryResult::Status::Rejected);
}

QTEST_MAIN(QueryCoreTest)
#include "tst_query_core.moc"
