// SPDX-License-Identifier: LGPL-3.0-or-later
#include "editable_grid_model.h"
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;

class LazySource final : public GridCellSource {
public:
    using GridCellSource::GridCellSource;
    mutable int reads = 0;
    QVariant value(const QString &row, const QString &column) const override
    { ++reads; return row + QLatin1Char('/') + column; }
    QString validate(const QString &, const QString &, const QVariant &v) const override
    { return v.toString() == QStringLiteral("INVALID") ? QStringLiteral("Rejected cell") : QString{}; }
};

class GridTest : public QObject {
    Q_OBJECT
private slots:
    void lazyScaleAndBoundedWindow();
    void stableIdentityAndGeometry();
    void atomicEditsAndUndo();
    void hostHistoryBridge();
};

void GridTest::lazyScaleAndBoundedWindow()
{
    EditableGridModel grid;
    LazySource source;
    grid.setSource(&source);
    QElapsedTimer timer; timer.start();
    QVERIFY(grid.setDimensions(100000, 100));
    const qint64 dimensionMs = timer.elapsed();
    QCOMPARE(grid.rowCount(), 100000);
    QCOMPARE(grid.columnCount(), 100);
    QCOMPARE(grid.sparseCellCount(), 0);
    QCOMPARE(source.reads, 0);
    const auto data = grid.visibleCells(51000, 32, 45, 20);
    QCOMPARE(data.size(), 640);
    QCOMPARE(source.reads, 640);
    QCOMPARE(data.first().toMap().value("value").toString(),
             grid.rowIdAt(51000) + QLatin1Char('/') + grid.columnIdAt(45));
    QCOMPARE(grid.visibleCells(0, 100000, 0, 100).size(), 0);
    QCOMPARE(source.reads, 640);
    QVERIFY(grid.setRange(grid.rowIdAt(1), grid.columnIdAt(2),
                          grid.rowIdAt(99999), grid.columnIdAt(99)));
    QCOMPARE(grid.ranges().size(), 1);
    QCOMPARE(grid.sparseCellCount(), 0);
    qInfo() << "100k x 100 dimensions ms" << dimensionMs
            << "visible result" << data.size() << "source reads" << source.reads
            << "sparse overrides" << grid.sparseCellCount();
}

void GridTest::stableIdentityAndGeometry()
{
    EditableGridModel grid;
    QVERIFY(grid.setRowIds({"a", "b", "c"}));
    QVERIFY(grid.setColumnIds({"x", "y", "z"}));
    grid.setFrozenRows(1); grid.setFrozenColumns(1);
    QVERIFY(grid.requestRowResize("b", 40));
    QVERIFY(grid.requestColumnResize("y", 60));
    QCOMPARE(grid.cellRect(1, 1, 25, 10), QRectF(75, 14, 60, 40));
    QCOMPARE(grid.cellRect(0, 0, 25, 10), QRectF(0, 0, 100, 24));
    QCOMPARE(grid.hitTest(110, 30, 25, 10).value("rowId").toString(), QString("b"));
    QVERIFY(grid.setRange("a", "x", "c", "z"));
    QVERIFY(grid.requestColumnMove("z", "x"));
    QCOMPARE(grid.columnIdAt(0), QString("z"));
    QCOMPARE(grid.columnIndex("y"), 2);
    QCOMPARE(grid.ranges().first().toMap().value("anchorColumnId").toString(), QString("x"));
    QVERIFY(grid.setRowIds({"c", "a", "b"}));
    QCOMPARE(grid.rowIndex("a"), 1);
    QCOMPARE(grid.ranges().first().toMap().value("activeRowId").toString(), QString("c"));
    QCOMPARE(grid.cellRect(grid.rowIndex("b"), grid.columnIndex("y"), 0, 0).height(), 40.0);
    grid.setDefaultRowSize(32);
    grid.setDefaultColumnSize(120);
    QCOMPARE(grid.cellRect(grid.rowIndex("a"), grid.columnIndex("x"), 0, 0).height(), 32.0);
    QCOMPARE(grid.cellRect(grid.rowIndex("b"), grid.columnIndex("y"), 0, 0).height(), 40.0);
    QCOMPARE(grid.cellRect(grid.rowIndex("a"), grid.columnIndex("y"), 0, 0).width(), 60.0);
    QVERIFY(grid.setRowIds({"a", "b"}));
    QCOMPARE(grid.ranges().size(), 0);
    const auto regions = grid.visibleRegions(240, 70, 10, 5);
    QVERIFY(regions.size() <= 4);
    QVERIFY(!grid.requestColumnResize("y", -1));
    QVERIFY(!grid.requestColumnMove("missing", "x"));
}

void GridTest::atomicEditsAndUndo()
{
    EditableGridModel grid;
    LazySource source; grid.setSource(&source);
    QVERIFY(grid.setRowIds({"a", "b", "c"}));
    QVERIFY(grid.setColumnIds({"x", "y", "z"}));
    QSignalSpy committed(&grid, &EditableGridModel::committed);
    QVERIFY(!grid.paste(0, 0, {QVariantList{"one", "INVALID"}, QVariantList{"two", "three"}}));
    QCOMPARE(grid.sparseCellCount(), 0);
    QCOMPARE(committed.size(), 0);
    QCOMPARE(grid.cellValue("a", "x").toString(), QString("a/x"));
    QVERIFY(grid.paste(0, 0, {QVariantList{"one", "two"}, QVariantList{"three", "four"}}));
    QCOMPARE(grid.sparseCellCount(), 4);
    QCOMPARE(committed.size(), 1);
    QCOMPARE(committed.first().at(2).toList().size(), 4);
    QVERIFY(grid.canUndo());
    QVERIFY(grid.setRowIds({"c", "a", "b"}));
    QCOMPARE(grid.cellValue("a", "x").toString(), QString("one"));
    QVERIFY(grid.undo());
    QCOMPARE(grid.cellValue("a", "x").toString(), QString("a/x"));
    QCOMPARE(grid.sparseCellCount(), 0);
    QVERIFY(grid.redo());
    QCOMPARE(grid.cellValue("b", "y").toString(), QString("four"));
    QVERIFY(grid.setRowIds({"a", "b", "c"}));
    QVERIFY2(grid.fill(0, 0, 2, 2, 1, 1), qPrintable(grid.lastError()));
    QCOMPARE(grid.cellValue("c", "z").toString(), QString("four"));
    QVERIFY(grid.undo());
    QCOMPARE(grid.cellValue("c", "z").toString(), QString("c/z"));
    QVERIFY(grid.beginEdit());
    QVERIFY(grid.stageCell("a", "x", "draft"));
    grid.cancelEdit();
    QCOMPARE(grid.cellValue("a", "x").toString(), QString("one"));
    QVERIFY(!grid.paste(0, 0, QVariantList{QStringLiteral("bad row")}));
    QCOMPARE(grid.cellValue("a", "x").toString(), QString("one"));
}

void GridTest::hostHistoryBridge()
{
    EditableGridModel grid;
    QVERIFY(grid.setRowIds({"a"})); QVERIFY(grid.setColumnIds({"x"}));
    grid.setExternalUndo(true);
    QSignalSpy committed(&grid, &EditableGridModel::committed);
    QVERIFY2(grid.paste(0, 0, QVariantList{QVariant(QVariantList{"edited"})}),
             qPrintable(grid.lastError()));
    QCOMPARE(committed.size(), 1);
    QVERIFY(!grid.canUndo());
    QVERIFY(!grid.undo());
    const QString id = committed.first().at(0).toString();
    const QVariantList changes = committed.first().at(2).toList();
    const QString rev = grid.revision();
    QVERIFY(!grid.applyHistoryChanges(id, changes, true, "stale"));
    QCOMPARE(grid.cellValue("a", "x").toString(), QString("edited"));
    QVariantList tampered = changes;
    auto record = tampered.first().toMap(); record["after"] = "evil"; tampered[0] = record;
    QVERIFY(!grid.applyHistoryChanges(id, tampered, true, rev));
    QCOMPARE(grid.cellValue("a", "x").toString(), QString("edited"));
    QVERIFY(grid.applyHistoryChanges(id, changes, true, rev));
    QVERIFY(!grid.cellValue("a", "x").isValid());
    QVERIFY(grid.applyHistoryChanges(id, changes, false, grid.revision()));
    QCOMPARE(grid.cellValue("a", "x").toString(), QString("edited"));
    QVERIFY(grid.releaseHistoryTransaction(id));
    QVERIFY(!grid.applyHistoryChanges(id, changes, true, grid.revision()));
}

QTEST_MAIN(GridTest)
#include "tst_editable_grid_model.moc"
