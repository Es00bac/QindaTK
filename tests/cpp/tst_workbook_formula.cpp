// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_formula.h"
#include <QtTest>
#include <map>

using namespace QindaTK::Calc;

namespace {
QString sheetIdFor(const QString &name)
{
    if (name.compare(QStringLiteral("Sheet2"), Qt::CaseInsensitive) == 0) return QStringLiteral("s2");
    if (name.compare(QStringLiteral("My Sheet"), Qt::CaseInsensitive) == 0) return QStringLiteral("s3");
    if (name.compare(QStringLiteral("Sheet1"), Qt::CaseInsensitive) == 0) return QStringLiteral("s1");
    return {};
}
QString sheetNameFor(const QString &id)
{
    if (id == u"s1") return QStringLiteral("Sheet1");
    if (id == u"s2") return QStringLiteral("Sheet2");
    if (id == u"s3") return QStringLiteral("My Sheet");
    return {};
}
FormulaPtr parse(const QString &text)
{
    const ParseResult result = parseFormula(text, sheetIdFor);
    if (!result.ok()) qWarning() << text << result.error.message << result.error.start << result.error.end;
    return result.formula;
}
QString render(const FormulaPtr &formula) { return formula ? renderFormula(*formula, sheetNameFor) : QString(); }

class MapContext final : public EvalContext {
public:
    std::map<std::tuple<QString, int, int>, Value> cells;
    void set(const QString &address, const Value &value, const QString &sheet = QStringLiteral("s1"))
    {
        RefPoint p;
        QVERIFY(parseCellAddress(address, &p));
        cells[{sheet, p.row, p.column}] = value;
    }
    Value cell(const QString &sheetId, int row, int column) const override
    {
        const auto it = cells.find({sheetId, row, column});
        return it == cells.end() ? Value::blank() : it->second;
    }
    void visitRange(const QString &sheetId, int r1, int c1, int r2, int c2,
                    const std::function<bool(const Value &)> &visitor) const override
    {
        for (int r = r1; r <= r2; ++r)
            for (int c = c1; c <= c2; ++c) {
                const auto it = cells.find({sheetId, r, c});
                if (it != cells.end() && !visitor(it->second)) return;
            }
    }
};
} // namespace

class WorkbookFormulaTest : public QObject {
    Q_OBJECT
private slots:
    void canonicalRendering_data();
    void canonicalRendering();
    void parseErrorsCarrySpans_data();
    void parseErrorsCarrySpans();
    void knownAnswers_data();
    void knownAnswers();
    void numberDisplay();
    void addresses();
    void copyOffsetsMoveOnlyRelativeParts();
    void structuralRewritesKeepTargets();
};

void WorkbookFormulaTest::canonicalRendering_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("canonical");
    QTest::newRow("precedence") << "=1+2*3" << "=1+2*3";
    QTest::newRow("spaces and case") << "= sum( a1 : b2 ) + 1" << "=SUM(A1:B2)+1";
    QTest::newRow("anchors") << "=$a$1+b$2+$c3" << "=$A$1+B$2+$C3";
    QTest::newRow("range normalized") << "=B2:A1" << "=A1:B2";
    QTest::newRow("mixed anchors normalized") << "=$B2:A$1" << "=A$1:$B2";
    QTest::newRow("group kept") << "=((1))" << "=((1))";
    QTest::newRow("negation power") << "=-2^2" << "=-2^2";
    QTest::newRow("percent") << "=50%" << "=50%";
    QTest::newRow("text escape") << "=\"a\"\"b\"&\"c\"" << "=\"a\"\"b\"&\"c\"";
    QTest::newRow("shortest number") << "=0.1+1e3+1.5E-7" << "=0.1+1000+1.5E-07";
    QTest::newRow("quoted sheet") << "='my sheet'!a1" << "='My Sheet'!A1";
    QTest::newRow("sheet range") << "=sheet2!b2:a1" << "=Sheet2!A1:B2";
    QTest::newRow("booleans errors") << "=if(true,#n/a,false)" << "=IF(TRUE,#N/A,FALSE)";
    QTest::newRow("comparison chain") << "=a1<>b1" << "=A1<>B1";
    QTest::newRow("unary chain") << "=1--1" << "=1--1";
    QTest::newRow("max bounds") << "=XFD1048576" << "=XFD1048576";
}
void WorkbookFormulaTest::canonicalRendering()
{
    QFETCH(QString, input);
    QFETCH(QString, canonical);
    const FormulaPtr formula = parse(input);
    QVERIFY(formula);
    QCOMPARE(render(formula), canonical);
    const FormulaPtr again = parse(canonical);
    QVERIFY(again);
    QCOMPARE(render(again), canonical);
    QVERIFY(again->sameAs(*formula));
}

void WorkbookFormulaTest::parseErrorsCarrySpans_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("message");
    QTest::addColumn<int>("start");
    QTest::addColumn<int>("end");
    QTest::newRow("dangling operator") << "=1+" << "ends where a value" << 3 << 4;
    QTest::newRow("unclosed call") << "=SUM(1,2" << "Expected ',' or ')' in SUM" << 8 << 9;
    QTest::newRow("unknown function") << "=FOO(1)" << "Unknown function 'FOO'" << 1 << 4;
    QTest::newRow("invalid name") << "=A1+ab$c" << "'ab$c' is not a valid name or reference" << 4 << 8;
    QTest::newRow("unknown sheet") << "=Nope!A1" << "Unknown sheet 'Nope'" << 1 << 5;
    QTest::newRow("open text") << "=\"abc" << "closing quote" << 1 << 5;
    QTest::newRow("open group") << "=(1+2" << "Missing closing parenthesis" << 1 << 5;
    QTest::newRow("juxtaposed") << "=1 2" << "Unexpected '2'" << 3 << 4;
    QTest::newRow("arity") << "=IF(1)" << "IF expects" << 1 << 6;
    QTest::newRow("empty argument") << "=SUM(A1,,2)" << "Expected a value before ','" << 8 << 9;
    QTest::newRow("range end") << "=A1:" << "after ':'" << 4 << 5;
    QTest::newRow("bad character") << "=1@2" << "Unexpected character '@'" << 2 << 3;
    QTest::newRow("column bound") << "=XFE1" << "'XFE1' is outside the sheet grid" << 1 << 5;
    QTest::newRow("row bound") << "=A1048577" << "'A1048577' is outside the sheet grid" << 1 << 9;
    QTest::newRow("whole column end") << "=SUM(A:1)" << "Expected a column such as B" << 7 << 8;
    QTest::newRow("whole row end") << "=SUM(2:B)" << "Expected a row number" << 7 << 8;
    QTest::newRow("exponent") << "=1e+" << "exponent needs digits" << 1 << 4;
    QTest::newRow("error literal") << "=#FOO" << "Unknown error value" << 1 << 2;
    QTest::newRow("no equals") << "SUM(1)" << "starts with '='" << 0 << 1;
    QTest::newRow("nesting") << "=" + QString(65, QLatin1Char('(')) + "1" + QString(65, QLatin1Char(')'))
                             << "nests more than 64" << 65 << 66;
    QString longFormula = QStringLiteral("=1");
    while (longFormula.size() <= MaxFormulaLength) longFormula += QStringLiteral("+1");
    QTest::newRow("length") << longFormula << "longer than 8192" << 8192 << int(longFormula.size());
}
void WorkbookFormulaTest::parseErrorsCarrySpans()
{
    QFETCH(QString, input);
    QFETCH(QString, message);
    QFETCH(int, start);
    QFETCH(int, end);
    const ParseResult result = parseFormula(input, sheetIdFor);
    QVERIFY(!result.ok());
    QVERIFY2(result.error.message.contains(message), qPrintable(result.error.message));
    QCOMPARE(result.error.start, start);
    QCOMPARE(result.error.end, end);
}

void WorkbookFormulaTest::knownAnswers_data()
{
    QTest::addColumn<QString>("formula");
    QTest::addColumn<QString>("display");
    QTest::addColumn<QString>("type");
    QTest::newRow("precedence") << "=1+2*3" << "7" << "number";
    QTest::newRow("group") << "=(1+2)*3" << "9" << "number";
    QTest::newRow("power left assoc") << "=2^3^2" << "64" << "number";
    QTest::newRow("negation binds first") << "=-2^2" << "4" << "number";
    QTest::newRow("negative exponent") << "=2^-1" << "0.5" << "number";
    QTest::newRow("percent") << "=50%" << "0.5" << "number";
    QTest::newRow("division") << "=10/4" << "2.5" << "number";
    QTest::newRow("div zero") << "=1/0" << "#DIV/0!" << "error";
    QTest::newRow("zero pow zero") << "=0^0" << "#NUM!" << "error";
    QTest::newRow("zero pow negative") << "=0^-1" << "#DIV/0!" << "error";
    QTest::newRow("complex root") << "=(-8)^(1/3)" << "#NUM!" << "error";
    QTest::newRow("overflow") << "=1E+308*10" << "#NUM!" << "error";
    QTest::newRow("blank is zero") << "=Z9" << "0" << "number";
    QTest::newRow("blank plus") << "=Z9+1" << "1" << "number";
    QTest::newRow("blank concat") << "=Z9&\"x\"" << "x" << "text";
    QTest::newRow("blank equals zero") << "=Z9=0" << "TRUE" << "boolean";
    QTest::newRow("blank equals empty") << "=Z9=\"\"" << "TRUE" << "boolean";
    QTest::newRow("blank equals false") << "=Z9=FALSE" << "TRUE" << "boolean";
    QTest::newRow("numeric text") << "=B1+1" << "4" << "number";
    QTest::newRow("plain text") << "=B2+1" << "#VALUE!" << "error";
    QTest::newRow("text compare ci") << "=\"a\"<\"B\"" << "TRUE" << "boolean";
    QTest::newRow("text equal ci") << "=\"A\"=\"a\"" << "TRUE" << "boolean";
    QTest::newRow("number before text") << "=1<\"a\"" << "TRUE" << "boolean";
    QTest::newRow("text before boolean") << "=\"z\"<TRUE" << "TRUE" << "boolean";
    QTest::newRow("no coercion in equality") << "=1=\"1\"" << "FALSE" << "boolean";
    QTest::newRow("boolean arithmetic") << "=C1+1" << "2" << "number";
    QTest::newRow("boolean in reference ignored") << "=SUM(C1)" << "0" << "number";
    QTest::newRow("direct boolean counted") << "=SUM(TRUE,2)" << "3" << "number";
    QTest::newRow("direct numeric text") << "=SUM(\"3\",1)" << "4" << "number";
    QTest::newRow("direct bad text") << "=SUM(\"x\")" << "#VALUE!" << "error";
    QTest::newRow("concat third") << "=1/3&\"\"" << "0.333333333333333" << "text";
    QTest::newRow("concat exponent") << "=1E+20&\"\"" << "1E+20" << "text";
    QTest::newRow("concat boolean") << "=TRUE&1" << "TRUE1" << "text";
    QTest::newRow("sum mixed range") << "=SUM(A1:A5)" << "5" << "number";
    QTest::newRow("average mixed range") << "=AVERAGE(A1:A5)" << "2.5" << "number";
    QTest::newRow("min mixed range") << "=MIN(A1:A5)" << "1" << "number";
    QTest::newRow("max mixed range") << "=MAX(A1:A5)" << "4" << "number";
    QTest::newRow("count mixed range") << "=COUNT(A1:A5)" << "2" << "number";
    QTest::newRow("counta mixed range") << "=COUNTA(A1:A5)" << "4" << "number";
    QTest::newRow("range error") << "=SUM(D1:D2)" << "#N/A" << "error";
    QTest::newRow("count ignores errors") << "=COUNT(D1:D2,1)" << "2" << "number";
    QTest::newRow("average empty") << "=AVERAGE(Z1:Z9)" << "#DIV/0!" << "error";
    QTest::newRow("min empty") << "=MIN(Z1:Z9)" << "0" << "number";
    QTest::newRow("if true") << "=IF(1,\"y\",\"n\")" << "y" << "text";
    QTest::newRow("if default false") << "=IF(0,\"y\")" << "FALSE" << "boolean";
    QTest::newRow("if text condition") << "=IF(\"true\",1,2)" << "1" << "number";
    QTest::newRow("if bad condition") << "=IF(\"x\",1,2)" << "#VALUE!" << "error";
    QTest::newRow("if lazy") << "=IF(1,2,1/0)" << "2" << "number";
    QTest::newRow("if error condition") << "=IF(1/0,1,2)" << "#DIV/0!" << "error";
    QTest::newRow("if returns reference") << "=SUM(IF(TRUE,A1:A5,0))" << "5" << "number";
    QTest::newRow("iferror") << "=IFERROR(1/0,\"z\")" << "z" << "text";
    QTest::newRow("iferror pass") << "=IFERROR(5,0)" << "5" << "number";
    QTest::newRow("and") << "=AND(TRUE,1)" << "TRUE" << "boolean";
    QTest::newRow("and false") << "=AND(TRUE,0)" << "FALSE" << "boolean";
    QTest::newRow("or") << "=OR(FALSE,0)" << "FALSE" << "boolean";
    QTest::newRow("and bad text") << "=AND(\"x\")" << "#VALUE!" << "error";
    QTest::newRow("and no logicals") << "=AND(E1:E2)" << "#VALUE!" << "error";
    QTest::newRow("not") << "=NOT(0)" << "TRUE" << "boolean";
    QTest::newRow("abs") << "=ABS(-2.5)" << "2.5" << "number";
    QTest::newRow("round half away") << "=ROUND(2.675,2)" << "2.68" << "number";
    QTest::newRow("round negative half") << "=ROUND(-2.5,0)" << "-3" << "number";
    QTest::newRow("round tens") << "=ROUND(1234.5678,-2)" << "1200" << "number";
    QTest::newRow("round binary noise") << "=ROUND(0.1+0.2,15)" << "0.3" << "number";
    QTest::newRow("left error first") << "=1/0+#N/A" << "#DIV/0!" << "error";
    QTest::newRow("right error") << "=#N/A+1/0" << "#N/A" << "error";
    QTest::newRow("area in scalar") << "=A1:A2+1" << "#VALUE!" << "error";
    QTest::newRow("single area in scalar") << "=A1:A1+1" << "2" << "number";
    QTest::newRow("cross sheet") << "=Sheet2!A1*2" << "84" << "number";
    QTest::newRow("float noise display") << "=0.1+0.2" << "0.3" << "number";
}
void WorkbookFormulaTest::knownAnswers()
{
    QFETCH(QString, formula);
    QFETCH(QString, display);
    QFETCH(QString, type);
    MapContext context;
    context.set("A1", Value::fromNumber(1));
    context.set("A2", Value::fromText("text"));
    context.set("A3", Value::fromBool(true));
    context.set("A5", Value::fromNumber(4));
    context.set("B1", Value::fromText("3"));
    context.set("B2", Value::fromText("abc"));
    context.set("C1", Value::fromBool(true));
    context.set("D1", Value::fromError(ErrorKind::NA));
    context.set("D2", Value::fromNumber(5));
    context.set("E1", Value::fromText("text"));
    context.set("A1", Value::fromNumber(42), QStringLiteral("s2"));
    const FormulaPtr parsed = parse(formula);
    QVERIFY(parsed);
    const Value value = evaluateFormula(*parsed, QStringLiteral("s1"), context);
    QCOMPARE(displayText(value), display);
    QCOMPARE(typeName(value.type), type);
}

void WorkbookFormulaTest::numberDisplay()
{
    QCOMPARE(formatNumber(0.1 + 0.2), QStringLiteral("0.3"));
    QCOMPARE(formatNumber(1e21), QStringLiteral("1E+21"));
    QCOMPARE(formatNumber(1.0 / 3.0), QStringLiteral("0.333333333333333"));
    QCOMPARE(formatNumber(-0.0), QStringLiteral("0"));
    QCOMPARE(formatNumber(123456789012345678.0), QStringLiteral("1.23456789012346E+17"));
    QCOMPARE(numberLiteral(0.1 + 0.2), QStringLiteral("0.30000000000000004"));
    double parsed = 0;
    QVERIFY(parseNumber(u"-1.5e3", &parsed));
    QCOMPARE(parsed, -1500.0);
    QVERIFY(parseNumber(u".5", &parsed));
    QVERIFY(!parseNumber(u"1,000", &parsed));
    QVERIFY(!parseNumber(u" 12", &parsed));
    QVERIFY(!parseNumber(u"1e999", &parsed));
    QVERIFY(!parseNumber(u"inf", &parsed));
}

void WorkbookFormulaTest::addresses()
{
    QCOMPARE(columnName(0), QStringLiteral("A"));
    QCOMPARE(columnName(25), QStringLiteral("Z"));
    QCOMPARE(columnName(26), QStringLiteral("AA"));
    QCOMPARE(columnName(701), QStringLiteral("ZZ"));
    QCOMPARE(columnName(702), QStringLiteral("AAA"));
    QCOMPARE(columnName(MaxColumns - 1), QStringLiteral("XFD"));
    for (int c = 0; c < MaxColumns; ++c) QCOMPARE(columnFromName(columnName(c)), c);
    QCOMPARE(columnFromName(u"XFE"), -1);
    RefPoint p;
    QVERIFY(parseCellAddress(u"$c$7", &p));
    QCOMPARE(p.row, 6);
    QCOMPARE(p.column, 2);
    QVERIFY(p.rowAbsolute && p.columnAbsolute);
    QVERIFY(!parseCellAddress(u"A0", &p));
    QVERIFY(!parseCellAddress(u"1A", &p));
    QCOMPARE(quoteSheetName(QStringLiteral("Sheet1")), QStringLiteral("Sheet1"));
    QCOMPARE(quoteSheetName(QStringLiteral("My Sheet")), QStringLiteral("'My Sheet'"));
    QCOMPARE(quoteSheetName(QStringLiteral("A1")), QStringLiteral("'A1'"));
    QCOMPARE(quoteSheetName(QStringLiteral("O'Brien")), QStringLiteral("'O''Brien'"));
    QCOMPARE(quoteSheetName(QStringLiteral("True")), QStringLiteral("'True'"));
    QCOMPARE(quoteSheetName(QStringLiteral("2024")), QStringLiteral("'2024'"));
    QVERIFY(!sheetNameProblem(QStringLiteral("a/b")).isEmpty());
    QVERIFY(!sheetNameProblem(QString(32, QLatin1Char('x'))).isEmpty());
    QVERIFY(!sheetNameProblem(QStringLiteral("'quoted")).isEmpty());
    QVERIFY(sheetNameProblem(QStringLiteral("Q3 plan (draft)")).isEmpty());
}

void WorkbookFormulaTest::copyOffsetsMoveOnlyRelativeParts()
{
    const FormulaPtr base = parse(QStringLiteral("=A1+$B$2+C$3+$D4"));
    QCOMPARE(render(offsetFormula(base, 2, 1)), QStringLiteral("=B3+$B$2+D$3+$D6"));
    QCOMPARE(render(offsetFormula(parse(QStringLiteral("=A1")), -1, 0)), QStringLiteral("=#REF!"));
    QCOMPARE(render(offsetFormula(parse(QStringLiteral("=SUM(A$1:A1)")), 4, 0)), QStringLiteral("=SUM(A$1:A5)"));
    QCOMPARE(render(offsetFormula(parse(QStringLiteral("=SUM(A5:A$10)")), 10, 0)),
             QStringLiteral("=SUM(A$10:A15)"));
    QCOMPARE(render(offsetFormula(parse(QStringLiteral("=Sheet2!A1+Sheet2!$A$1")), 1, 1)),
             QStringLiteral("=Sheet2!B2+Sheet2!$A$1"));
    const FormulaPtr absolute = parse(QStringLiteral("=$A$1"));
    QCOMPARE(offsetFormula(absolute, 5, 5), absolute); // shared, unchanged
}

void WorkbookFormulaTest::structuralRewritesKeepTargets()
{
    const QString host = QStringLiteral("s1");
    auto rewrite = [&](const QString &text, StructuralEdit edit, bool *affected = nullptr) {
        const Rewrite result = rewriteForStructure(parse(text), host, edit);
        if (affected) *affected = result.valueAffected;
        return render(result.formula);
    };
    const StructuralEdit insertRows{host, true, true, 2, 2};
    bool affected = true;
    QCOMPARE(rewrite(QStringLiteral("=A1+A3+SUM(A2:A4)+$A$5"), insertRows, &affected),
             QStringLiteral("=A1+A5+SUM(A2:A6)+$A$7"));
    QVERIFY(!affected);
    const StructuralEdit removeRows{host, true, false, 2, 2};
    QCOMPARE(rewrite(QStringLiteral("=A1+A3+SUM(A2:A6)+A7"), removeRows, &affected),
             QStringLiteral("=A1+#REF!+SUM(A2:A4)+A5"));
    QVERIFY(affected);
    QCOMPARE(rewrite(QStringLiteral("=SUM(A3:A4)"), removeRows), QStringLiteral("=SUM(#REF!)"));
    QCOMPARE(rewrite(QStringLiteral("=SUM(A1:A3)+SUM(A4:A9)"), removeRows), QStringLiteral("=SUM(A1:A2)+SUM(A3:A7)"));
    QCOMPARE(rewrite(QStringLiteral("=Sheet2!A5+A5"), removeRows), QStringLiteral("=Sheet2!A5+A3"));
    const StructuralEdit insertColumns{host, false, true, 1, 1};
    QCOMPARE(rewrite(QStringLiteral("=A1+B1+SUM(A1:C1)"), insertColumns), QStringLiteral("=A1+C1+SUM(A1:D1)"));
    const StructuralEdit removeColumns{host, false, false, 0, 1};
    QCOMPARE(rewrite(QStringLiteral("=A1+B1+SUM(A1:C1)"), removeColumns), QStringLiteral("=#REF!+A1+SUM(A1:B1)"));
    const StructuralEdit overflow{host, true, true, 0, 1};
    QCOMPARE(rewrite(QStringLiteral("=A1048576"), overflow, &affected), QStringLiteral("=#REF!"));
    QVERIFY(affected);
    const Rewrite removed = invalidateSheet(parse(QStringLiteral("=Sheet2!A1+A1")), QStringLiteral("s2"));
    QVERIFY(removed.changed && removed.valueAffected);
    QCOMPARE(render(removed.formula), QStringLiteral("=#REF!+A1"));
    QVERIFY(removed.formula->sheets.isEmpty());
}

QTEST_GUILESS_MAIN(WorkbookFormulaTest)
#include "tst_workbook_formula.moc"
