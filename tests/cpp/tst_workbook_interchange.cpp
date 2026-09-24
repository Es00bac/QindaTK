// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook.h"
#include "workbook_delimited.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool writeAll(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QVariantMap waitForJob(FormatRegistry &registry, const QString &id)
{
    if (id.isEmpty()) return {};
    const bool finished = QTest::qWaitFor([&]() {
        const QString state = registry.jobs()->job(id).value("state").toString();
        return state == QStringLiteral("succeeded") || state == QStringLiteral("failed");
    }, 10000);
    return finished ? registry.jobs()->job(id) : QVariantMap();
}
}

class WorkbookInterchangeTest : public QObject {
    Q_OBJECT
private slots:
    void delimitedParsing();
    void csvImportTypesValues();
    void csvExportWritesDisplayedValuesAndDeclaresLosses();
    void registryExportsSourcesAndAdoptsImports();
};

void WorkbookInterchangeTest::delimitedParsing()
{
    QList<QStringList> rows;
    QString error;
    QVERIFY(WorkbookDelimitedFormat::parse(QStringLiteral("a,\"b,c\",\"say \"\"hi\"\"\"\r\n1,,\"two\nlines\"\n"),
                                           QLatin1Char(','), &rows, &error));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0], (QStringList{"a", "b,c", "say \"hi\""}));
    QCOMPARE(rows[1], (QStringList{"1", "", "two\nlines"}));
    QVERIFY(WorkbookDelimitedFormat::parse(QStringLiteral("x\ty\n\n\"\""), QLatin1Char('\t'), &rows, &error));
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[1], QStringList{QString()});
    QCOMPARE(rows[2], QStringList{QString()});
    QVERIFY(!WorkbookDelimitedFormat::parse(QStringLiteral("a,\"open"), QLatin1Char(','), &rows, &error));
    QCOMPARE(error, QStringLiteral("A quoted field is not closed"));
    QCOMPARE(WorkbookDelimitedFormat::quoted(QStringLiteral("plain"), QLatin1Char(',')), QStringLiteral("plain"));
    QCOMPARE(WorkbookDelimitedFormat::quoted(QStringLiteral("a,b \"c\""), QLatin1Char(',')), QStringLiteral("\"a,b \"\"c\"\"\""));
    QCOMPARE(WorkbookDelimitedFormat::quoted(QStringLiteral(" padded"), QLatin1Char('\t')), QStringLiteral("\" padded\""));
}

void WorkbookInterchangeTest::csvImportTypesValues()
{
    const WorkbookDelimitedFormat csv(QLatin1Char(','));
    const QByteArray bytes = "\xEF\xBB\xBFName,Amount,Paid,Check,Formula\nRent,1200.5,TRUE,#N/A,=B2*2\n\"Smith, J\",-3e2,false,,=SUM(\n";
    const CancellationSource never;
    FormatPayload imported = csv.importDocument(bytes, {}, never.token(), {});
    QVERIFY2(!imported.error, qPrintable(imported.error.message));
    QCOMPARE(imported.warnings.size(), 1); // formula-like fields stay text by default
    Workbook wb;
    QVERIFY(wb.restore(imported.bytes));
    const QString s = wb.sheetIdAt(0);
    QCOMPARE(wb.sheetName(s), QStringLiteral("Sheet1"));
    QCOMPARE(wb.cellType(s, 1, 1), QStringLiteral("number"));
    QCOMPARE(wb.cellDisplay(s, 2, 1), QStringLiteral("-300"));
    QCOMPARE(wb.cellType(s, 1, 2), QStringLiteral("boolean"));
    QCOMPARE(wb.cellDisplay(s, 1, 3), QStringLiteral("#N/A"));
    QCOMPARE(wb.cellInput(s, 2, 0), QStringLiteral("Smith, J"));
    QCOMPARE(wb.cellInput(s, 1, 4), QStringLiteral("'=B2*2")); // text, shown escaped as a cell input
    QCOMPARE(wb.rowCount(s), Workbook::DefaultRows);

    imported = csv.importDocument(bytes, {{"formulas", true}, {"sheetName", "Costs"}}, never.token(), {});
    QVERIFY(wb.restore(imported.bytes));
    QCOMPARE(wb.sheetName(wb.sheetIdAt(0)), QStringLiteral("Costs"));
    QCOMPARE(wb.cellInput(wb.sheetIdAt(0), 1, 4), QStringLiteral("=B2*2"));
    QCOMPARE(wb.cellDisplay(wb.sheetIdAt(0), 1, 4), QStringLiteral("2401"));
    QCOMPARE(wb.cellInput(wb.sheetIdAt(0), 2, 4), QStringLiteral("'=SUM(")); // did not parse: text
    QCOMPARE(imported.warnings.size(), 1);

    QVERIFY(csv.importDocument("\xFF\xFE", {}, never.token(), {}).error);
    QVERIFY(csv.importDocument("a,\"b", {}, never.token(), {}).error);
    QVERIFY(csv.importDocument("a", {{"bogus", 1}}, never.token(), {}).error);
    QVERIFY(csv.importDocument("a", {{"sheetName", "bad:name"}}, never.token(), {}).error);
}

void WorkbookInterchangeTest::csvExportWritesDisplayedValuesAndDeclaresLosses()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(wb.renameSheet(s, "Budget"));
    QVERIFY(wb.pasteInputs(s, 0, 0, {QVariant(QVariantList{"Item", "Amount"}),
                                     QVariant(QVariantList{"Rent, office", "1234.5"}),
                                     QVariant(QVariantList{"Total", "=B2*2"})}));
    QVERIFY(wb.applyStyle(s, 1, 1, 2, 1, {{"numberFormat", "#,##0.00"}}));
    const QString other = wb.addSheet("Notes");
    QVERIFY(wb.setCellInput(other, 0, 0, "note"));
    const WorkbookDelimitedFormat csv(QLatin1Char(',')), tsv(QLatin1Char('\t'));
    const CancellationSource never;
    FormatPayload exported = csv.exportDocument(wb.snapshot(), {}, never.token(), {});
    QVERIFY2(!exported.error, qPrintable(exported.error.message));
    QCOMPARE(exported.bytes, QByteArray("Item,Amount\r\n\"Rent, office\",\"1,234.50\"\r\nTotal,\"2,469.00\"\r\n"));
    QCOMPARE(exported.losses, (QVariantList{"formulas", "otherSheets", "formats"}));
    exported = tsv.exportDocument(wb.snapshot(), {{"sheet", "notes"}, {"byteOrderMark", true}}, never.token(), {});
    QCOMPARE(exported.bytes, QByteArray("\xEF\xBB\xBFnote\n"));
    QVERIFY(wb.setSheetProperties(s, {{"hidden", true}}));
    exported = csv.exportDocument(wb.snapshot(), {{"lineEnding", "lf"}}, never.token(), {});
    QCOMPARE(exported.bytes, QByteArray("note\n")); // the first visible sheet
    QVERIFY(csv.exportDocument(wb.snapshot(), {{"sheet", "Missing"}}, never.token(), {}).error);
    QVERIFY(csv.preflightExport(nullptr, {}).error);
}

void WorkbookInterchangeTest::registryExportsSourcesAndAdoptsImports()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(wb.pasteInputs(s, 0, 0, {QVariant(QVariantList{"x", "1"}), QVariant(QVariantList{"y", "=B1+1"})}));
    FormatRegistry registry;
    QVERIFY(wb.registerFormat(&registry));
    QVERIFY(!wb.registerFormat(&registry));
    QStringList exportIds;
    for (const QVariant &format : registry.formatsForOperation(QStringLiteral("export")))
        exportIds.append(format.toMap().value("formatId").toString());
    QVERIFY(exportIds.contains(QStringLiteral("text.csv")) && exportIds.contains(QStringLiteral("text.tsv")));

    // G-6: a Workbook exports through the registry as a snapshot source.
    const QUrl target = QUrl::fromLocalFile(dir.filePath(QStringLiteral("out.csv")));
    const QVariantMap plan = registry.preflightExportSource(&wb, target, QStringLiteral("text.csv"));
    QVERIFY2(plan.value("ok").toBool(), qPrintable(QVariant(plan).toString()));
    QVERIFY(!registry.preflightExportSource(&registry, target, QStringLiteral("text.csv")).value("ok").toBool());
    const QVariantMap exportJob = waitForJob(registry, registry.startExportSource(&wb, target, QStringLiteral("text.csv")));
    QCOMPARE(exportJob.value("state").toString(), QStringLiteral("succeeded"));
    QCOMPARE(readAll(target.toLocalFile()), QByteArray("x,1\r\ny,2\r\n"));
    QCOMPARE(exportJob.value("result").toMap().value("losses").toList(), QVariantList{"formulas"});

    // Imports adopt into a Workbook (all-or-nothing, like restore).
    const QString source = dir.filePath(QStringLiteral("in.tsv"));
    QVERIFY(writeAll(source, "a\t10\nb\t20\n"));
    const QVariantMap importJob = waitForJob(registry, registry.startImport(QUrl::fromLocalFile(source)));
    QCOMPARE(importJob.value("state").toString(), QStringLiteral("succeeded"));
    Workbook adopted;
    QVERIFY(adopted.adoptImport(&registry, importJob.value("id").toString()));
    QCOMPARE(adopted.cellDisplay(adopted.sheetIdAt(0), 1, 1), QStringLiteral("20"));
    QVERIFY(adopted.isModified()); // not saved in the native format yet
    QVERIFY(adopted.fileUrl().isEmpty());
    QVERIFY(!adopted.adoptImport(&registry, exportJob.value("id").toString()));
    QVERIFY(!adopted.adoptImport(&wb, importJob.value("id").toString()));

    // A CSV round trip keeps displayed values.
    const QVariantMap back = waitForJob(registry, registry.startImport(target));
    Workbook reread;
    QVERIFY(reread.adoptImport(&registry, back.value("id").toString()));
    QCOMPARE(reread.cellDisplay(reread.sheetIdAt(0), 1, 1), QStringLiteral("2"));
    QCOMPARE(reread.cellDisplay(reread.sheetIdAt(0), 0, 0), QStringLiteral("x"));
}

QTEST_MAIN(WorkbookInterchangeTest)
#include "tst_workbook_interchange.moc"
