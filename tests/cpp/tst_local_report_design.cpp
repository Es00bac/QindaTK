// SPDX-License-Identifier: LGPL-3.0-or-later
// Report evaluation and output (WP-059, docs/forms-reports.md): bands,
// nested groups and totals with nulls through the WP-029 engine, typed
// parameter binding, data and definition failures that keep the last layout,
// group page breaks, running page header/footer bands with page numbers,
// and printed/PDF output through the WP-053 adapter.
#include "local_database_provider.h"
#include "local_report_document_model.h"
#include "local_report_spec.h"
#include "print_adapter.h"
#include "rich_text_codec.h"
#include "rich_text_layout.h"

#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
bool seedSales(LocalDatabaseProvider &db)
{
    if (!db.openMemory()) return false;
    if (!db.executeMigration(QStringLiteral("CREATE TABLE sales(id INTEGER PRIMARY KEY, region TEXT NOT NULL, "
                                            "rep TEXT, amount REAL, units INTEGER)")).value("ok").toBool())
        return false;
    const QList<QVariantList> rows{
        {1, QStringLiteral("North"), QStringLiteral("Ann"), 10.5, 1},
        {2, QStringLiteral("North"), QStringLiteral("Ann"), 20.0, 2},
        {3, QStringLiteral("North"), QStringLiteral("Bo"), QVariant(), 3},
        {4, QStringLiteral("South"), QStringLiteral("Cy"), 5.0, QVariant()},
        {5, QStringLiteral("South"), QStringLiteral("Cy"), 7.25, 4},
        {6, QStringLiteral("South"), QVariant(), 1.0, 1}};
    for (const QVariantList &row : rows)
        if (!db.execute(QStringLiteral("INSERT INTO sales(id, region, rep, amount, units) VALUES(?, ?, ?, ?, ?)"), row)
                 .value("ok").toBool())
            return false;
    return true;
}

QVariantMap total(const QString &field, const QString &function)
{
    return {{QStringLiteral("field"), field}, {QStringLiteral("function"), function}};
}

QVariantMap groupedReport(bool breakRegions = false)
{
    return {{"tableName", "sales"}, {"headerText", "Sales"}, {"footerText", "Internal"},
            {"columns", QVariantList{QStringLiteral("id"), QVariantMap{{"field", "amount"}, {"label", "Amount"}}}},
            {"groups", QVariantList{
                QVariantMap{{"field", "region"}, {"pageBreakBefore", breakRegions},
                            {"totals", QVariantList{total("amount", "sum"), total("units", "average")}}},
                QVariantMap{{"field", "rep"}, {"totals", QVariantList{total("amount", "sum")}}}}},
            {"totals", QVariantList{total("amount", "sum"), total("units", "countValues")}}};
}

bool seedLines(LocalDatabaseProvider &db, int count)
{
    if (!db.openMemory()) return false;
    if (!db.executeMigration(QStringLiteral("CREATE TABLE lines(id INTEGER PRIMARY KEY, label TEXT NOT NULL)"))
             .value("ok").toBool())
        return false;
    for (int i = 1; i <= count; ++i)
        if (!db.execute(QStringLiteral("INSERT INTO lines(label) VALUES(?)"),
                        {QStringLiteral("Line %1").arg(i, 3, 10, QLatin1Char('0'))}).value("ok").toBool())
            return false;
    return true;
}

// Whether any pixel in rows [top, bottom) of `image` is darker than paper.
bool inked(const QImage &image, int top, int bottom)
{
    for (int y = qMax(0, top); y < qMin(image.height(), bottom); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) < 200) return true;
    return false;
}

QStringList bandTexts(const LocalReportDocumentModel &model, const QStringList &kinds)
{
    QStringList out;
    for (const QVariant &band : model.bands())
        if (kinds.contains(band.toMap().value("kind").toString())) out.append(band.toMap().value("text").toString());
    return out;
}

int pdfPages(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return -1;
    static const QRegularExpression page(QStringLiteral("/Type\\s*/Page(?![A-Za-z])"));
    int pages = 0;
    for (auto it = page.globalMatch(QString::fromLatin1(file.readAll())); it.hasNext(); it.next()) ++pages;
    return pages;
}

// A raster printer that keeps each printed sheet.
struct PrintedSheets {
    QVector<QImage> sheets;
    bool finished = false;
};
class SheetDevice final : public Print::Device {
public:
    explicit SheetDevice(std::shared_ptr<PrintedSheets> log) : m_log(std::move(log)) {}
    bool beginSheet(const Print::Sheet &sheet, int, QString *) override
    {
        close();
        m_image = QImage(Print::sheetPixelSize(sheet, 1.0), QImage::Format_ARGB32_Premultiplied);
        m_image.fill(Qt::white);
        return m_painter.begin(&m_image);
    }
    QPainter *painter() override { return &m_painter; }
    bool finish(QString *) override { close(); m_log->finished = true; return true; }
    void abort() override { if (m_painter.isActive()) m_painter.end(); }
private:
    void close()
    {
        if (!m_painter.isActive()) return;
        m_painter.end();
        m_log->sheets.append(m_image);
    }
    std::shared_ptr<PrintedSheets> m_log;
    QImage m_image;
    QPainter m_painter;
};
class SheetBackend final : public Print::Backend {
public:
    std::shared_ptr<PrintedSheets> log = std::make_shared<PrintedSheets>();
    QVector<Print::Capabilities> destinations() override
    {
        Print::Capabilities c;
        c.printerId = QStringLiteral("office");
        c.label = QStringLiteral("Office printer");
        c.papers = Print::standardPapers();
        return {c};
    }
    std::unique_ptr<Print::Device> open(const Print::Ticket &, const Print::Capabilities &, QString *) override
    {
        return std::make_unique<SheetDevice>(log);
    }
};
} // namespace

class LocalReportDesignTest final : public QObject {
    Q_OBJECT
private slots:
    void nestedGroupsTotalsAndNulls()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedSales(db));
        LocalReportDocumentModel output;
        const QVariantMap built = output.build(&db, groupedReport());
        QVERIFY2(built.value("ok").toBool(), qPrintable(built.value("error").toString()));
        QCOMPARE(output.rowCount(), 6);
        QCOMPARE(output.groupCount(), 4);
        QCOMPARE(output.columns(), (QStringList{"id", "Amount"}));
        QCOMPARE(bandTexts(output, {"reportInfo"}), QStringList{"Source: main.sales · 6 rows"});
        // Group keys sort with nulls last; SUM/AVG skip nulls and a group
        // whose values are all null totals to null, never zero.
        const QStringList expected{
            "region: North", "rep: Ann", "rep Ann total · Count 2 · Sum of amount 30.50",
            "rep: Bo", "rep Bo total · Count 1 · Sum of amount —",
            "region North total · Count 3 · Sum of amount 30.50 · Average of units 2",
            "region: South", "rep: Cy", "rep Cy total · Count 2 · Sum of amount 12.25",
            "rep: —", "rep — total · Count 1 · Sum of amount 1",
            "region South total · Count 3 · Sum of amount 13.25 · Average of units 2.50",
            "Report total · Count 6 · Sum of amount 43.75 · Count of units 5", "Internal"};
        QCOMPARE(bandTexts(output, {"groupHeader", "groupFooter", "reportFooter"}), expected);
        QStringList details;
        for (const QVariant &band : output.bands())
            if (band.toMap().value("kind") == QStringLiteral("detail"))
                details.append(band.toMap().value("cells").toStringList().join(u'|'));
        QCOMPARE(details, (QStringList{"1|10.50", "2|20", "3|—", "4|5", "5|7.25", "6|1"}));
        // One table per innermost group; its first row is the repeated header.
        const QVariantList tables = output.document()->tableSnapshots();
        QCOMPARE(tables.size(), 4);
        const QVariantList firstRows = tables.first().toMap().value("rows").toList();
        QCOMPARE(firstRows.size(), 3);
        QVERIFY(firstRows.first().toMap().value("header").toBool());
        QVERIFY(!firstRows.at(1).toMap().value("header").toBool());
        QVERIFY(output.document()->text().contains(QStringLiteral("1\t10.50")));
    }

    // A column's number format prints money as money — detail cells and
    // the totals that inherit it — and leaves counts and text alone.
    void numberFormatsPrintMoneyAsMoney()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedSales(db));
        QVERIFY(db.execute(QStringLiteral("INSERT INTO sales(id, region, rep, amount, units) VALUES(7, 'South', 'Cy', 1250, 2)"))
                    .value("ok").toBool());
        const QVariantMap money{{"decimals", 2}, {"grouping", true}, {"prefix", "$"}};
        const QVariantMap report{
            {"tableName", "sales"},
            {"columns", QVariantList{QStringLiteral("id"), QVariantMap{{"field", "amount"}, {"label", "Amount"}, {"format", money}},
                                     QVariantMap{{"field", "units"}, {"format", QVariantMap{{"suffix", " u"}}}}}},
            {"groups", QVariantList{QVariantMap{{"field", "region"}, {"totals", QVariantList{total("amount", "sum")}}}}},
            {"totals", QVariantList{total("amount", "sum"), total("amount", "countValues"),
                                    QVariantMap{{"field", "units"}, {"function", "sum"},
                                                {"format", QVariantMap{{"decimals", 1}}}}}}};
        LocalReportDocumentModel output;
        const QVariantMap built = output.build(&db, report);
        QVERIFY2(built.value("ok").toBool(), qPrintable(built.value("error").toString()));
        QStringList details;
        for (const QVariant &band : output.bands())
            if (band.toMap().value("kind") == QStringLiteral("detail"))
                details.append(band.toMap().value("cells").toStringList().join(u'|'));
        QCOMPARE(details.first(), QStringLiteral("1|$10.50|1 u"));
        QVERIFY(details.contains(QStringLiteral("2|$20.00|2 u")));
        QVERIFY(details.contains(QStringLiteral("3|—|3 u")));
        QVERIFY(details.contains(QStringLiteral("7|$1,250.00|2 u")));
        QCOMPARE(bandTexts(output, {"groupFooter"}).first(), QStringLiteral("region North total · Count 3 · Sum of amount $30.50"));
        QCOMPARE(bandTexts(output, {"reportFooter"}).first(),
                 QStringLiteral("Report total · Count 7 · Sum of amount $1,293.75 · Count of amount 6 · Sum of units 13.0"));
        // A bad format is a definition error naming the column.
        QVariantMap bad = report;
        bad.insert("columns", QVariantList{QVariantMap{{"field", "amount"}, {"format", QVariantMap{{"decimals", 9}}}}});
        const QVariantMap refused = output.build(&db, bad);
        QVERIFY(!refused.value("ok").toBool());
        QVERIFY(refused.value("error").toString().contains(QStringLiteral("amount")));
        QCOMPARE(LocalReport::displayValue(QueryValue::number(-0.001), LocalReport::NumberFormat{2, true, "$", ""}),
                 QStringLiteral("$0.00"));
        QCOMPARE(LocalReport::displayValue(QueryValue::number(-1234.5), LocalReport::NumberFormat{2, true, "$", ""}),
                 QStringLiteral("-$1,234.50"));
    }

    void summaryAndLegacyDefinitions()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedSales(db));
        LocalReportDocumentModel output;
        QVariantMap summary = groupedReport();
        summary.insert("detail", false);
        QVERIFY(output.build(&db, summary).value("ok").toBool());
        QCOMPARE(bandTexts(output, {"detail"}).size(), 0);
        QVERIFY(output.document()->tableSnapshots().isEmpty());
        QCOMPARE(output.groupCount(), 4);
        // The first-cut keys: one groupField with a SUM prints a summary.
        const QVariantMap legacy{{"tableName", "sales"}, {"groupField", "region"}, {"sumField", "units"},
                                 {"filterField", "region"}, {"filterValue", "South"}};
        QVERIFY(output.build(&db, legacy).value("ok").toBool());
        QCOMPARE(output.rowCount(), 3);
        QCOMPARE(bandTexts(output, {"groupFooter"}), QStringList{"region South total · Count 3 · Sum of units 5"});
        QVERIFY(!output.build(&db, {{"tableName", "sales"}, {"sumField", "units"}}).value("ok").toBool());
    }

    void typedParametersBindSafely()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedSales(db));
        LocalReportDocumentModel output;
        const QVariantMap atLeast{{"tableName", "sales"}, {"columns", QVariantList{"id"}},
            {"query", QVariantMap{
                {"filter", QVariantMap{{"kind", "compare"}, {"field", "units"}, {"op", "ge"}, {"parameter", "minUnits"}}},
                {"parameters", QVariantMap{{"minUnits", QVariantMap{{"type", "integer"}, {"value", "2"}}}}}}}};
        QVERIFY(output.build(&db, atLeast).value("ok").toBool());
        QCOMPARE(output.rowCount(), 3); // units 2, 3, 4; a null never compares true
        QVERIFY(output.build(&db, atLeast, {{"minUnits", 4}}).value("ok").toBool());
        QCOMPARE(output.rowCount(), 1);
        QVERIFY(output.build(&db, atLeast, {{"minUnits", "3"}}).value("ok").toBool());
        QCOMPARE(output.rowCount(), 2);
        const QString text = output.document()->text();
        QVariantMap refused = output.build(&db, atLeast, {{"minUnits", "many"}});
        QVERIFY(!refused.value("ok").toBool());
        QCOMPARE(refused.value("stage").toString(), QStringLiteral("parameters"));
        QCOMPARE(output.errorStage(), QStringLiteral("parameters"));
        QCOMPARE(output.rowCount(), 2);
        QCOMPARE(output.document()->text(), text);
        QCOMPARE(output.build(&db, atLeast, {{"unknown", 1}}).value("stage").toString(), QStringLiteral("parameters"));
        QVariantMap mistyped = atLeast;
        QVariantMap query = mistyped.value("query").toMap();
        query.insert("parameters", QVariantMap{{"minUnits", QVariantMap{{"type", "text"}, {"value", "2"}}}});
        mistyped.insert("query", query);
        QCOMPARE(output.build(&db, mistyped).value("stage").toString(), QStringLiteral("parameters"));

        // A hostile text value is data, never SQL.
        const QVariantMap byRep{{"tableName", "sales"},
            {"query", QVariantMap{
                {"filter", QVariantMap{{"kind", "compare"}, {"field", "rep"}, {"op", "eq"}, {"parameter", "who"}}},
                {"parameters", QVariantMap{{"who", QVariantMap{{"type", "text"}, {"value", "' OR 1=1 --"}}}}}}}};
        QVERIFY(output.build(&db, byRep).value("ok").toBool());
        QCOMPARE(output.rowCount(), 0);
        QCOMPARE(bandTexts(output, {"empty"}), QStringList{"No rows match this report."});
        QVERIFY(output.build(&db, byRep, {{"who", "Cy"}}).value("ok").toBool());
        QCOMPARE(output.rowCount(), 2);
        QCOMPARE(db.query("SELECT COUNT(*) AS n FROM sales").value("rows").toList().first().toMap().value("n").toInt(), 6);
    }

    void failuresKeepTheLastLayout()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedSales(db));
        LocalReportDocumentModel output;
        QVERIFY(output.build(&db, groupedReport()).value("ok").toBool());
        const QString text = output.document()->text();
        const int bands = output.bands().size();

        QVariantMap broken = groupedReport();
        broken.insert("groups", QVariantList{QVariantMap{{"field", "territory"}}});
        QVariantMap result = output.build(&db, broken);
        QCOMPARE(result.value("stage").toString(), QStringLiteral("definition"));
        QVariantMap textSum = groupedReport();
        textSum.insert("totals", QVariantList{total("rep", "sum")});
        QCOMPARE(output.build(&db, textSum).value("stage").toString(), QStringLiteral("definition"));
        QCOMPARE(output.build(&db, {{"tableName", "missing"}}).value("stage").toString(), QStringLiteral("query"));

        // SQLite stores text in a REAL column; the report names the column
        // instead of printing a guess or dropping the row.
        QVERIFY(db.execute(QStringLiteral("UPDATE sales SET amount = ? WHERE id = ?"), {QStringLiteral("lots"), 2})
                    .value("ok").toBool());
        result = output.build(&db, groupedReport());
        QVERIFY(!result.value("ok").toBool());
        QCOMPARE(result.value("stage").toString(), QStringLiteral("query"));
        QCOMPARE(result.value("error").toString(), QStringLiteral("Column amount holds text in a number column"));
        QCOMPARE(output.document()->text(), text);
        QCOMPARE(output.bands().size(), bands);
        QCOMPARE(output.rowCount(), 6);
    }

    void pageRulesAndPrintedOutput()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedSales(db));
        LocalReportDocumentModel output;
        QVERIFY(output.build(&db, groupedReport(false)).value("ok").toBool());
        {
            PrintAdapter adapter;
            adapter.setDocument(output.document());
            QCOMPARE(adapter.pageCount(), 1);
        }
        QVERIFY(output.build(&db, groupedReport(true)).value("ok").toBool());
        int breaks = 0;
        for (const QVariant &band : output.bands()) breaks += band.toMap().value("pageBreakBefore").toBool() ? 1 : 0;
        QCOMPARE(breaks, 1); // South only: the first group never starts a page

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto printer = std::make_shared<SheetBackend>();
        PrintAdapter adapter;
        adapter.setBackends({printer});
        adapter.setDocument(output.document());
        QCOMPARE(adapter.pageCount(), 2);
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), QStringLiteral("office")},
                                       {QStringLiteral("paperId"), QStringLiteral("a4")},
                                       {QStringLiteral("includeBleed"), false}}));
        const QImage preview = adapter.previewImage(1, 1.0);
        QVERIFY2(!adapter.print().isEmpty(), qPrintable(adapter.lastError()));
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QVERIFY(printer->log->finished);
        QCOMPARE(printer->log->sheets.size(), 2);
        QVERIFY(printer->log->sheets.at(1) == preview); // the printer got what the preview showed

        // PDF through the adapter and the model's default ticket agree on pages.
        PrintAdapter pdfAdapter;
        pdfAdapter.setDocument(output.document());
        const QString viaAdapter = directory.filePath(QStringLiteral("adapter.pdf"));
        QVERIFY(pdfAdapter.applySettings({{QStringLiteral("outputPath"), viaAdapter}}));
        QVERIFY(!pdfAdapter.print().isEmpty());
        QTRY_COMPARE(pdfAdapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        const QString exported = directory.filePath(QStringLiteral("export.pdf"));
        QVERIFY(output.exportPdf(exported).value("ok").toBool());
        QCOMPARE(pdfPages(viaAdapter), 2);
        QCOMPARE(pdfPages(exported), 2);
    }

    void longDetailPaginates()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedLines(db, 300));
        LocalReportDocumentModel output;
        QVERIFY(output.build(&db, {{"tableName", "lines"}, {"page", QVariantMap{{"paperId", "letter"}}}})
                    .value("ok").toBool());
        QCOMPARE(output.rowCount(), 300);
        QCOMPARE(output.document()->tableSnapshots().size(), 1);
        QCOMPARE(output.document()->pageSetupMap().value("widthPt").toDouble(), 612.0);
        PrintAdapter adapter;
        adapter.setDocument(output.document());
        QVERIFY2(adapter.pageCount() >= 3, qPrintable(QString::number(adapter.pageCount())));
        QVERIFY(output.document()->text().contains(QStringLiteral("300\tLine 300")));

        // The column-label row is the table's header row and every later
        // page starts with it repeated, above that page's first detail row.
        RichTextLayout layout;
        layout.setDocument(output.document());
        layout.paginateAll();
        QCOMPARE(layout.pageCount(), adapter.pageCount());
        const QUuid table = output.document()->blocks().at(2).id(); // header, info, table
        int nextRow = 1;
        for (int i = 0; i < layout.pageCount(); ++i) {
            QVector<RichText::PageItem> rows;
            for (const RichText::PageItem &item : layout.page(i).items)
                if (item.kind == RichText::PageItem::TableRows) rows.append(item);
            if (rows.isEmpty()) continue;
            QCOMPARE(rows.first().block, table);
            if (i == 0) {
                QVERIFY(!rows.first().headerRepeat);
                QCOMPARE(rows.first().first, 0);
                nextRow = rows.first().first + rows.first().count;
                continue;
            }
            QVERIFY2(rows.first().headerRepeat, qPrintable(QStringLiteral("page %1").arg(i + 1)));
            QCOMPARE(rows.first().first, 0);
            QCOMPARE(rows.first().count, 1);
            QCOMPARE(rows.at(1).first, nextRow); // no detail row lost or repeated
            nextRow = rows.at(1).first + rows.at(1).count;
        }
        QCOMPARE(nextRow, 301);
    }

    void pageHeaderAndFooterBandsNumberEveryPage()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedLines(db, 300));
        LocalReportDocumentModel output;
        // Without pageFooterText a report numbers its pages.
        QVERIFY(output.build(&db, {{"tableName", "lines"}, {"pageHeaderText", "Line register · page {page}"}})
                    .value("ok").toBool());
        QCOMPARE(output.document()->pageSetupValue().footerText, LocalReport::defaultPageFooter());
        RichTextLayout layout;
        layout.setDocument(output.document());
        layout.paginateAll();
        const int pages = layout.pageCount();
        QVERIFY(pages >= 3);
        for (int i = 0; i < pages; ++i) {
            QCOMPARE(layout.pageRegionText(i, true), QStringLiteral("Page %1 of %2").arg(i + 1).arg(pages));
            QCOMPARE(layout.pageRegionText(i, false), QStringLiteral("Line register · page %1").arg(i + 1));
        }
        // The bands survive the native format, which needs version 2 for them.
        const QJsonObject json = RichTextCodec::toJsonObject(*output.document());
        QCOMPARE(json.value("version").toInt(), 2);
        QCOMPARE(json.value("page").toObject().value("footerText").toString(), QStringLiteral("Page {page} of {pages}"));
        RichTextDocument reopened;
        QVERIFY(RichTextCodec::fromJsonObject(json, reopened));
        QCOMPARE(reopened.pageSetupValue(), output.document()->pageSetupValue());

        // Printed sheets carry the bands in their margins, match the preview,
        // and differ page to page only where the number changes.
        auto printer = std::make_shared<SheetBackend>();
        PrintAdapter adapter;
        adapter.setBackends({printer});
        adapter.setDocument(output.document());
        QCOMPARE(adapter.pageCount(), pages);
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), QStringLiteral("office")},
                                       {QStringLiteral("paperId"), QStringLiteral("a4")},
                                       {QStringLiteral("includeBleed"), false}}));
        const QImage preview = adapter.previewImage(1, 1.0);
        QVERIFY2(!adapter.print().isEmpty(), qPrintable(adapter.lastError()));
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(printer->log->sheets.size(), pages);
        QVERIFY(printer->log->sheets.at(1) == preview);
        const QImage &first = printer->log->sheets.at(0);
        const int h = first.height();
        const int margin = qRound(h * 48.0 / 841.89) - 2; // 48pt bands on an A4 sheet
        QVERIFY(inked(first, 0, margin));
        QVERIFY(inked(first, h - margin, h));
        QVERIFY(first.copy(0, h - margin, first.width(), margin)
                != printer->log->sheets.at(1).copy(0, h - margin, first.width(), margin));

        // "" turns a band off: the margins stay paper.
        QVERIFY(output.build(&db, {{"tableName", "lines"}, {"pageFooterText", ""}}).value("ok").toBool());
        PrintAdapter plain;
        plain.setDocument(output.document());
        const QImage bare = plain.previewImage(1, 1.0);
        QVERIFY(!inked(bare, 0, margin));
        QVERIFY(!inked(bare, bare.height() - margin, bare.height()));
        QVERIFY(output.document()->pageSetupValue().footerText.isEmpty());
        QVariantMap tooLong{{"tableName", "lines"}, {"pageFooterText", QString(501, u'x')}};
        QCOMPARE(output.build(&db, tooLong).value("stage").toString(), QStringLiteral("definition"));
    }
};

QTEST_MAIN(LocalReportDesignTest)
#include "tst_local_report_design.moc"
