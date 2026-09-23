// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_sheet_adapter.h"
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;

namespace {
QVariantList matrix(std::initializer_list<QVariantList> rows)
{
    QVariantList result;
    for (const QVariantList &row : rows) result.append(QVariant(row));
    return result;
}
bool edit(EditableGridModel *grid, const QString &rowId, const QString &columnId, const QVariant &value)
{
    if (!grid->beginEdit(QStringLiteral("Edit cell"))) return false;
    if (!grid->stageCell(rowId, columnId, value)) { grid->cancelEdit(); return false; }
    return grid->commitEdit();
}
}

class WorkbookAdapterTest : public QObject {
    Q_OBJECT
private slots:
    void gridShowsDisplayValuesOnDemand();
    void gridEditsAndPastesBecomeWorkbookTransactions();
    void structuralChangesMoveStableGridIds();
    void sheetSwitchRemovalAndReset();
    void fillCopyAndTabularText();
    void largeSheetVisibleWorkIsBounded();
};

void WorkbookAdapterTest::gridShowsDisplayValuesOnDemand()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(wb.setCellInput(s, 0, 0, QStringLiteral("2")));
    QVERIFY(wb.setCellInput(s, 1, 0, QStringLiteral("=A1*10")));
    WorkbookSheetAdapter adapter;
    adapter.setWorkbook(&wb);
    EditableGridModel *grid = adapter.gridModel();
    QCOMPARE(adapter.sheetId(), s);
    QCOMPARE(grid->rowCount(), 1000);
    QCOMPARE(grid->columnCount(), 26);
    QVERIFY(grid->externalUndo());
    QCOMPARE(grid->rowIdAt(0), QStringLiteral("r:1"));
    adapter.resetSourceReads();
    const QVariantList cells = grid->visibleCells(0, 3, 0, 2);
    QCOMPARE(cells.size(), 6);
    QCOMPARE(adapter.sourceReadCount(), 6);
    QCOMPARE(cells[0].toMap().value("value").toString(), QStringLiteral("2"));
    QCOMPARE(cells[2].toMap().value("value").toString(), QStringLiteral("20"));
    QVERIFY(!cells[1].toMap().value("value").isValid());
    QCOMPARE(adapter.inputAt(1, 0), QStringLiteral("=A1*10"));
    QCOMPARE(adapter.displayAt(1, 0), QStringLiteral("20"));
    QSignalSpy dataChanged(grid, &QAbstractItemModel::dataChanged);
    QSignalSpy cellsChanged(&adapter, &WorkbookSheetAdapter::cellsChanged);
    QVERIFY(wb.setCellInput(s, 0, 0, QStringLiteral("3")));
    QVERIFY(dataChanged.size() >= 1);
    QCOMPARE(cellsChanged.last().at(0).toInt(), 0);
    QCOMPARE(cellsChanged.last().at(2).toInt(), 1); // A1 edited, A2 recalculated
    QCOMPARE(grid->cellValue(QStringLiteral("r:2"), QStringLiteral("c:1")).toString(), QStringLiteral("30"));
    QCOMPARE(grid->sparseCellCount(), 0);
}

void WorkbookAdapterTest::gridEditsAndPastesBecomeWorkbookTransactions()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(wb.setCellInput(s, 1, 0, QStringLiteral("=A1*10")));
    WorkbookSheetAdapter adapter;
    adapter.setWorkbook(&wb);
    EditableGridModel *grid = adapter.gridModel();
    const qint64 steps = wb.statistics().value("historySteps").toLongLong();
    QVERIFY2(edit(grid, "r:1", "c:1", QStringLiteral("=5+1")), qPrintable(grid->lastError()));
    QCOMPARE(wb.cellInput(s, 0, 0), QStringLiteral("=5+1"));
    QCOMPARE(wb.cellDisplay(s, 1, 0), QStringLiteral("60"));
    QCOMPARE(grid->sparseCellCount(), 0); // no second store in the grid
    QCOMPARE(grid->cellValue("r:1", "c:1").toString(), QStringLiteral("6"));
    QVERIFY(!grid->canUndo());
    QCOMPARE(wb.statistics().value("historySteps").toLongLong(), steps + 1);
    QCOMPARE(wb.undoText(), QStringLiteral("Edit cell"));

    const QString revision = wb.revision();
    QVERIFY(!edit(grid, "r:1", "c:1", QStringLiteral("=SUM(")));
    QVERIFY(grid->lastError().startsWith(QStringLiteral("A1: ")));
    QCOMPARE(adapter.lastErrorDetail().value("start").toInt(), 5);
    QCOMPARE(adapter.lastErrorDetail().value("address").toString(), QStringLiteral("A1"));
    QCOMPARE(wb.revision(), revision);
    QCOMPARE(wb.cellInput(s, 0, 0), QStringLiteral("=5+1"));

    QVERIFY(grid->paste(3, 0, matrix({{QStringLiteral("1"), QStringLiteral("=A4+1")},
                                      {QStringLiteral("=B4*2"), QStringLiteral("x")}})));
    QCOMPARE(wb.cellDisplay(s, 4, 0), QStringLiteral("4"));
    QCOMPARE(wb.undoText(), QStringLiteral("Paste cells"));
    QCOMPARE(grid->sparseCellCount(), 0);
    const QString pasted = wb.revision();
    QVERIFY(!grid->paste(0, 0, matrix({{QStringLiteral("1"), QStringLiteral("=1+")}})));
    QCOMPARE(wb.revision(), pasted);
    QCOMPARE(wb.cellInput(s, 0, 0), QStringLiteral("=5+1"));
    QVERIFY(wb.undo());
    QCOMPARE(grid->cellValue("r:5", "c:1"), QVariant());

    // AGENT-NOTE: the grid records an edit only when the typed text differs
    // from its displayed value. Replacing formula =5+1 (shown as 6) with the
    // literal 6 is dropped by the grid, so SheetView also routes every
    // editCommitted value through commitInput (docs/workbook.md gap G-1).
    QVERIFY(edit(grid, "r:1", "c:1", QStringLiteral("6")));
    QCOMPARE(wb.cellInput(s, 0, 0), QStringLiteral("=5+1"));
    QVERIFY(adapter.commitInput(0, 0, QStringLiteral("6")));
    QCOMPARE(wb.cellInput(s, 0, 0), QStringLiteral("6"));
    QVERIFY(adapter.commitInput(0, 0, QStringLiteral("6"))); // no-op, no history
    QCOMPARE(wb.undoText(), QStringLiteral("Edit A1"));
}

void WorkbookAdapterTest::structuralChangesMoveStableGridIds()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    WorkbookSheetAdapter adapter;
    adapter.setWorkbook(&wb);
    EditableGridModel *grid = adapter.gridModel();
    QVERIFY(grid->setRange("r:2", "c:2", "r:3", "c:3"));
    QVERIFY(wb.setCellInput(s, 1, 1, QStringLiteral("kept")));
    QVERIFY(wb.insertRows(s, 0, 2));
    QCOMPARE(grid->rowCount(), 1002);
    QCOMPARE(grid->rowIdAt(0), QStringLiteral("r:1001"));
    QCOMPARE(grid->rowIdAt(2), QStringLiteral("r:1"));
    QCOMPARE(grid->ranges().first().toMap().value("firstRow").toInt(), 3);
    QCOMPARE(grid->cellValue("r:2", "c:2").toString(), QStringLiteral("kept"));
    QVERIFY(wb.insertColumns(s, 0, 1));
    QCOMPARE(grid->columnIdAt(0), QStringLiteral("c:27"));
    QCOMPARE(grid->cellValue("r:2", "c:2").toString(), QStringLiteral("kept"));
    QVERIFY(wb.undo());
    QVERIFY(wb.undo());
    QCOMPARE(grid->rowCount(), 1000);
    QCOMPARE(grid->columnCount(), 26);
    QCOMPARE(grid->rowIdAt(0), QStringLiteral("r:1"));
    QCOMPARE(grid->cellValue("r:2", "c:2").toString(), QStringLiteral("kept"));
    QVERIFY(wb.removeRows(s, 0, 1));
    QCOMPARE(grid->rowIdAt(0), QStringLiteral("r:2"));
    QCOMPARE(grid->ranges().size(), 1);
}

void WorkbookAdapterTest::sheetSwitchRemovalAndReset()
{
    Workbook wb;
    const QString first = wb.sheetIdAt(0);
    const QString second = wb.addSheet(QStringLiteral("Other"), -1, 50, 5);
    QVERIFY(wb.setCellInput(second, 0, 0, QStringLiteral("other")));
    WorkbookSheetAdapter adapter;
    adapter.setSheetId(second); // before the workbook: kept when valid
    adapter.setWorkbook(&wb);
    QCOMPARE(adapter.sheetId(), second);
    QCOMPARE(adapter.sheetName(), QStringLiteral("Other"));
    QCOMPARE(adapter.gridModel()->rowCount(), 50);
    QCOMPARE(adapter.gridModel()->columnCount(), 5);
    QCOMPARE(adapter.gridModel()->cellValue("r:1", "c:1").toString(), QStringLiteral("other"));
    adapter.setSheetId(QStringLiteral("missing"));
    QCOMPARE(adapter.sheetId(), second);
    QVERIFY(!adapter.lastError().isEmpty());
    QSignalSpy sheetChanged(&adapter, &WorkbookSheetAdapter::sheetIdChanged);
    QVERIFY(wb.renameSheet(second, QStringLiteral("Renamed")));
    QCOMPARE(adapter.sheetName(), QStringLiteral("Renamed"));
    QVERIFY(wb.removeSheet(second));
    QCOMPARE(adapter.sheetId(), first);
    QCOMPARE(adapter.gridModel()->rowCount(), 1000);
    QVERIFY(sheetChanged.size() >= 1);
    QVERIFY(wb.undo());
    adapter.setSheetId(second);
    QCOMPARE(adapter.gridModel()->cellValue("r:1", "c:1").toString(), QStringLiteral("other"));
    wb.reset();
    QCOMPARE(adapter.sheetId(), wb.sheetIdAt(0));
    QCOMPARE(adapter.gridModel()->rowCount(), 1000);
    QCOMPARE(adapter.gridModel()->cellValue("r:1", "c:1"), QVariant());
}

void WorkbookAdapterTest::fillCopyAndTabularText()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    WorkbookSheetAdapter adapter;
    adapter.setWorkbook(&wb);
    QVERIFY(adapter.commitInput(0, 0, QStringLiteral("1")));
    QVERIFY(adapter.commitInput(0, 1, QStringLiteral("=A1*2")));
    QVERIFY(adapter.commitInput(1, 0, QStringLiteral("5")));
    QVERIFY(adapter.fillDown(1, 1, 1, 1)); // single row: copy from the row above
    QCOMPARE(adapter.inputAt(1, 1), QStringLiteral("=A2*2"));
    QCOMPARE(adapter.displayAt(1, 1), QStringLiteral("10"));
    QVERIFY(adapter.fillDown(1, 1, 4, 1));
    QCOMPARE(adapter.inputAt(4, 1), QStringLiteral("=A5*2"));
    QVERIFY(adapter.fillRight(0, 1, 0, 3));
    QCOMPARE(adapter.inputAt(0, 3), QStringLiteral("=C1*2"));
    QCOMPARE(adapter.displayAt(0, 3), QStringLiteral("8"));
    QVERIFY(!adapter.fillDown(0, 0, 0, 0));
    QVERIFY(adapter.copyRange(1, 1, 1, 1));
    QCOMPARE(adapter.copiedRange(), QStringLiteral("B2"));
    QVERIFY(adapter.pasteCopied(4, 2));
    QCOMPARE(adapter.inputAt(4, 2), QStringLiteral("=B5*2"));
    QVERIFY(adapter.clearRange(0, 0, 4, 3));
    QCOMPARE(wb.statistics().value("cells").toLongLong(), 0);
    const QVariantList rows = adapter.parseTabularText(QStringLiteral("1\t=A1+1\r\nx\ty\n"));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].toList().at(1).toString(), QStringLiteral("=A1+1"));
    QVERIFY(adapter.gridModel()->paste(0, 0, rows));
    QCOMPARE(wb.cellDisplay(s, 0, 1), QStringLiteral("2"));
}

void WorkbookAdapterTest::largeSheetVisibleWorkIsBounded()
{
    Workbook wb;
    const QString large = wb.addSheet(QStringLiteral("Large"), -1, 100000, 100);
    QVERIFY(wb.beginTransaction(QStringLiteral("Seed")));
    for (int i = 0; i < 1000; ++i) {
        QVERIFY(wb.setCellInput(large, i * 97, i % 100, QString::number(i)));
        QVERIFY(wb.setCellInput(large, i * 97 + 1, i % 100, QStringLiteral("=%1*2").arg(wb.address(i * 97, i % 100))));
    }
    QVERIFY(wb.commitTransaction());
    WorkbookSheetAdapter adapter;
    adapter.setWorkbook(&wb);
    QElapsedTimer timer;
    timer.start();
    adapter.setSheetId(large);
    const qint64 setupMs = timer.elapsed();
    EditableGridModel *grid = adapter.gridModel();
    QCOMPARE(grid->rowCount(), 100000);
    QCOMPARE(grid->columnCount(), 100);
    QCOMPARE(grid->sparseCellCount(), 0);
    adapter.resetSourceReads();
    timer.restart();
    const QVariantList window = grid->visibleCells(51000, 32, 45, 20);
    const qint64 windowUs = timer.nsecsElapsed() / 1000;
    QCOMPARE(window.size(), 640);
    QCOMPARE(adapter.sourceReadCount(), 640);
    QCOMPARE(grid->visibleCells(0, 100000, 0, 100).size(), 0);
    QCOMPARE(adapter.sourceReadCount(), 640);
    timer.restart();
    QVERIFY(wb.setCellInput(large, 0, 0, QStringLiteral("7")));
    const qint64 editUs = timer.nsecsElapsed() / 1000;
    QCOMPARE(wb.statistics().value("lastEvaluated").toLongLong(), 1);
    qInfo().noquote() << QStringLiteral("METRIC adapter.100000x100.setupMs %1").arg(setupMs);
    qInfo().noquote() << QStringLiteral("METRIC adapter.window32x20.sourceReads %1").arg(adapter.sourceReadCount());
    qInfo().noquote() << QStringLiteral("METRIC adapter.window32x20.us %1").arg(windowUs);
    qInfo().noquote() << QStringLiteral("METRIC adapter.edit.us %1").arg(editUs);
}

QTEST_MAIN(WorkbookAdapterTest)
#include "tst_workbook_adapter.moc"
