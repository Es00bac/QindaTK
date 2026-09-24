// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook.h"
#include "workbook_numfmt.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
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
QVariantMap look(Workbook &wb, const QString &sheet, const QString &address)
{ const auto p = at(address); return wb.cellPresentation(sheet, p.row, p.column); }
QJsonObject sheetJson(const Workbook &wb, int index)
{ return QJsonDocument::fromJson(wb.serialize()).object().value("sheets").toArray().at(index).toObject(); }
}

class WorkbookFeaturesTest : public QObject {
    Q_OBJECT
private slots:
    void wholeColumnAndRowReferences();
    void namedReferences();
    void numberFormats();
    void formatsAndAlignment();
    void mergedAreas();
    void sheetPropertiesAndProtection();
    void copyAndFillCarryFormats();
    void featuresRoundTrip();
};

void WorkbookFeaturesTest::wholeColumnAndRowReferences()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    const QString data = wb.addSheet(QStringLiteral("Data"));
    QVERIFY(put(wb, s, "A1", "10"));
    QVERIFY(put(wb, s, "A5", "20"));
    QVERIFY(put(wb, s, "A999", "30"));
    QVERIFY(put(wb, s, "B1", "=SUM(a:a)"));
    QVERIFY(put(wb, s, "D2", "=SUM(1:1)"));
    QVERIFY(put(wb, s, "F3", "=SUM($A:a)"));
    QCOMPARE(in(wb, s, "B1"), QStringLiteral("=SUM(A:A)"));
    QCOMPARE(in(wb, s, "F3"), QStringLiteral("=SUM($A:A)"));
    QCOMPARE(out(wb, s, "B1"), QStringLiteral("60"));
    QCOMPARE(out(wb, s, "D2"), QStringLiteral("70"));
    QVERIFY(put(wb, s, "A2", "5")); // the whole-column dependency is indexed
    QCOMPARE(out(wb, s, "B1"), QStringLiteral("65"));
    QCOMPARE(out(wb, s, "D2"), QStringLiteral("75"));
    QVERIFY(put(wb, data, "A1", "7"));
    QVERIFY(put(wb, s, "E2", "=SUM(Data!A:A)*2"));
    QCOMPARE(out(wb, s, "E2"), QStringLiteral("14"));
    QVERIFY(wb.renameSheet(data, QStringLiteral("Raw data")));
    QCOMPARE(in(wb, s, "E2"), QStringLiteral("=SUM('Raw data'!A:A)*2"));

    // Rows inserted above move the row reference but never the column one.
    QVERIFY(wb.insertRows(s, 0, 2));
    QCOMPARE(in(wb, s, "B3"), QStringLiteral("=SUM(A:A)"));
    QCOMPARE(out(wb, s, "B3"), QStringLiteral("65"));
    QCOMPARE(in(wb, s, "D4"), QStringLiteral("=SUM(3:3)"));
    QCOMPARE(out(wb, s, "D4"), QStringLiteral("75"));
    QVERIFY(wb.insertColumns(s, 0, 1));
    QCOMPARE(in(wb, s, "C3"), QStringLiteral("=SUM(B:B)"));
    QCOMPARE(in(wb, s, "E4"), QStringLiteral("=SUM(3:3)"));
    QVERIFY(wb.removeColumns(s, 1, 1));
    QCOMPARE(in(wb, s, "B3"), QStringLiteral("=SUM(#REF!)"));
    QCOMPARE(out(wb, s, "B3"), QStringLiteral("#REF!"));
    QVERIFY(wb.undo());
    QCOMPARE(in(wb, s, "C3"), QStringLiteral("=SUM(B:B)"));
    QCOMPARE(out(wb, s, "C3"), QStringLiteral("65"));

    // Copies move a whole-column reference only sideways, whole rows only down.
    Workbook copy;
    const QString c = copy.sheetIdAt(0);
    QVERIFY(put(copy, c, "A1", "=SUM(B:B)"));
    QVERIFY(put(copy, c, "A2", "=SUM(3:$4)"));
    QVERIFY(copy.copyCells(c, 0, 0, 2, 1, c, 4, 2));
    QCOMPARE(in(copy, c, "C5"), QStringLiteral("=SUM(D:D)"));
    QCOMPARE(in(copy, c, "C6"), QStringLiteral("=SUM($4:7)")); // re-normalized: first <= last
    QVERIFY(put(copy, c, "D1", "=SUM(A:XFD)")); // a whole-sheet width parses; D1 is inside it
    QCOMPARE(copy.cellDisplay(c, 0, 3), QStringLiteral("#CYCLE!"));
}

void WorkbookFeaturesTest::namedReferences()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    const QString other = wb.addSheet(QStringLiteral("Other"));
    QVERIFY(put(wb, s, "A1", "1"));
    QVERIFY(put(wb, s, "A2", "2"));
    QVERIFY(put(wb, s, "A3", "3"));
    QVERIFY(put(wb, s, "B1", "=SUM(Sales)*10")); // defined later: #NAME? until then
    QCOMPARE(out(wb, s, "B1"), QStringLiteral("#NAME?"));
    QSignalSpy namesChanged(&wb, &Workbook::namesChanged);
    QVERIFY(wb.defineName(QStringLiteral("Sales"), s, 0, 0, 2, 0));
    QCOMPARE(namesChanged.size(), 1);
    QCOMPARE(out(wb, s, "B1"), QStringLiteral("60"));
    QVERIFY(wb.defineName(QStringLiteral("Rate"), s, 0, 0, 0, 0));
    QVERIFY(put(wb, s, "B2", "=rate*2"));
    QCOMPARE(in(wb, s, "B2"), QStringLiteral("=Rate*2")); // the defined spelling
    QCOMPARE(out(wb, s, "B2"), QStringLiteral("2"));
    QVERIFY(wb.defineName(QStringLiteral("Sales"), s, 0, 0, 1, 0)); // redefined
    QCOMPARE(out(wb, s, "B1"), QStringLiteral("30"));
    QVERIFY(wb.insertRows(s, 1, 1)); // inside the named area: it grows
    QCOMPARE(wb.names().first().toMap().value("reference").toString(), QStringLiteral("Sheet1!A1"));
    QCOMPARE(wb.names().at(1).toMap().value("reference").toString(), QStringLiteral("Sheet1!A1:A3"));
    QCOMPARE(out(wb, s, "B1"), QStringLiteral("30"));
    QVERIFY(put(wb, s, "A2", "4")); // the inserted row is part of Sales
    QCOMPARE(out(wb, s, "B1"), QStringLiteral("70"));

    QVERIFY(wb.defineName(QStringLiteral("Far"), other, 0, 0, 0, 0));
    QVERIFY(put(wb, other, "A1", "8"));
    QVERIFY(put(wb, s, "C1", "=Far+1"));
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("9"));
    QVERIFY(wb.removeSheet(other));
    QCOMPARE(in(wb, s, "C1"), QStringLiteral("=Far+1"));
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("#REF!"));
    QVERIFY(wb.undo());
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("9"));
    QVERIFY(wb.removeName(QStringLiteral("sales")));
    QCOMPARE(out(wb, s, "B1"), QStringLiteral("#NAME?"));
    QVERIFY(wb.undo());
    QCOMPARE(out(wb, s, "B1"), QStringLiteral("70"));

    QVERIFY(!wb.defineName(QStringLiteral("A1"), s, 0, 0, 0, 0));
    for (const char *bad : {"A1", "xfd10", "R1C1", "r", "TRUE", "sum", "1abc", "a b", ""})
        QVERIFY2(!wb.nameProblem(QLatin1String(bad)).isEmpty(), bad);
    for (const char *good : {"Sales_2024", "a.b", "_total", "ABCD1", "Rx"})
        QVERIFY2(wb.nameProblem(QLatin1String(good)).isEmpty(), good);

    Workbook reopened;
    QVERIFY(reopened.restore(wb.serialize()));
    QCOMPARE(reopened.names(), wb.names());
    QCOMPARE(reopened.cellDisplay(s, 0, 1), QStringLiteral("70"));
    QCOMPARE(reopened.cellDisplay(s, 0, 2), QStringLiteral("9"));
    QCOMPARE(reopened.serialize(), wb.serialize());
}

void WorkbookFeaturesTest::numberFormats()
{
    using Calc::Value;
    const auto f = [](double v, const char *format) { return Calc::formatValue(Value::fromNumber(v), QLatin1String(format)); };
    QCOMPARE(f(1234.5, "#,##0.00"), QStringLiteral("1,234.50"));
    QCOMPARE(f(-1234.5, "$#,##0"), QStringLiteral("-$1,235"));
    QCOMPARE(f(0.125, "0%"), QStringLiteral("13%"));
    QCOMPARE(f(0.125, "0.0%"), QStringLiteral("12.5%"));
    QCOMPARE(f(2.675, "0.00"), QStringLiteral("2.68"));
    QCOMPARE(f(-0.4, "0"), QStringLiteral("0"));
    QCOMPARE(f(12345.678, "0.00E+00"), QStringLiteral("1.23E+04"));
    QCOMPARE(f(0.00012, "0.0E+0"), QStringLiteral("1.2E-4"));
    QCOMPARE(f(0.5, "#.00"), QStringLiteral(".50"));
    QCOMPARE(f(7, "000"), QStringLiteral("007"));
    QCOMPARE(f(1234567, "#,##0 \"units\""), QStringLiteral("1,234,567 units"));
    QCOMPARE(f(45000, "yyyy-mm-dd"), QStringLiteral("2023-03-15"));
    QCOMPARE(f(45000.75, "yyyy-mm-dd hh:mm"), QStringLiteral("2023-03-15 18:00"));
    QCOMPARE(f(0.5, "h:mm AM/PM"), QStringLiteral("12:00 PM"));
    QCOMPARE(f(0.25, "h:mm:ss AM/PM"), QStringLiteral("6:00:00 AM"));
    QCOMPARE(f(45000, "mmm d, yyyy"), QStringLiteral("Mar 15, 2023"));
    QCOMPARE(f(45000, "dddd"), QStringLiteral("Wednesday"));
    QCOMPARE(f(-1, "yyyy"), QStringLiteral("######"));
    QCOMPARE(f(12.5, "General"), QStringLiteral("12.5"));
    QCOMPARE(f(12.5, "@"), QStringLiteral("12.5"));
    QCOMPARE(Calc::formatValue(Value::fromText(QStringLiteral("x")), QStringLiteral("0.00")), QStringLiteral("x"));
    for (const char *bad : {"0.0.0", "abc", "0.0000000000000000", "yyyyy", "\"open", "0E+0000", "%0"})
        QVERIFY2(!Calc::numberFormatProblem(QLatin1String(bad)).isEmpty(), bad);
    for (const char *good : {"", "General", "@", "0", "#,##0.00", "0%", "yyyy-mm-dd", "€0.00", "hh:mm:ss"})
        QVERIFY2(Calc::numberFormatProblem(QString::fromUtf8(good)).isEmpty(), good);
}

void WorkbookFeaturesTest::formatsAndAlignment()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, "A1", "1234.5"));
    QVERIFY(put(wb, s, "A2", "text"));
    QVERIFY(put(wb, s, "A3", "TRUE"));
    QVERIFY(put(wb, s, "A4", "=1/0"));
    QCOMPARE(look(wb, s, "A1").value("horizontalAlignment").toString(), QStringLiteral("right"));
    QCOMPARE(look(wb, s, "A2").value("horizontalAlignment").toString(), QStringLiteral("left"));
    QCOMPARE(look(wb, s, "A3").value("horizontalAlignment").toString(), QStringLiteral("center"));
    QCOMPARE(look(wb, s, "A4").value("error").toBool(), true);
    QCOMPARE(look(wb, s, "A1").value("numeric").toBool(), true); // views show ### rather than cut it
    QVERIFY(!look(wb, s, "A2").contains("numeric"));
    QVERIFY(wb.applyStyle(s, 0, 0, 0, 0, {{"numberFormat", "#,##0.00"}}));
    QCOMPARE(out(wb, s, "A1"), QStringLiteral("1,234.50"));
    QCOMPARE(in(wb, s, "A1"), QStringLiteral("1234.5")); // the input is never formatted
    QCOMPARE(wb.cellInfo(s, 0, 0).value("display").toString(), QStringLiteral("1,234.50"));
    QVERIFY(wb.applyStyle(s, 1, 0, 1, 0, {{"horizontalAlignment", "center"}, {"bold", true}, {"fillColor", "#FC0"}}));
    QVariantMap style = look(wb, s, "A2");
    QCOMPARE(style.value("horizontalAlignment").toString(), QStringLiteral("center"));
    QCOMPARE(style.value("bold").toBool(), true);
    QCOMPARE(style.value("fillColor").toString(), QStringLiteral("#ffcc00"));
    QCOMPARE(wb.cellFormat(s, 1, 0), (QVariantMap{{"horizontalAlignment", "center"}, {"bold", true}, {"fillColor", "#ffcc00"}}));

    const int layers = sheetJson(wb, 0).value("formats").toArray().size();
    QVERIFY(wb.applyStyle(s, 0, 0, 3, 1, {{"italic", true}}));
    QVERIFY(wb.applyStyle(s, 0, 0, 3, 1, {{"italic", false}})); // replaces the dead layer
    QCOMPARE(sheetJson(wb, 0).value("formats").toArray().size(), layers + 1);
    QVERIFY(!look(wb, s, "A2").contains("italic"));

    QVERIFY(!wb.applyStyle(s, 0, 0, 0, 0, {{"horizontalAlignment", "justify"}}));
    QVERIFY(!wb.applyStyle(s, 0, 0, 0, 0, {{"textColor", "red"}}));
    QVERIFY(!wb.applyStyle(s, 0, 0, 0, 0, {{"numberFormat", "0.0.0"}}));
    QVERIFY(!wb.applyStyle(s, 0, 0, 0, 0, {{"shadow", true}}));
    QVERIFY(!wb.applyStyle(s, 0, 0, 5000, 0, {{"bold", true}}));

    QVERIFY(wb.clearFormats(s, 1, 0, 1, 0));
    QCOMPARE(look(wb, s, "A2").value("horizontalAlignment").toString(), QStringLiteral("left"));
    QVERIFY(!look(wb, s, "A2").contains("bold"));
    QCOMPARE(out(wb, s, "A1"), QStringLiteral("1,234.50")); // other cells keep their formats
    QVERIFY(wb.undo());
    QCOMPARE(look(wb, s, "A2").value("bold").toBool(), true);
    QVERIFY(wb.redo());
    QVERIFY(!look(wb, s, "A2").contains("bold"));

    QVERIFY(wb.insertRows(s, 0, 1)); // formats follow their cells
    QCOMPARE(out(wb, s, "A2"), QStringLiteral("1,234.50"));
    QVERIFY(wb.removeRows(s, 1, 1));
    QVERIFY(wb.applyStyle(s, 0, 0, 0, 0, {{"numberFormat", "0.0"}}));
    QVERIFY(put(wb, s, "A1", "2"));
    QCOMPARE(out(wb, s, "A1"), QStringLiteral("2.0"));
}

void WorkbookFeaturesTest::mergedAreas()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, "A1", "keep"));
    QVERIFY(put(wb, s, "B1", "gone"));
    QVERIFY(put(wb, s, "A2", "gone too"));
    QVERIFY(wb.mergeCells(s, 0, 0, 1, 1));
    QCOMPARE(in(wb, s, "A1"), QStringLiteral("keep"));
    QCOMPARE(in(wb, s, "B1"), QString());
    QCOMPARE(in(wb, s, "A2"), QString());
    QCOMPARE(wb.mergedAreas(s, 0, 0, 5, 5).size(), 1);
    QCOMPARE(wb.mergeAt(s, 1, 1).value("range").toString(), QStringLiteral("A1:B2"));
    QVERIFY(wb.mergeAt(s, 2, 2).isEmpty());
    QCOMPARE(wb.undoText(), QStringLiteral("Merge cells"));
    QVERIFY(wb.undo()); // one step restores the cleared inputs and removes the area
    QCOMPARE(in(wb, s, "B1"), QStringLiteral("gone"));
    QVERIFY(wb.mergedAreas(s, 0, 0, 5, 5).isEmpty());
    QVERIFY(wb.redo());
    QVERIFY(wb.mergeCells(s, 1, 1, 2, 2)); // overlaps: replaces A1:B2
    QCOMPARE(wb.mergedAreas(s, 0, 0, 9, 9).size(), 1);
    QCOMPARE(wb.mergeAt(s, 2, 2).value("range").toString(), QStringLiteral("B2:C3"));
    QVERIFY(!wb.mergeCells(s, 4, 4, 4, 4));
    QVERIFY(wb.insertRows(s, 2, 1)); // inside the area: it grows
    QCOMPARE(wb.mergeAt(s, 1, 1).value("range").toString(), QStringLiteral("B2:C4"));
    QVERIFY(wb.removeRows(s, 1, 3)); // the whole area is deleted
    QVERIFY(wb.mergedAreas(s, 0, 0, 9, 9).isEmpty());
    QVERIFY(wb.undo());
    QVERIFY(wb.unmergeCells(s, 3, 2, 3, 2));
    QVERIFY(wb.mergedAreas(s, 0, 0, 9, 9).isEmpty());
}

void WorkbookFeaturesTest::sheetPropertiesAndProtection()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QSignalSpy properties(&wb, &Workbook::sheetPropertiesChanged);
    QVERIFY(wb.setSheetProperties(s, {{"tabColor", "#FF0000"}, {"frozenRows", 1}, {"frozenColumns", 2}}));
    QCOMPARE(properties.size(), 1);
    QCOMPARE(properties.first().first().toString(), s);
    QCOMPARE(wb.sheetProperties(s).value("tabColor").toString(), QStringLiteral("#ff0000"));
    QCOMPARE(wb.sheetProperties(s).value("frozenColumns").toInt(), 2);
    QCOMPARE(wb.sheets().first().toMap().value("tabColor").toString(), QStringLiteral("#ff0000"));
    QVERIFY(!wb.setSheetProperties(s, {{"hidden", true}})); // the only visible sheet
    QVERIFY(!wb.setSheetProperties(s, {{"frozenRows", 5000}}));
    QVERIFY(!wb.setSheetProperties(s, {{"zoom", 2}}));
    const QString two = wb.addSheet(QStringLiteral("Two"));
    QVERIFY(wb.setSheetProperties(s, {{"hidden", true}}));
    QCOMPARE(wb.sheets().first().toMap().value("hidden").toBool(), true);
    QVERIFY(!wb.removeSheet(two)); // would leave no visible sheet
    QVERIFY(wb.undo());
    QCOMPARE(wb.sheetProperties(s).value("hidden").toBool(), false);

    QVERIFY(wb.setSheetProperties(s, {{"protected", true}}));
    QVERIFY(!wb.setCellInput(s, 0, 0, QStringLiteral("1")));
    QVERIFY(wb.lastError().contains(QStringLiteral("protected")));
    QVERIFY(!wb.applyStyle(s, 0, 0, 0, 0, {{"bold", true}}));
    QVERIFY(!wb.insertRows(s, 0, 1));
    QVERIFY(!wb.mergeCells(s, 0, 0, 1, 1));
    QVERIFY(!wb.pasteInputs(s, 0, 0, {QVariant(QVariantList{"1"})}));
    QVERIFY(!wb.setColumnWidth(s, 0, 50));
    QVERIFY(wb.setSheetProperties(s, {{"tabColor", "#00ff00"}})); // properties stay editable
    QVERIFY(wb.setSheetProperties(s, {{"protected", false}}));
    QVERIFY(wb.setCellInput(s, 0, 0, QStringLiteral("1")));

    QVERIFY(wb.setColumnWidth(s, 2, 120));
    QCOMPARE(wb.columnWidths(s), (QVariantMap{{"2", 120.0}}));
    QVERIFY(wb.insertColumns(s, 0, 1));
    QCOMPARE(wb.columnWidths(s), (QVariantMap{{"3", 120.0}}));
    QVERIFY(wb.setColumnWidth(s, 3, 0));
    QVERIFY(wb.columnWidths(s).isEmpty());
    QVERIFY(wb.setRowHeight(s, 0, 30.5));
    QCOMPARE(wb.rowHeights(s), (QVariantMap{{"0", 30.5}}));
    QVERIFY(!wb.setRowHeight(s, 5000, 30));
}

void WorkbookFeaturesTest::copyAndFillCarryFormats()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, "A1", "1"));
    QVERIFY(wb.applyStyle(s, 0, 0, 0, 0, {{"bold", true}, {"numberFormat", "0.00"}}));
    QVERIFY(wb.fill(s, 0, 0, 1, 1, 1, 0, 3, 1));
    QCOMPARE(out(wb, s, "A4"), QStringLiteral("1.00"));
    QCOMPARE(look(wb, s, "A4").value("bold").toBool(), true);
    QVERIFY(wb.copyCells(s, 0, 0, 1, 1, s, 0, 2));
    QCOMPARE(out(wb, s, "C1"), QStringLiteral("1.00"));
    QVERIFY(wb.undo());
    QCOMPARE(out(wb, s, "C1"), QString());
    QVERIFY(!look(wb, s, "C1").contains("bold"));

    // A two-row block alternates its formats over the target.
    QVERIFY(put(wb, s, "B10", "x"));
    QVERIFY(put(wb, s, "B11", "y"));
    QVERIFY(wb.applyStyle(s, 9, 1, 9, 1, {{"bold", true}}));
    QVERIFY(wb.applyStyle(s, 10, 1, 10, 1, {{"italic", true}}));
    QVERIFY(wb.fill(s, 9, 1, 2, 1, 11, 1, 4, 1));
    QCOMPARE(look(wb, s, "B12").value("bold").toBool(), true);
    QCOMPARE(look(wb, s, "B13").value("italic").toBool(), true);
    QCOMPARE(look(wb, s, "B14").value("bold").toBool(), true);
    QVERIFY(!look(wb, s, "B15").contains("bold"));
    QCOMPARE(in(wb, s, "B15"), QStringLiteral("y"));

    // A paste carries merged areas inside its source.
    QVERIFY(wb.mergeCells(s, 0, 3, 0, 4));
    QVERIFY(wb.copyCells(s, 0, 3, 1, 2, s, 5, 3));
    QCOMPARE(wb.mergeAt(s, 5, 4).value("range").toString(), QStringLiteral("D6:E6"));

    // A long fill stays one layer per source layer.
    QVERIFY(wb.fill(s, 0, 0, 1, 1, 1, 0, 900, 1));
    QVERIFY(sheetJson(wb, 0).value("formats").toArray().size() < 12);
    QCOMPARE(look(wb, s, "A900").value("bold").toBool(), true);
}

void WorkbookFeaturesTest::featuresRoundTrip()
{
    Workbook plain;
    QVERIFY(put(plain, plain.sheetIdAt(0), "A1", "1"));
    QVERIFY(plain.serialize().contains("\"version\":1")); // no features: still version 1

    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, "A1", "0.5"));
    QVERIFY(wb.applyStyle(s, 0, 0, 2, 1, {{"numberFormat", "0%"}, {"textColor", "#112233"}}));
    QVERIFY(wb.mergeCells(s, 4, 0, 5, 2));
    QVERIFY(wb.setSheetProperties(s, {{"tabColor", "#abcdef"}, {"frozenRows", 2}, {"protected", true}}));
    QVERIFY(wb.setSheetProperties(s, {{"protected", false}}));
    QVERIFY(wb.setColumnWidth(s, 25, 140));
    QVERIFY(wb.setRowHeight(s, 9, 40));
    QVERIFY(wb.defineName(QStringLiteral("Rate"), s, 0, 0, 0, 0));
    const QByteArray bytes = wb.serialize();
    QVERIFY(bytes.contains("\"version\":2"));
    QVERIFY(bytes.contains("\"columnWidths\":{\"Z\":140}"));
    QVERIFY(bytes.contains("\"rowHeights\":{\"10\":40}"));
    Workbook reopened;
    QVERIFY2(reopened.restore(bytes), qPrintable(reopened.lastError()));
    QCOMPARE(reopened.cellDisplay(s, 0, 0), QStringLiteral("50%"));
    QCOMPARE(reopened.cellPresentation(s, 0, 0), wb.cellPresentation(s, 0, 0));
    QCOMPARE(reopened.sheetProperties(s), wb.sheetProperties(s));
    QCOMPARE(reopened.mergedAreas(s, 0, 0, 20, 20), wb.mergedAreas(s, 0, 0, 20, 20));
    QCOMPARE(reopened.columnWidths(s), wb.columnWidths(s));
    QCOMPARE(reopened.serialize(), bytes);

    // Invalid feature data refuses the whole file.
    QVERIFY(!reopened.restore(QByteArray(bytes).replace("\"A5:C6\"", "\"A5:C6\",\"B5:B5\"")));
    QVERIFY(!reopened.restore(QByteArray(bytes).replace("#abcdef", "#abcdeg")));
    QVERIFY(!reopened.restore(QByteArray(bytes).replace("\"frozenRows\":2", "\"frozenRows\":2000")));
    QVERIFY(!reopened.restore(QByteArray(bytes).replace("\"numberFormat\":\"0%\"", "\"numberFormat\":\"0.0.0\"")));
    QVERIFY(!reopened.restore(QByteArray(bytes).replace("Sheet1!A1", "Nowhere!A1")));
    QCOMPARE(reopened.serialize(), bytes); // failed restores leave the document alone
}

QTEST_GUILESS_MAIN(WorkbookFeaturesTest)
#include "tst_workbook_features.moc"
