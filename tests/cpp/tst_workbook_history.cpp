// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook.h"
#include "workbook_format.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
bool put(Workbook &wb, const QString &sheet, int row, int column, const QString &input)
{
    const bool ok = wb.setCellInput(sheet, row, column, input);
    if (!ok) qWarning() << row << column << input << wb.lastError();
    return ok;
}
qint64 steps(Workbook &wb) { return wb.statistics().value(QStringLiteral("historySteps")).toLongLong(); }
// Document content without the sheet-ID allocation counter, which never
// rolls back so that undo cannot make a sheet ID reusable.
QByteArray content(const QByteArray &bytes)
{
    QJsonObject root = QJsonDocument::fromJson(bytes).object();
    root.remove(QStringLiteral("nextSheetNumber"));
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
QVariantList matrix(std::initializer_list<QVariantList> rows)
{
    QVariantList result;
    for (const QVariantList &row : rows) result.append(QVariant(row));
    return result;
}
// Two sheets with every literal kind, quoted names, cross-sheet formulas,
// an invalidated reference and a cycle.
void populate(Workbook &wb)
{
    const QString budget = wb.sheetIdAt(0);
    QVERIFY(wb.renameSheet(budget, QStringLiteral("Budget 2026")));
    const QString summary = wb.addSheet(QStringLiteral("Summary"));
    const QString scratch = wb.addSheet(QStringLiteral("Scratch"));
    QVERIFY(put(wb, scratch, 0, 0, QStringLiteral("3")));
    QVERIFY(wb.pasteInputs(budget, 0, 0, matrix({
        {QStringLiteral("Item"), QStringLiteral("Cost"), QStringLiteral("Note")},
        {QStringLiteral("Rent"), QStringLiteral("1200.5"), QStringLiteral("'=literal")},
        {QStringLiteral("Food"), QStringLiteral("310.25"), QStringLiteral("'123")},
        {QStringLiteral("Flag"), QStringLiteral("TRUE"), QStringLiteral("#N/A")},
        {QStringLiteral("Total"), QStringLiteral("=SUM(B2:B3)"), QStringLiteral("=B5*0.1+Scratch!A1")}})));
    QVERIFY(put(wb, summary, 0, 0, QStringLiteral("='Budget 2026'!B5/2")));
    QVERIFY(put(wb, summary, 1, 0, QStringLiteral("=IF(A1>700,\"high\",\"low\")&\" \"\"q\"\"\"")));
    QVERIFY(put(wb, summary, 2, 0, QStringLiteral("=A4+1")));
    QVERIFY(put(wb, summary, 3, 0, QStringLiteral("=A3+1")));
    QVERIFY(put(wb, summary, 4, 0, QStringLiteral("=ROUND(0.1+0.2,15)*1E+20")));
    QVERIFY(wb.removeSheet(scratch));
}
}

class WorkbookHistoryTest : public QObject {
    Q_OBJECT
private slots:
    void invalidPasteRollsBackEverything();
    void oneStepUndoRedoAndCancel();
    void saveReopenIsByteEquivalent();
    void fileSaveConflictsAndFailedRestore();
    void formatRegistryImportAndProviderExport();
};

void WorkbookHistoryTest::invalidPasteRollsBackEverything()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, 0, 0, QStringLiteral("1")));
    const QByteArray before = wb.serialize();
    const QString revision = wb.revision();
    const qint64 history = steps(wb);
    QVERIFY(!wb.pasteInputs(s, 0, 0, matrix({{QStringLiteral("5"), QStringLiteral("=A1+1")},
                                            {QStringLiteral("x"), QStringLiteral("=SUM(A1,")}})));
    QCOMPARE(wb.lastErrorDetail().value("address").toString(), QStringLiteral("B2"));
    QCOMPARE(wb.lastErrorDetail().value("start").toInt(), 8);
    QCOMPARE(wb.lastErrorDetail().value("end").toInt(), 9);
    QCOMPARE(wb.serialize(), before);
    QCOMPARE(wb.revision(), revision);
    QCOMPARE(steps(wb), history);
    QVERIFY(!wb.pasteInputs(s, 0, 0, matrix({{QStringLiteral("1"), QStringLiteral("2")}, {QStringLiteral("3")}})));
    QVERIFY(wb.lastError().contains(QStringLiteral("rectangular")));
    QVERIFY(!wb.pasteInputs(s, 999, 25, matrix({{QStringLiteral("1"), QStringLiteral("2")}})));
    QCOMPARE(wb.serialize(), before);

    QVERIFY(wb.beginTransaction(QStringLiteral("Batch")));
    QVERIFY(put(wb, s, 1, 0, QStringLiteral("2")));
    QVERIFY(!wb.pasteInputs(s, 2, 0, matrix({{QStringLiteral("=1+")}})));
    QVERIFY(wb.transactionActive());
    QVERIFY(wb.commitTransaction());
    QCOMPARE(steps(wb), history + 1);
    QCOMPARE(wb.undoText(), QStringLiteral("Batch"));
    QCOMPARE(wb.cellDisplay(s, 1, 0), QStringLiteral("2"));
    QCOMPARE(wb.cellType(s, 2, 0), QStringLiteral("blank"));

    QVERIFY(wb.pasteInputs(s, 5, 0, matrix({{3.5, true, QVariant()}})));
    QCOMPARE(wb.cellValue(s, 5, 0).toDouble(), 3.5);
    QCOMPARE(wb.cellInput(s, 5, 1), QStringLiteral("TRUE"));
    QCOMPARE(wb.cellType(s, 5, 2), QStringLiteral("blank"));
}

void WorkbookHistoryTest::oneStepUndoRedoAndCancel()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, 0, 1, QStringLiteral("4")));
    const QByteArray base = wb.serialize();
    QVERIFY(wb.pasteInputs(s, 2, 0, matrix({{QStringLiteral("1"), QStringLiteral("=A3*2")},
                                           {QStringLiteral("=B3+1"), QStringLiteral("x")}})));
    const QByteArray pasted = wb.serialize();
    QCOMPARE(wb.cellDisplay(s, 3, 0), QStringLiteral("3"));
    QVERIFY(wb.undo());
    QCOMPARE(wb.serialize(), base);
    QVERIFY(wb.redo());
    QCOMPARE(wb.serialize(), pasted);
    QCOMPARE(wb.cellDisplay(s, 3, 0), QStringLiteral("3"));

    const qint64 history = steps(wb);
    QVERIFY(wb.beginTransaction(QStringLiteral("Restructure")));
    QVERIFY(put(wb, s, 0, 0, QStringLiteral("=B1*2")));
    QVERIFY(wb.insertRows(s, 0, 1));
    QVERIFY(wb.renameSheet(s, QStringLiteral("Plan")));
    QVERIFY(wb.fill(s, 1, 0, 1, 1, 2, 0, 3, 1));
    const QString extra = wb.addSheet(QStringLiteral("Extra"));
    QVERIFY(put(wb, extra, 0, 0, QStringLiteral("=Plan!A2+1")));
    QVERIFY(!wb.undo()); // history is locked while a transaction is open
    QVERIFY(wb.commitTransaction());
    QCOMPARE(steps(wb), history + 1);
    QCOMPARE(wb.undoText(), QStringLiteral("Restructure"));
    QCOMPARE(wb.cellInput(s, 1, 0), QStringLiteral("=B2*2"));
    QCOMPARE(wb.cellInput(s, 3, 0), QStringLiteral("=B4*2"));
    QCOMPARE(wb.cellDisplay(extra, 0, 0), QStringLiteral("9"));
    const QByteArray restructured = wb.serialize();
    QVERIFY(wb.undo());
    QCOMPARE(content(wb.serialize()), content(pasted));
    QVERIFY(wb.serialize() != pasted); // only nextSheetNumber differs
    QCOMPARE(wb.sheetCount(), 1);
    QVERIFY(wb.redo());
    QCOMPARE(wb.serialize(), restructured);
    QCOMPARE(wb.cellDisplay(extra, 0, 0), QStringLiteral("9"));

    const QString revision = wb.revision();
    QVERIFY(wb.beginTransaction(QStringLiteral("Abandoned")));
    QVERIFY(put(wb, s, 1, 1, QStringLiteral("99")));
    QVERIFY(wb.removeRows(s, 0, 2));
    QVERIFY(wb.removeSheet(extra));
    QVERIFY(wb.cancelTransaction());
    QCOMPARE(wb.serialize(), restructured);
    QCOMPARE(wb.revision(), revision);
    QCOMPARE(wb.cellDisplay(extra, 0, 0), QStringLiteral("9"));
    QCOMPARE(wb.cellDisplay(s, 1, 0), QStringLiteral("8"));
    QVERIFY(!wb.canRedo());

    wb.markSaved();
    QVERIFY(!wb.isModified());
    QVERIFY(put(wb, s, 9, 9, QStringLiteral("1")));
    QVERIFY(wb.isModified());
    QVERIFY(wb.undo());
    QVERIFY(!wb.isModified());
    for (int i = 0; i < Workbook::MaxHistorySteps + 5; ++i) QVERIFY(put(wb, s, 20, 0, QString::number(i)));
    QCOMPARE(steps(wb), qint64(Workbook::MaxHistorySteps));
    QVERIFY(wb.isModified());
}

void WorkbookHistoryTest::saveReopenIsByteEquivalent()
{
    Workbook wb;
    populate(wb);
    const QByteArray bytes = wb.serialize();
    QVERIFY(bytes.startsWith("{\"documentId\":\""));
    QCOMPARE(wb.serialize(), bytes); // deterministic
    Workbook reopened;
    QVERIFY2(reopened.restore(bytes), qPrintable(reopened.lastError()));
    QCOMPARE(reopened.serialize(), bytes);
    QCOMPARE(reopened.documentId(), wb.documentId());
    QCOMPARE(reopened.sheets(), wb.sheets());
    QVERIFY(!reopened.canUndo());
    for (int index = 0; index < wb.sheetCount(); ++index) {
        const QString id = wb.sheetIdAt(index);
        for (int r = 0; r < 6; ++r)
            for (int c = 0; c < 3; ++c) {
                QCOMPARE(reopened.cellInput(id, r, c), wb.cellInput(id, r, c));
                QCOMPARE(reopened.cellDisplay(id, r, c), wb.cellDisplay(id, r, c));
                QCOMPARE(reopened.cellType(id, r, c), wb.cellType(id, r, c));
            }
    }
    const QString budget = wb.sheetIdAt(0), summary = wb.sheetIdAt(1);
    QCOMPARE(reopened.cellDisplay(budget, 4, 1), QStringLiteral("1510.75"));
    QCOMPARE(reopened.cellInput(budget, 4, 2), QStringLiteral("=B5*0.1+#REF!"));
    QCOMPARE(reopened.cellInput(budget, 1, 2), QStringLiteral("'=literal"));
    QCOMPARE(reopened.cellInput(budget, 2, 2), QStringLiteral("'123"));
    QCOMPARE(reopened.cellInput(summary, 0, 0), QStringLiteral("='Budget 2026'!B5/2"));
    QCOMPARE(reopened.cellDisplay(summary, 0, 0), QStringLiteral("755.375"));
    QCOMPARE(reopened.cellDisplay(summary, 1, 0), QStringLiteral("high \"q\""));
    QCOMPARE(reopened.cellDisplay(summary, 2, 0), QStringLiteral("#CYCLE!"));
    QCOMPARE(reopened.cellDisplay(summary, 4, 0), QStringLiteral("3E+19"));
    QCOMPARE(reopened.addSheet(), QStringLiteral("sheet-4")); // counter persisted
    QVERIFY(reopened.restoreText(QString::fromUtf8(bytes)));
    QCOMPARE(reopened.serializedText(), QString::fromUtf8(bytes));
    const auto snapshot = wb.snapshot();
    QCOMPARE(snapshot->header.schemaId, QStringLiteral("qindatk.workbook.v1"));
    QCOMPARE(snapshot->header.revision.toDecimal(), wb.revision());
    QCOMPARE(snapshot->payload, bytes);
}

void WorkbookHistoryTest::fileSaveConflictsAndFailedRestore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QUrl url = QUrl::fromLocalFile(dir.filePath(QStringLiteral("book.qtkwb")));
    Workbook wb;
    populate(wb);
    QVERIFY(wb.isModified());
    QVERIFY2(wb.save(url), qPrintable(wb.lastError()));
    QVERIFY(!wb.isModified());
    QCOMPARE(wb.fileUrl(), url);
    Workbook opened;
    QVERIFY2(opened.open(url), qPrintable(opened.lastError()));
    QCOMPARE(opened.serialize(), wb.serialize());
    QVERIFY(!opened.isModified());
    QVERIFY(put(wb, wb.sheetIdAt(1), 7, 0, QStringLiteral("changed")));
    QVERIFY(wb.save(url)); // replaces the file it wrote
    QVERIFY(opened.open(url));
    QCOMPARE(opened.cellDisplay(opened.sheetIdAt(1), 7, 0), QStringLiteral("changed"));
    {
        QFile file(url.toLocalFile());
        QVERIFY(file.open(QIODevice::Append));
        file.write(" ");
    }
    QVERIFY(put(wb, wb.sheetIdAt(1), 8, 0, QStringLiteral("again")));
    QVERIFY(!wb.save(url)); // changed on disk since this workbook wrote it
    QVERIFY(wb.isModified());
    Workbook stranger;
    QVERIFY(!stranger.save(url));
    QVERIFY(stranger.save(url, true));

    const QByteArray intact = opened.serialize();
    const QByteArray good = wb.serialize();
    QVERIFY(!opened.restore("{"));
    QVERIFY(opened.lastError().contains(QStringLiteral("Invalid workbook JSON")));
    QVERIFY(!opened.restore(QByteArray(good).replace("\"version\":1", "\"version\":2")));
    QVERIFY(!opened.restore(QByteArray(good).replace("\"name\":\"Summary\"", "\"name\":\"budget 2026\"")));
    QVERIFY(!opened.restore(QByteArray(good).replace("=SUM(B2:B3)", "=SUM(B2:B3")));
    QVERIFY(opened.lastError().contains(QStringLiteral("cell B5")));
    QVERIFY(!opened.restore(QByteArray(good).replace("\"A2\",\"s\"", "\"A2\",\"q\"")));
    QVERIFY(!opened.restore(QByteArray(good).replace("\"sheets\"", "\"extra\":1,\"sheets\"")));
    QCOMPARE(opened.serialize(), intact);
    QVERIFY(!opened.open(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.qtkwb")))));
    QCOMPARE(opened.serialize(), intact);
}

void WorkbookHistoryTest::formatRegistryImportAndProviderExport()
{
    QTemporaryDir dir;
    Workbook wb;
    populate(wb);
    FormatRegistry registry;
    QVERIFY(wb.registerFormat(&registry));
    QVERIFY(!wb.registerFormat(&registry));
    QVERIFY(!wb.registerFormat(&wb));
    bool listed = false;
    for (const QVariant &format : registry.formatsForOperation(QStringLiteral("roundtrip")))
        listed = listed || format.toMap().value("formatId") == QStringLiteral("qindatk.workbook.v1");
    QVERIFY(listed);
    const QUrl url = QUrl::fromLocalFile(dir.filePath(QStringLiteral("import.qtkwb")));
    QVERIFY(wb.save(url));
    QCOMPARE(registry.detectImport(url), QStringLiteral("qindatk.workbook.v1"));
    const QVariantMap plan = registry.preflightImport(url, QStringLiteral("qindatk.workbook.v1"));
    QVERIFY2(plan.value("ok").toBool(), qPrintable(plan.value("error").toMap().value("message").toString()));
    QCOMPARE(plan.value("preview").toMap().value("sheetCount").toInt(), 2);
    QCOMPARE(plan.value("preview").toMap().value("formulaCount").toInt(), 7);
    const QString job = registry.startImport(url, QStringLiteral("qindatk.workbook.v1"),
                                             plan.value("options").toMap());
    QVERIFY(!job.isEmpty());
    QTRY_COMPARE(registry.jobs()->job(job).value("state").toString(), QStringLiteral("succeeded"));
    const QByteArray imported = registry.jobs()->job(job).value("result").toMap().value("documentBytes").toByteArray();
    Workbook adopted;
    QVERIFY(adopted.restore(imported));
    QCOMPARE(adopted.serialize(), wb.serialize());

    WorkbookFormat provider;
    const auto snapshot = wb.snapshot();
    QVERIFY(!provider.preflightExport(snapshot, {}).error);
    CancellationSource cancel;
    const FormatPayload exported = provider.exportDocument(snapshot, {}, cancel.token(), [](double) {});
    QVERIFY(!exported.error);
    QCOMPARE(exported.bytes, wb.serialize());
    cancel.cancel();
    QCOMPARE(provider.exportDocument(snapshot, {}, cancel.token(), [](double) {}).error.code, ErrorCode::Cancelled);
    const QUrl broken = QUrl::fromLocalFile(dir.filePath(QStringLiteral("broken.qtkwb")));
    {
        QFile file(broken.toLocalFile());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"format\":\"qindatk.workbook\",\"version\":1}");
    }
    QVERIFY(!registry.preflightImport(broken, QStringLiteral("qindatk.workbook.v1")).value("ok").toBool());
}

QTEST_MAIN(WorkbookHistoryTest)
#include "tst_workbook_history.moc"
