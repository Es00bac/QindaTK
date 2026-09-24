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
    return wb.setCellInput(sheet, p.row, p.column, input);
}
QString in(Workbook &wb, const QString &sheet, const QString &address)
{ const auto p = at(address); return wb.cellInput(sheet, p.row, p.column); }
QVariantMap look(Workbook &wb, const QString &sheet, const QString &address)
{ const auto p = at(address); return wb.cellPresentation(sheet, p.row, p.column); }
QVariantList column(const QString &a, const QString &b, const QString &c)
{ return {QVariant(QVariantList{a}), QVariant(QVariantList{b}), QVariant(QVariantList{c})}; }
}

class WorkbookRulesTest : public QObject {
    Q_OBJECT
private slots:
    void conditionalPredicatesAndPriority();
    void scalesBarsAndIcons();
    void conditionalPreviewEqualsCommittedAndReopened();
    void conditionalRulesFollowStructure();
    void validationOnEntry();
    void validationListsAndCustomRules();
    void pastePolicies();
    void validationOwnershipAndPersistence();
};

void WorkbookRulesTest::conditionalPredicatesAndPriority()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    for (int i = 1; i <= 5; ++i) QVERIFY(put(wb, s, QStringLiteral("A%1").arg(i), QString::number(i * 10)));
    const QString big = wb.setConditionalFormat(s, {{"ranges", QStringList{"A1:A5"}}, {"type", "cellValue"},
                                                    {"operator", "greater"}, {"value1", "25"},
                                                    {"style", QVariantMap{{"bold", true}, {"fillColor", "#ff0000"}}}});
    QCOMPARE(big, QStringLiteral("cf1"));
    QCOMPARE(look(wb, s, "A3").value("bold").toBool(), true);
    QCOMPARE(look(wb, s, "A3").value("fillColor").toString(), QStringLiteral("#ff0000"));
    QVERIFY(!look(wb, s, "A2").contains("bold"));
    // A new rule goes first; per field the higher priority wins, others fill in.
    const QString mid = wb.setConditionalFormat(s, {{"ranges", "A1:A5"}, {"type", "cellValue"}, {"operator", "between"},
                                                    {"value1", "35"}, {"value2", 15},
                                                    {"style", QVariantMap{{"textColor", "#0000ff"}, {"fillColor", "#00ff00"}}}});
    QCOMPARE(mid, QStringLiteral("cf2"));
    QVariantMap a3 = look(wb, s, "A3");
    QCOMPARE(a3.value("fillColor").toString(), QStringLiteral("#00ff00"));
    QCOMPARE(a3.value("textColor").toString(), QStringLiteral("#0000ff"));
    QCOMPARE(a3.value("bold").toBool(), true);
    QVariantMap rule = wb.conditionalFormats(s).first().toMap();
    rule.insert("stopIfTrue", true);
    QCOMPARE(wb.setConditionalFormat(s, rule), mid);
    QVERIFY(!look(wb, s, "A3").contains("bold")); // stopped before cf1
    QVERIFY(wb.moveConditionalFormat(s, big, 0));
    a3 = look(wb, s, "A3");
    QCOMPARE(a3.value("fillColor").toString(), QStringLiteral("#ff0000"));
    QCOMPARE(a3.value("textColor").toString(), QStringLiteral("#0000ff"));
    QCOMPARE(wb.conditionalFormats(s).first().toMap().value("id").toString(), big);

    QVERIFY(put(wb, s, "B1", "apple PIE"));
    QVERIFY(put(wb, s, "B3", "=1/0"));
    QVERIFY(put(wb, s, "C4", "x"));
    QVERIFY(!wb.setConditionalFormat(s, {{"ranges", "C1:C5"}, {"type", "expression"}, {"value1", "=A1>=40"},
                                         {"style", QVariantMap{{"italic", true}}}}).isEmpty());
    QVERIFY(!wb.setConditionalFormat(s, {{"ranges", "B1:B3"}, {"type", "containsText"}, {"text", "pie"},
                                         {"style", QVariantMap{{"underline", true}}}}).isEmpty());
    QVERIFY(!wb.setConditionalFormat(s, {{"ranges", "B1:B3"}, {"type", "blank"},
                                         {"style", QVariantMap{{"strikeout", true}}}}).isEmpty());
    QVERIFY(!wb.setConditionalFormat(s, {{"ranges", "B1:B3"}, {"type", "errors"},
                                         {"style", QVariantMap{{"fillColor", "#999999"}}}}).isEmpty());
    // The expression is relative to C1: at C4 it reads A4 (40), at C3 A3 (30).
    QCOMPARE(look(wb, s, "C4").value("italic").toBool(), true);
    QVERIFY(!look(wb, s, "C3").contains("italic"));
    QCOMPARE(look(wb, s, "B1").value("underline").toBool(), true);
    QCOMPARE(look(wb, s, "B2").value("strikeout").toBool(), true);
    QCOMPARE(look(wb, s, "B3").value("fillColor").toString(), QStringLiteral("#999999"));
    // Conditional fields win over the cell's own format.
    QVERIFY(wb.applyStyle(s, 2, 0, 2, 0, {{"fillColor", "#123456"}, {"italic", true}}));
    QCOMPARE(look(wb, s, "A3").value("fillColor").toString(), QStringLiteral("#ff0000"));
    QCOMPARE(look(wb, s, "A3").value("italic").toBool(), true);

    QVERIFY(!wb.checkConditionalFormat(s, {{"ranges", "A1"}, {"type", "cellValue"}, {"operator", "between"},
                                           {"value1", "1"}, {"style", QVariantMap{{"bold", true}}}}).value("ok").toBool());
    QVERIFY(!wb.checkConditionalFormat(s, {{"ranges", "A1"}, {"type", "sparkle"}}).value("ok").toBool());
    QVERIFY(!wb.checkConditionalFormat(s, {{"ranges", "A1:A2000"}, {"type", "blank"},
                                           {"style", QVariantMap{{"bold", true}}}}).value("ok").toBool());
    QVERIFY(!wb.checkConditionalFormat(s, {{"ranges", "A1"}, {"type", "blank"}}).value("ok").toBool());
    QVERIFY(!wb.checkConditionalFormat(s, {{"ranges", "A1"}, {"type", "expression"}, {"value1", "=SUM("},
                                           {"style", QVariantMap{{"bold", true}}}}).value("ok").toBool());
    QVERIFY(wb.setConditionalFormat(s, {{"ranges", "A1"}, {"type", "blank"}, {"extra", 1}}).isEmpty());
    const int rules = wb.conditionalFormats(s).size();
    QVERIFY(wb.removeConditionalFormat(s, big));
    QCOMPARE(wb.conditionalFormats(s).size(), rules - 1);
    QVERIFY(wb.undo());
    QCOMPARE(wb.conditionalFormats(s).size(), rules);
}

void WorkbookRulesTest::scalesBarsAndIcons()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    for (const auto &[address, value] : {std::pair{"D1", "0"}, std::pair{"D2", "5"}, std::pair{"D3", "10"},
                                         std::pair{"E1", "1"}, std::pair{"E2", "2"}, std::pair{"E3", "3"}})
        QVERIFY(put(wb, s, QLatin1String(address), QLatin1String(value)));
    QVERIFY(put(wb, s, "D4", "text")); // ignored by the scale
    const QString scale = wb.setConditionalFormat(s, {{"ranges", "D1:D4"}, {"type", "colorScale"},
                                                      {"minColor", "#000000"}, {"maxColor", "#ffffff"}});
    QCOMPARE(look(wb, s, "D1").value("fillColor").toString(), QStringLiteral("#000000"));
    QCOMPARE(look(wb, s, "D2").value("fillColor").toString(), QStringLiteral("#808080"));
    QCOMPARE(look(wb, s, "D3").value("fillColor").toString(), QStringLiteral("#ffffff"));
    QVERIFY(!look(wb, s, "D4").contains("fillColor"));
    QVariantMap three = wb.conditionalFormats(s).first().toMap();
    three.insert("midColor", "#ff0000");
    QCOMPARE(wb.setConditionalFormat(s, three), scale);
    QCOMPARE(look(wb, s, "D2").value("fillColor").toString(), QStringLiteral("#ff0000"));
    QVERIFY(put(wb, s, "D3", "20")); // the cached extremes follow edits: D2 is now a quarter up
    QCOMPARE(look(wb, s, "D2").value("fillColor").toString(), QStringLiteral("#800000"));
    QCOMPARE(look(wb, s, "D3").value("fillColor").toString(), QStringLiteral("#ffffff"));
    QVERIFY(!wb.setConditionalFormat(s, {{"ranges", "E1:E3"}, {"type", "dataBar"}}).isEmpty());
    QCOMPARE(look(wb, s, "E1").value("dataBar").toDouble(), 0.0);
    QCOMPARE(look(wb, s, "E2").value("dataBar").toDouble(), 0.5);
    QCOMPARE(look(wb, s, "E3").value("dataBarColor").toString(), QStringLiteral("#638ec6"));
    QVERIFY(!wb.setConditionalFormat(s, {{"ranges", "E1:E3"}, {"type", "iconSet"}}).isEmpty());
    QCOMPARE(look(wb, s, "E1").value("icon").toString(), QStringLiteral("arrow-down"));
    QCOMPARE(look(wb, s, "E2").value("icon").toString(), QStringLiteral("arrow-right"));
    QCOMPARE(look(wb, s, "E3").value("icon").toString(), QStringLiteral("arrow-up"));
    QCOMPARE(look(wb, s, "E3").value("iconTone").toString(), QStringLiteral("positive"));
}

void WorkbookRulesTest::conditionalPreviewEqualsCommittedAndReopened()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    for (int i = 1; i <= 6; ++i) QVERIFY(put(wb, s, QStringLiteral("A%1").arg(i), QString::number(i)));
    QVERIFY(put(wb, s, "B1", "3"));
    QVERIFY(!wb.setConditionalFormat(s, {{"ranges", "A1:A6"}, {"type", "cellValue"}, {"operator", "less"},
                                         {"value1", "3"}, {"style", QVariantMap{{"italic", true}}}}).isEmpty());
    const QVariantMap candidate{{"ranges", QStringList{"A1:A6"}}, {"type", "cellValue"}, {"operator", "greaterOrEqual"},
                                {"value1", "=$B$1"}, {"style", QVariantMap{{"bold", true}, {"textColor", "#aa0000"}}},
                                {"stopIfTrue", true}};
    const QVariantList preview = wb.previewConditionalFormat(s, candidate, 0, 0, 6, 1);
    QCOMPARE(preview.size(), 6);
    QCOMPARE(wb.conditionalFormats(s).size(), 1); // previewing changes nothing
    QVERIFY(!wb.canRedo());
    const QString id = wb.setConditionalFormat(s, candidate);
    QVERIFY(!id.isEmpty());
    Workbook reopened;
    QVERIFY(reopened.restore(wb.serialize()));
    for (int r = 0; r < 6; ++r) {
        QVariantMap expected = preview.at(r).toMap();
        expected.remove("row");
        expected.remove("column");
        expected.remove("display");
        QCOMPARE(wb.cellPresentation(s, r, 0), expected);
        QCOMPARE(reopened.cellPresentation(s, r, 0), expected);
    }
    QCOMPARE(preview.at(2).toMap().value("bold").toBool(), true);   // 3 >= $B$1
    QVERIFY(!preview.at(0).toMap().contains("bold"));
    QCOMPARE(preview.at(0).toMap().value("italic").toBool(), true);
    QCOMPARE(reopened.conditionalFormats(s), wb.conditionalFormats(s));
    QVERIFY(wb.previewConditionalFormat(s, {{"type", "bogus"}}, 0, 0, 1, 1).isEmpty());
}

void WorkbookRulesTest::conditionalRulesFollowStructure()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    const QString other = wb.addSheet(QStringLiteral("Limits"));
    // Relative to the anchor C3: each cell compares the value one row up, two columns left.
    QVERIFY(!wb.setConditionalFormat(s, {{"ranges", QStringList{"C3:C4", "E8"}}, {"type", "expression"},
                                         {"value1", "=A2>=Limits!$A$1"}, {"style", QVariantMap{{"italic", true}}}}).isEmpty());
    QVERIFY(wb.insertRows(s, 0, 1));
    QVariantMap rule = wb.conditionalFormats(s).first().toMap();
    QCOMPARE(rule.value("ranges").toStringList(), (QStringList{"C4:C5", "E9"}));
    QCOMPARE(rule.value("value1").toString(), QStringLiteral("=A3>=Limits!$A$1"));
    QVERIFY(wb.insertRows(other, 0, 2)); // references into another sheet follow too
    QCOMPARE(wb.conditionalFormats(s).first().toMap().value("value1").toString(), QStringLiteral("=A3>=Limits!$A$3"));
    // Deleting the anchoring area re-anchors on E9 -> E7; E7 still reads what E9 read (C8 -> C6).
    QVERIFY(wb.removeRows(s, 3, 2));
    rule = wb.conditionalFormats(s).first().toMap();
    QCOMPARE(rule.value("ranges").toStringList(), QStringList{"E7"});
    QCOMPARE(rule.value("value1").toString(), QStringLiteral("=C6>=Limits!$A$3"));
    QVERIFY(wb.undo());
    QCOMPARE(wb.conditionalFormats(s).first().toMap().value("ranges").toStringList(), (QStringList{"C4:C5", "E9"}));
    QVERIFY(wb.removeSheet(other));
    QCOMPARE(wb.conditionalFormats(s).first().toMap().value("value1").toString(), QStringLiteral("=A3>=#REF!"));
    QVERIFY(wb.undo());
    QCOMPARE(wb.conditionalFormats(s).first().toMap().value("value1").toString(), QStringLiteral("=A3>=Limits!$A$3"));
    QVERIFY(wb.removeRows(s, 0, 10)); // every area deleted: the rule goes
    QVERIFY(wb.conditionalFormats(s).isEmpty());
}

void WorkbookRulesTest::validationOnEntry()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    const QString id = wb.setValidation(s, {{"ranges", "A1:A10"}, {"type", "wholeNumber"}, {"operator", "between"},
                                            {"value1", "1"}, {"value2", "10"}, {"inputTitle", "Quantity"},
                                            {"inputMessage", "1 to 10"}, {"errorTitle", "Invalid"}});
    QCOMPARE(id, QStringLiteral("dv1"));
    QVERIFY(put(wb, s, "A1", "5"));
    QVERIFY(!put(wb, s, "A1", "11"));
    QCOMPARE(wb.lastError(), QStringLiteral("Enter a whole number between 1 and 10"));
    QCOMPARE(wb.lastErrorDetail().value("ruleId").toString(), id);
    QCOMPARE(in(wb, s, "A1"), QStringLiteral("5"));
    QVERIFY(!put(wb, s, "A1", "2.5"));
    QVERIFY(!put(wb, s, "A1", "abc"));
    QVERIFY(put(wb, s, "A1", "")); // blanks are allowed by default
    QVERIFY(put(wb, s, "A2", "=2*3"));
    QVERIFY(!put(wb, s, "A3", "=20"));
    QVariantMap report = wb.validateInput(s, 0, 0, QStringLiteral("11"));
    QCOMPARE(report.value("ok").toBool(), false);
    QCOMPARE(report.value("style").toString(), QStringLiteral("stop"));
    QCOMPARE(report.value("title").toString(), QStringLiteral("Invalid"));
    QCOMPARE(wb.validateInput(s, 0, 0, QStringLiteral("=1+")).value("syntax").toBool(), true);
    QCOMPARE(wb.validateInput(s, 0, 5, QStringLiteral("99")).value("ok").toBool(), true); // no rule on F1
    QCOMPARE(wb.validationAt(s, 3, 0).value("inputMessage").toString(), QStringLiteral("1 to 10"));
    QVERIFY(wb.validationAt(s, 20, 0).isEmpty());

    QVariantMap rule = wb.validations(s).first().toMap();
    rule.insert("errorStyle", "warning");
    rule.insert("errorMessage", "Unusual quantity");
    rule.insert("allowBlank", false);
    QCOMPARE(wb.setValidation(s, rule), id);
    QVERIFY(put(wb, s, "A1", "11")); // warnings are advisory: the view asks first
    report = wb.validateInput(s, 0, 0, QStringLiteral("11"));
    QCOMPARE(report.value("style").toString(), QStringLiteral("warning"));
    QCOMPARE(report.value("message").toString(), QStringLiteral("Unusual quantity"));
    QCOMPARE(wb.validateInput(s, 0, 0, QString()).value("message").toString(), QStringLiteral("Unusual quantity"));

    QVERIFY(!wb.checkValidation(s, {{"ranges", "A1"}, {"type", "decimal"}, {"operator", "between"},
                                    {"value1", "1"}}).value("ok").toBool());
    QVERIFY(!wb.checkValidation(s, {{"ranges", "A1"}, {"type", "decimal"}, {"operator", "greater"},
                                    {"value1", "abc"}}).value("ok").toBool());
    QVERIFY(!wb.checkValidation(s, {{"ranges", "A1"}, {"type", "list"}}).value("ok").toBool());
    QVERIFY(!wb.checkValidation(s, {{"ranges", "A1"}, {"type", "any"}, {"errorStyle", "panic"}}).value("ok").toBool());
}

void WorkbookRulesTest::validationListsAndCustomRules()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(!wb.setValidation(s, {{"ranges", "B1:B5"}, {"type", "list"}, {"items", QStringList{"Red", "Green"}}}).isEmpty());
    QVERIFY(put(wb, s, "B1", "green"));
    QVERIFY(!put(wb, s, "B2", "Blue"));
    QCOMPARE(wb.lastError(), QStringLiteral("Choose one of: Red, Green"));
    QVERIFY(put(wb, s, "D1", "x"));
    QVERIFY(put(wb, s, "D2", "y"));
    QVERIFY(put(wb, s, "D3", "z"));
    QVERIFY(!wb.setValidation(s, {{"ranges", "C1:C5"}, {"type", "list"}, {"value1", "=$D$1:$D$3"}}).isEmpty());
    QVERIFY(put(wb, s, "C1", "Y"));
    QVERIFY(!put(wb, s, "C2", "w"));
    QVERIFY(wb.defineName(QStringLiteral("Codes"), s, 0, 3, 1, 3));
    QVERIFY(!wb.setValidation(s, {{"ranges", "G1"}, {"type", "list"}, {"value1", "=Codes"}}).isEmpty());
    QVERIFY(put(wb, s, "G1", "x"));
    QVERIFY(!put(wb, s, "G1", "z"));
    QVERIFY(!wb.setValidation(s, {{"ranges", "E1"}, {"type", "textLength"}, {"operator", "lessOrEqual"},
                                  {"value1", "3"}}).isEmpty());
    QVERIFY(put(wb, s, "E1", "abc"));
    QVERIFY(!put(wb, s, "E1", "abcd"));
    QCOMPARE(wb.lastError(), QStringLiteral("Enter text with a length of at most 3"));
    QVERIFY(put(wb, s, "A2", "3"));
    QVERIFY(!wb.setValidation(s, {{"ranges", "F1:F5"}, {"type", "custom"}, {"value1", "=F1>A1"}}).isEmpty());
    QVERIFY(put(wb, s, "F2", "5")); // relative to F1: F2 compares with A2
    QVERIFY(!put(wb, s, "F2", "2"));
}

void WorkbookRulesTest::pastePolicies()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(!wb.setValidation(s, {{"ranges", "A1:A10"}, {"type", "wholeNumber"}, {"operator", "lessOrEqual"},
                                  {"value1", "10"}}).isEmpty());
    QVERIFY(!wb.setValidation(s, {{"ranges", "B1:B10"}, {"type", "wholeNumber"}, {"operator", "lessOrEqual"},
                                  {"value1", "10"}, {"errorStyle", "information"}}).isEmpty());
    QVERIFY(put(wb, s, "A1", "5"));
    QVERIFY(!wb.pasteInputs(s, 0, 0, column("1", "99", "3")));
    QVariantMap report = wb.lastPasteReport();
    QCOMPARE(report.value("ok").toBool(), false);
    QCOMPARE(report.value("applied").toInt(), 0);
    QCOMPARE(report.value("invalid").toList().size(), 1);
    QCOMPARE(report.value("invalid").toList().first().toMap().value("address").toString(), QStringLiteral("A2"));
    QCOMPARE(wb.lastError(), QStringLiteral("A2: Enter a whole number of at most 10"));
    QCOMPARE(in(wb, s, "A1"), QStringLiteral("5")); // nothing applied
    QVERIFY(!wb.pasteInputs(s, 0, 0, column("=1+", "2", "30")));
    QCOMPARE(wb.lastPasteReport().value("invalid").toList().size(), 2);
    QVERIFY(wb.lastError().startsWith(QStringLiteral("2 cells are invalid; first A1")));

    QVERIFY(wb.pasteInputs(s, 0, 0, column("2", "99", "4"), QStringLiteral("skipInvalid")));
    report = wb.lastPasteReport();
    QCOMPARE(report.value("applied").toInt(), 2);
    QCOMPARE(report.value("invalid").toList().size(), 1);
    QCOMPARE(in(wb, s, "A1"), QStringLiteral("2"));
    QCOMPARE(in(wb, s, "A2"), QString());
    QCOMPARE(in(wb, s, "A3"), QStringLiteral("4"));
    QCOMPARE(wb.undoText(), QStringLiteral("Paste cells"));
    QVERIFY(wb.undo()); // one step for the partial paste
    QCOMPARE(in(wb, s, "A1"), QStringLiteral("5"));
    QCOMPARE(in(wb, s, "A3"), QString());

    // Information-style problems are applied and reported.
    QVERIFY(wb.pasteInputs(s, 0, 1, column("50", "1", "2")));
    report = wb.lastPasteReport();
    QCOMPARE(report.value("applied").toInt(), 3);
    QCOMPARE(report.value("invalid").toList().first().toMap().value("style").toString(), QStringLiteral("information"));
    QCOMPARE(in(wb, s, "B1"), QStringLiteral("50"));
    QVERIFY(!wb.pasteInputs(s, 0, 0, column("1", "2", "3"), QStringLiteral("sometimes")));
    QVERIFY(wb.setCellInputs(s, {QVariantMap{{"row", 5}, {"column", 0}, {"input", "7"}},
                                 QVariantMap{{"row", 6}, {"column", 0}, {"input", "70"}}},
                             QStringLiteral("skipInvalid"), QStringLiteral("Edit two cells")));
    QCOMPARE(wb.undoText(), QStringLiteral("Edit two cells"));
    QCOMPARE(in(wb, s, "A6"), QStringLiteral("7"));
    QCOMPARE(in(wb, s, "A7"), QString());
}

void WorkbookRulesTest::validationOwnershipAndPersistence()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    const QString first = wb.setValidation(s, {{"ranges", "A1:A10"}, {"type", "decimal"}, {"operator", "greater"},
                                               {"value1", "0"}});
    const QString second = wb.setValidation(s, {{"ranges", "A5:B6"}, {"type", "any"}, {"inputMessage", "Free"}});
    QCOMPARE(second, QStringLiteral("dv2"));
    QVariantList rules = wb.validations(s);
    QCOMPARE(rules.size(), 2);
    QCOMPARE(rules.first().toMap().value("ranges").toStringList(), (QStringList{"A1:A4", "A7:A10"}));
    QVERIFY(put(wb, s, "A5", "-3")); // now owned by the "any" rule
    QVERIFY(!put(wb, s, "A7", "-3"));
    QCOMPARE(wb.validationAt(s, 4, 1).value("inputMessage").toString(), QStringLiteral("Free"));
    QVERIFY(wb.setValidation(s, {{"ranges", "A1:A10"}, {"type", "any"}, {"id", first}}) == first);
    QCOMPARE(wb.validations(s).size(), 2);
    QCOMPARE(wb.validations(s).at(1).toMap().value("ranges").toStringList(), QStringList{"B5:B6"});
    QVERIFY(wb.undo());
    QCOMPARE(wb.validations(s), rules);
    QVERIFY(wb.removeValidation(s, second));
    QVERIFY(!wb.removeValidation(s, second));
    QVERIFY(wb.undo());

    const QByteArray bytes = wb.serialize();
    Workbook reopened;
    QVERIFY(reopened.restore(bytes));
    QCOMPARE(reopened.validations(s), wb.validations(s));
    QVERIFY(!reopened.setCellInput(s, 6, 0, QStringLiteral("-1")));
    QCOMPARE(reopened.serialize(), bytes);
    // Two rules may not own one cell in a file either.
    QVERIFY(!reopened.restore(QByteArray(bytes).replace("\"A5:B6\"", "\"A4:B6\"")));
}

QTEST_GUILESS_MAIN(WorkbookRulesTest)
#include "tst_workbook_rules.moc"
