// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook.h"
#include <QtTest>

using namespace QindaTK;

namespace {
Calc::RefPoint at(const QString &address)
{
    Calc::RefPoint p;
    if (!Calc::parseCellAddress(address, &p)) qFatal("bad address %s", qPrintable(address));
    return p;
}
bool put(Workbook &wb, const QString &sheet, const QString &address, const QString &input)
{
    const auto p = at(address);
    const bool ok = wb.setCellInput(sheet, p.row, p.column, input);
    if (!ok) qWarning() << address << input << wb.lastError();
    return ok;
}
QString in(Workbook &wb, const QString &sheet, const QString &address)
{ const auto p = at(address); return wb.cellInput(sheet, p.row, p.column); }
QString out(Workbook &wb, const QString &sheet, const QString &address)
{ const auto p = at(address); return wb.cellDisplay(sheet, p.row, p.column); }
QString type(Workbook &wb, const QString &sheet, const QString &address)
{ const auto p = at(address); return wb.cellType(sheet, p.row, p.column); }
qint64 stat(Workbook &wb, const char *key) { return wb.statistics().value(QLatin1String(key)).toLongLong(); }
}

class WorkbookModelTest : public QObject {
    Q_OBJECT
private slots:
    void typedInputs();
    void mixedAbsoluteRelativeFill();
    void crossSheetReferencesSurviveRenameReorder();
    void dependencyFanOutIsIncremental();
    void cyclesAreDetectedAndPropagate();
    void parseSpansAndReferenceSpans();
    void rowInsertionAndDeletion();
    void columnEditsAndLimits();
};

void WorkbookModelTest::typedInputs()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QCOMPARE(s, QStringLiteral("sheet-1"));
    QCOMPARE(wb.sheetName(s), QStringLiteral("Sheet1"));
    QVERIFY(put(wb, s, "A1", "12"));
    QVERIFY(put(wb, s, "A2", "'12"));
    QVERIFY(put(wb, s, "A3", "true"));
    QVERIFY(put(wb, s, "A4", "#n/a"));
    QVERIFY(put(wb, s, "A5", "hello"));
    QVERIFY(put(wb, s, "A6", "=1+1"));
    QVERIFY(put(wb, s, "A7", "'=not a formula"));
    QVERIFY(put(wb, s, "A8", "1.50"));
    QCOMPARE(type(wb, s, "A1"), QStringLiteral("number"));
    QCOMPARE(in(wb, s, "A1"), QStringLiteral("12"));
    QCOMPARE(type(wb, s, "A2"), QStringLiteral("text"));
    QCOMPARE(out(wb, s, "A2"), QStringLiteral("12"));
    QCOMPARE(in(wb, s, "A2"), QStringLiteral("'12"));
    QCOMPARE(type(wb, s, "A3"), QStringLiteral("boolean"));
    QCOMPARE(in(wb, s, "A3"), QStringLiteral("TRUE"));
    QCOMPARE(type(wb, s, "A4"), QStringLiteral("error"));
    QCOMPARE(in(wb, s, "A4"), QStringLiteral("#N/A"));
    QCOMPARE(out(wb, s, "A5"), QStringLiteral("hello"));
    QCOMPARE(out(wb, s, "A6"), QStringLiteral("2"));
    QCOMPARE(in(wb, s, "A6"), QStringLiteral("=1+1"));
    QVERIFY(wb.cellInfo(s, 5, 0).value("formula").toBool());
    QCOMPARE(out(wb, s, "A7"), QStringLiteral("=not a formula"));
    QCOMPARE(in(wb, s, "A7"), QStringLiteral("'=not a formula"));
    QCOMPARE(in(wb, s, "A8"), QStringLiteral("1.5"));
    QCOMPARE(wb.cellValue(s, 7, 0).toDouble(), 1.5);

    const QString revision = wb.revision();
    const qint64 steps = stat(wb, "historySteps");
    QVERIFY(!wb.setCellInput(s, 0, 8, QStringLiteral("=")));
    QCOMPARE(wb.lastErrorDetail().value("address").toString(), QStringLiteral("I1"));
    QCOMPARE(wb.lastErrorDetail().value("start").toInt(), 1);
    QCOMPARE(wb.revision(), revision);
    QCOMPARE(stat(wb, "historySteps"), steps);
    QCOMPARE(type(wb, s, "I1"), QStringLiteral("blank"));
    QVERIFY(!wb.setCellInput(s, 5000, 0, QStringLiteral("1")));
    QVERIFY(put(wb, s, "A1", ""));
    QCOMPARE(type(wb, s, "A1"), QStringLiteral("blank"));
    QCOMPARE(stat(wb, "cells"), 7);
    // Re-entering an equal input is not a change and records no history.
    const qint64 before = stat(wb, "historySteps");
    QVERIFY(put(wb, s, "A6", "= 1 + 1"));
    QCOMPARE(stat(wb, "historySteps"), before);
}

void WorkbookModelTest::mixedAbsoluteRelativeFill()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, "A1", "1") && put(wb, s, "A2", "2") && put(wb, s, "A3", "3"));
    QVERIFY(put(wb, s, "B1", "10"));
    QVERIFY(put(wb, s, "C1", "=A1*$B$1+A$1+$A1"));
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("12"));
    QVERIFY(wb.fill(s, 0, 2, 1, 1, 1, 2, 2, 1)); // C1 down into C2:C3
    QCOMPARE(in(wb, s, "C2"), QStringLiteral("=A2*$B$1+A$1+$A2"));
    QCOMPARE(out(wb, s, "C2"), QStringLiteral("23"));
    QCOMPARE(in(wb, s, "C3"), QStringLiteral("=A3*$B$1+A$1+$A3"));
    QCOMPARE(out(wb, s, "C3"), QStringLiteral("34"));
    QCOMPARE(wb.undoText(), QStringLiteral("Fill cells"));
    QVERIFY(wb.fill(s, 0, 2, 1, 1, 0, 3, 1, 2)); // C1 right into D1:E1
    QCOMPARE(in(wb, s, "D1"), QStringLiteral("=B1*$B$1+B$1+$A1"));
    QCOMPARE(out(wb, s, "D1"), QStringLiteral("111"));
    QCOMPARE(in(wb, s, "E1"), QStringLiteral("=C1*$B$1+C$1+$A1"));
    QCOMPARE(out(wb, s, "E1"), QStringLiteral("133"));
    // A two-row pattern repeats aligned to its origin.
    QVERIFY(put(wb, s, "A10", "x") && put(wb, s, "A11", "=A10&\"y\""));
    QVERIFY(wb.fill(s, 9, 0, 2, 1, 11, 0, 5, 1));
    QCOMPARE(out(wb, s, "A12"), QStringLiteral("x"));
    QCOMPARE(in(wb, s, "A13"), QStringLiteral("=A12&\"y\""));
    QCOMPARE(out(wb, s, "A13"), QStringLiteral("xy"));
    QCOMPARE(out(wb, s, "A16"), QStringLiteral("x"));
    // Filling upward past row 1 invalidates the moved relative reference.
    QVERIFY(put(wb, s, "G2", "=G1*2"));
    QVERIFY(wb.fill(s, 1, 6, 1, 1, 0, 6, 1, 1));
    QCOMPARE(in(wb, s, "G1"), QStringLiteral("=#REF!*2"));
    QCOMPARE(out(wb, s, "G1"), QStringLiteral("#REF!"));
    QVERIFY(wb.undo());
    QCOMPARE(type(wb, s, "G1"), QStringLiteral("blank"));
    QVERIFY(!wb.fill(s, 0, 0, 1, 1, 999, 0, 2, 1)); // outside the sheet
}

void WorkbookModelTest::crossSheetReferencesSurviveRenameReorder()
{
    Workbook wb;
    const QString data = wb.sheetIdAt(0);
    QVERIFY(wb.renameSheet(data, QStringLiteral("Data")));
    const QString summary = wb.addSheet(QStringLiteral("Summary"));
    QCOMPARE(summary, QStringLiteral("sheet-2"));
    QVERIFY(put(wb, data, "A1", "5") && put(wb, data, "A2", "1"));
    QVERIFY(put(wb, summary, "A1", "=data!a1*2"));
    QVERIFY(put(wb, summary, "A2", "=SUM(Data!A1:A3)"));
    QCOMPARE(in(wb, summary, "A1"), QStringLiteral("=Data!A1*2"));
    QCOMPARE(out(wb, summary, "A1"), QStringLiteral("10"));
    QVERIFY(wb.renameSheet(data, QStringLiteral("Input Data")));
    QCOMPARE(stat(wb, "lastEvaluated"), 0);
    QCOMPARE(in(wb, summary, "A1"), QStringLiteral("='Input Data'!A1*2"));
    QCOMPARE(out(wb, summary, "A1"), QStringLiteral("10"));
    QVERIFY(wb.moveSheet(summary, 0));
    QCOMPARE(wb.sheetIdAt(0), summary);
    QCOMPARE(in(wb, summary, "A2"), QStringLiteral("=SUM('Input Data'!A1:A3)"));
    QVERIFY(put(wb, data, "A1", "7"));
    QCOMPARE(stat(wb, "lastEvaluated"), 2);
    QCOMPARE(out(wb, summary, "A1"), QStringLiteral("14"));
    QCOMPARE(out(wb, summary, "A2"), QStringLiteral("8"));
    QVERIFY(!wb.setCellInput(summary, 2, 0, QStringLiteral("=Data!A1")));
    QVERIFY(wb.lastError().contains(QStringLiteral("Unknown sheet 'Data'")));
    QVERIFY(!wb.renameSheet(summary, QStringLiteral("input data"))); // case-insensitive duplicate
    QVERIFY(!wb.renameSheet(summary, QStringLiteral("bad/name")));

    QVERIFY(wb.undo()); // edit
    QCOMPARE(out(wb, summary, "A1"), QStringLiteral("10"));
    QVERIFY(wb.undo()); // move
    QCOMPARE(wb.sheetIdAt(0), data);
    QVERIFY(wb.undo()); // rename
    QCOMPARE(in(wb, summary, "A1"), QStringLiteral("=Data!A1*2"));
    QVERIFY(wb.redo() && wb.redo() && wb.redo());
    QCOMPARE(in(wb, summary, "A1"), QStringLiteral("='Input Data'!A1*2"));
    QCOMPARE(out(wb, summary, "A1"), QStringLiteral("14"));

    QVERIFY(wb.removeSheet(data));
    QCOMPARE(wb.sheetCount(), 1);
    QCOMPARE(in(wb, summary, "A1"), QStringLiteral("=#REF!*2"));
    QCOMPARE(out(wb, summary, "A1"), QStringLiteral("#REF!"));
    QCOMPARE(in(wb, summary, "A2"), QStringLiteral("=SUM(#REF!)"));
    QVERIFY(!wb.removeSheet(summary)); // a workbook keeps one sheet
    QVERIFY(wb.undo());
    QCOMPARE(wb.sheetIdAt(1), data);
    QCOMPARE(wb.sheetName(data), QStringLiteral("Input Data"));
    QCOMPARE(in(wb, summary, "A1"), QStringLiteral("='Input Data'!A1*2"));
    QCOMPARE(out(wb, summary, "A1"), QStringLiteral("14"));
    QCOMPARE(out(wb, data, "A2"), QStringLiteral("1"));
    QCOMPARE(wb.addSheet(), QStringLiteral("sheet-3")); // IDs are never reused
    QCOMPARE(wb.sheetName(QStringLiteral("sheet-3")), QStringLiteral("Sheet1"));
}

void WorkbookModelTest::dependencyFanOutIsIncremental()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, "A1", "1") && put(wb, s, "Z1", "5"));
    QVariantList rows;
    for (int i = 1; i <= 1000; ++i) rows.append(QVariant(QVariantList{QStringLiteral("=$A$1+%1").arg(i)}));
    QVERIFY(wb.pasteInputs(s, 0, 1, rows));
    QCOMPARE(stat(wb, "lastEvaluated"), 1000);
    QVERIFY(put(wb, s, "C1", "=SUM(B1:B1000)"));
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("501500"));
    QVERIFY(put(wb, s, "A1", "2"));
    QCOMPARE(stat(wb, "lastDirty"), 1001);
    QCOMPARE(stat(wb, "lastEvaluated"), 1001);
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("502500"));
    QVERIFY(put(wb, s, "Z1", "6"));
    QCOMPARE(stat(wb, "lastEvaluated"), 0);
    QVERIFY(put(wb, s, "B500", "=$A$1"));
    QCOMPARE(stat(wb, "lastEvaluated"), 2);
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("502000"));
    QCOMPARE(stat(wb, "dependencyCells"), 1);
    QCOMPARE(stat(wb, "dependencyRanges"), 1);
    QVERIFY(wb.undo());
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("502500"));
    QCOMPARE(stat(wb, "lastEvaluated"), 2);
}

void WorkbookModelTest::cyclesAreDetectedAndPropagate()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, "A1", "=B1+1"));
    QCOMPARE(out(wb, s, "A1"), QStringLiteral("1"));
    QVERIFY(put(wb, s, "B1", "=A1+1"));
    QCOMPARE(stat(wb, "lastCycleCells"), 2);
    QCOMPARE(out(wb, s, "A1"), QStringLiteral("#CYCLE!"));
    QCOMPARE(out(wb, s, "B1"), QStringLiteral("#CYCLE!"));
    QVERIFY(put(wb, s, "C1", "=A1*2"));
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("#CYCLE!"));
    QCOMPARE(stat(wb, "lastCycleCells"), 0); // C1 depends on the cycle but is not in it
    QVERIFY(put(wb, s, "D1", "=IFERROR(A1,99)"));
    QCOMPARE(out(wb, s, "D1"), QStringLiteral("99"));
    QVERIFY(put(wb, s, "E1", "=E1"));
    QCOMPARE(out(wb, s, "E1"), QStringLiteral("#CYCLE!"));
    QVERIFY(put(wb, s, "F10", "=SUM(F1:F10)"));
    QCOMPARE(out(wb, s, "F10"), QStringLiteral("#CYCLE!"));
    QVERIFY(put(wb, s, "B1", "5"));
    QCOMPARE(out(wb, s, "A1"), QStringLiteral("6"));
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("12"));
    QCOMPARE(out(wb, s, "D1"), QStringLiteral("6"));
    QCOMPARE(stat(wb, "lastCycleCells"), 0);
    QVERIFY(wb.undo());
    QCOMPARE(out(wb, s, "A1"), QStringLiteral("#CYCLE!"));
    QCOMPARE(out(wb, s, "D1"), QStringLiteral("99"));
    const QString other = wb.addSheet(QStringLiteral("Other"));
    QVERIFY(put(wb, other, "A1", "=Sheet1!G1+1"));
    QVERIFY(put(wb, s, "G1", "=Other!A1"));
    QCOMPARE(out(wb, s, "G1"), QStringLiteral("#CYCLE!"));
    QCOMPARE(out(wb, other, "A1"), QStringLiteral("#CYCLE!"));
    QVERIFY(wb.undo());
    QCOMPARE(out(wb, other, "A1"), QStringLiteral("1"));
}

void WorkbookModelTest::parseSpansAndReferenceSpans()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    const QString s2 = wb.addSheet(QStringLiteral("Sheet2"));
    const QString revision = wb.revision();
    QVERIFY(!wb.setCellInput(s, 0, 0, QStringLiteral("=SUM(A1,,2)")));
    QCOMPARE(wb.lastErrorDetail().value("address").toString(), QStringLiteral("A1"));
    QCOMPARE(wb.lastErrorDetail().value("start").toInt(), 8);
    QCOMPARE(wb.lastErrorDetail().value("end").toInt(), 9);
    QCOMPARE(wb.revision(), revision);
    const QVariantMap check = wb.checkInput(s, QStringLiteral("=a1+sheet2!C3:B2"));
    QVERIFY(check.value("ok").toBool());
    QCOMPARE(check.value("kind").toString(), QStringLiteral("formula"));
    QCOMPARE(check.value("canonical").toString(), QStringLiteral("=A1+Sheet2!B2:C3"));
    const QVariantList refs = check.value("references").toList();
    QCOMPARE(refs.size(), 2);
    QCOMPARE(refs[0].toMap().value("start").toInt(), 1);
    QCOMPARE(refs[0].toMap().value("end").toInt(), 3);
    QCOMPARE(refs[0].toMap().value("sheetId").toString(), s);
    QCOMPARE(refs[1].toMap().value("text").toString(), QStringLiteral("sheet2!C3:B2"));
    QCOMPARE(refs[1].toMap().value("sheetId").toString(), s2);
    QCOMPARE(refs[1].toMap().value("firstRow").toInt(), 1);
    QCOMPARE(refs[1].toMap().value("lastColumn").toInt(), 2);
    QCOMPARE(wb.checkInput(s, QStringLiteral("'=A1")).value("kind").toString(), QStringLiteral("text"));
    const QVariantMap bad = wb.checkInput(s, QStringLiteral("=1+"));
    QVERIFY(!bad.value("ok").toBool());
    QCOMPARE(bad.value("start").toInt(), 3);
    QCOMPARE(bad.value("end").toInt(), 4);
}

void WorkbookModelTest::rowInsertionAndDeletion()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    const QString s2 = wb.addSheet(QStringLiteral("Sheet2"));
    for (int i = 1; i <= 6; ++i) QVERIFY(put(wb, s, QStringLiteral("A%1").arg(i), QString::number(i)));
    QVERIFY(put(wb, s, "C1", "=SUM(A1:A6)") && put(wb, s, "C2", "=A4*10") && put(wb, s, "C8", "=$A$6"));
    QVERIFY(put(wb, s2, "A1", "=Sheet1!A5"));
    const QByteArray base = wb.serialize();

    QVERIFY(wb.insertRows(s, 2, 2));
    QCOMPARE(stat(wb, "lastEvaluated"), 0); // pure shifts keep cached values
    QCOMPARE(wb.rowCount(s), 1002);
    QCOMPARE(in(wb, s, "C1"), QStringLiteral("=SUM(A1:A8)"));
    QCOMPARE(in(wb, s, "C2"), QStringLiteral("=A6*10"));
    QCOMPARE(in(wb, s, "C10"), QStringLiteral("=$A$8"));
    QCOMPARE(in(wb, s2, "A1"), QStringLiteral("=Sheet1!A7"));
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("21"));
    QCOMPARE(out(wb, s, "C2"), QStringLiteral("40"));
    QCOMPARE(out(wb, s2, "A1"), QStringLiteral("5"));
    const QByteArray inserted = wb.serialize();
    QVERIFY(put(wb, s, "A3", "100"));
    QCOMPARE(stat(wb, "lastEvaluated"), 1);
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("121"));
    QVERIFY(wb.undo() && wb.undo());
    QCOMPARE(wb.serialize(), base);
    QCOMPARE(out(wb, s, "C8"), QStringLiteral("6"));
    QVERIFY(wb.redo());
    QCOMPARE(wb.serialize(), inserted);
    QVERIFY(wb.undo());

    QVERIFY(wb.removeRows(s, 3, 2)); // rows 4 and 5
    QCOMPARE(stat(wb, "lastEvaluated"), 3);
    QCOMPARE(in(wb, s, "C1"), QStringLiteral("=SUM(A1:A4)"));
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("12"));
    QCOMPARE(in(wb, s, "C2"), QStringLiteral("=#REF!*10"));
    QCOMPARE(out(wb, s, "C2"), QStringLiteral("#REF!"));
    QCOMPARE(in(wb, s, "C6"), QStringLiteral("=$A$4"));
    QCOMPARE(out(wb, s, "C6"), QStringLiteral("6"));
    QCOMPARE(in(wb, s2, "A1"), QStringLiteral("=#REF!"));
    QVERIFY(wb.undo());
    QCOMPARE(wb.serialize(), base);
    QCOMPARE(out(wb, s, "C2"), QStringLiteral("40"));
    QCOMPARE(out(wb, s2, "A1"), QStringLiteral("5"));

    QVERIFY(put(wb, s, "D1", "=SUM(A2:A3)"));
    QVERIFY(wb.removeRows(s, 1, 2));
    QCOMPARE(in(wb, s, "D1"), QStringLiteral("=SUM(#REF!)"));
    QVERIFY(wb.undo());
    QCOMPARE(out(wb, s, "D1"), QStringLiteral("5"));
    QVERIFY(!wb.removeRows(s, 0, wb.rowCount(s)));
    QVERIFY(!wb.insertRows(s, 0, Calc::MaxRows));
    QVERIFY(!wb.insertRows(s, wb.rowCount(s) + 1, 1));
}

void WorkbookModelTest::columnEditsAndLimits()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, "A1", "1") && put(wb, s, "B1", "7") && put(wb, s, "E1", "=A1+B1"));
    QVERIFY(wb.insertColumns(s, 1, 1));
    QCOMPARE(in(wb, s, "F1"), QStringLiteral("=A1+C1"));
    QCOMPARE(out(wb, s, "F1"), QStringLiteral("8"));
    QCOMPARE(wb.columnCount(s), 27);
    QVERIFY(wb.removeColumns(s, 0, 1));
    QCOMPARE(in(wb, s, "E1"), QStringLiteral("=#REF!+B1"));
    QCOMPARE(out(wb, s, "E1"), QStringLiteral("#REF!"));
    QVERIFY(wb.undo() && wb.undo());
    QCOMPARE(in(wb, s, "E1"), QStringLiteral("=A1+B1"));
    QCOMPARE(out(wb, s, "E1"), QStringLiteral("8"));
    QCOMPARE(wb.columnCount(s), 26);
    QVERIFY(!wb.insertColumns(s, 0, Calc::MaxColumns));
    QVERIFY(!wb.removeColumns(s, 20, 10));
}

QTEST_GUILESS_MAIN(WorkbookModelTest)
#include "tst_workbook_model.moc"
