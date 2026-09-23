// SPDX-License-Identifier: LGPL-3.0-or-later
#include "editable_grid_model.h"
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;

namespace {
qint64 rssKiB()
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray &line : status.readAll().split('\n'))
        if (line.startsWith("VmRSS:")) return line.mid(6).trimmed().split(' ').first().toLongLong();
    return -1;
}

// A domain source whose edit text differs from its displayed value, like a
// spreadsheet cell holding "=5+1" and showing "6".
class DomainSource final : public GridCellSource, public GridCellSourceExtension {
public:
    using GridCellSource::GridCellSource;
    QHash<QString, QString> inputs;
    QVector<QVariantList> commits;
    QVector<std::pair<QRect, QRect>> fills;
    bool rejectNext = false;
    bool supportFill = true;
    static QString key(const QString &r, const QString &c) { return r + QLatin1Char('/') + c; }
    QVariant value(const QString &r, const QString &c) const override
    {
        const QString input = inputs.value(key(r, c));
        return input.startsWith(QLatin1Char('=')) ? QVariant(QStringLiteral("computed")) : QVariant(input);
    }
    QVariant editValue(const QString &r, const QString &c) const override { return inputs.value(key(r, c)); }
    QVariantMap cellStyle(const QString &r, const QString &) const override
    { return r == QLatin1String("r:1") ? QVariantMap{{"horizontalAlignment", "right"}, {"bold", true}} : QVariantMap{}; }
    QVector<QRect> mergedRanges(const QRect &window) const override
    {
        const QRect merge(1, 1, 2, 3); // B2:C4
        return window.intersects(merge) ? QVector<QRect>{merge} : QVector<QRect>{};
    }
    bool commitChanges(const QString &, const QVariantList &changes, QString *error) override
    {
        if (rejectNext) { rejectNext = false; *error = QStringLiteral("domain says no"); return false; }
        for (const QVariant &item : changes) {
            const QVariantMap change = item.toMap();
            inputs.insert(key(change.value("rowId").toString(), change.value("columnId").toString()),
                          change.value("after").toString());
        }
        commits.append(changes);
        return true;
    }
    bool fillCells(const QRect &source, const QRect &target, QString *) override
    {
        if (!supportFill) return false;
        fills.append({source, target});
        return true;
    }
};
}

class EditableGridDomainTest : public QObject {
    Q_OBJECT
private slots:
    void axisIdsCompressGeneratedRuns();
    void millionRowGridStaysCompact();
    void sourceCommitsKeepEditTextDistinctFromDisplay();
    void notificationsStylesMergesAndFills();
};

void EditableGridDomainTest::axisIdsCompressGeneratedRuns()
{
    QString prefix;
    quint64 number = 0;
    QVERIFY(GridAxisIds::parseGenerated(QStringLiteral("r:12"), &prefix, &number));
    QCOMPARE(prefix, QStringLiteral("r:"));
    QCOMPARE(number, quint64(12));
    QVERIFY(!GridAxisIds::parseGenerated(QStringLiteral("r:012"), nullptr, nullptr));
    QVERIFY(!GridAxisIds::parseGenerated(QStringLiteral("row12"), nullptr, nullptr));
    GridAxisIds ids;
    ids.assign({"r:1", "r:2", "r:3", "alpha", "beta", "r:10", "r:11"});
    QCOMPARE(ids.size(), 7);
    QCOMPARE(ids.segmentCount(), 3);
    QCOMPARE(ids.indexOf("beta"), 4);
    QCOMPARE(ids.indexOf("r:11"), 6);
    QCOMPARE(ids.indexOf("r:4"), -1);
    ids.insertGenerated(3, "r:", 4, 3); // r:4..r:6 continue the first run
    QCOMPARE(ids.segmentCount(), 3);
    QCOMPARE(ids.at(5), QStringLiteral("r:6"));
    QCOMPARE(ids.indexOf("alpha"), 6);
    ids.remove(1, 2);
    QCOMPARE(ids.toList(), QStringList({"r:1", "r:4", "r:5", "r:6", "alpha", "beta", "r:10", "r:11"}));
    ids.move(4, 0);
    QCOMPARE(ids.at(0), QStringLiteral("alpha"));
    QCOMPARE(ids.indexOf("r:1"), 1);
    QVERIFY(ids.overlapsGenerated("r:", 9, 2));
    QCOMPARE(ids.nextFreeGenerated("r:", 3, 3), quint64(7));
    QCOMPARE(ids.nextFreeGenerated("c:", 1, 5), quint64(1));
}

void EditableGridDomainTest::millionRowGridStaysCompact()
{
    const qint64 before = rssKiB();
    EditableGridModel grid;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(grid.setDimensions(1048576, 16384));
    const qint64 setupMs = timer.elapsed();
    QCOMPARE(grid.rowCount(), 1048576);
    QCOMPARE(grid.rowIdAt(1048575), QStringLiteral("r:1048576"));
    QCOMPARE(grid.rowIndex(QStringLiteral("r:524288")), 524287);
    QVERIFY(grid.setRange("r:10", "c:2", "r:20", "c:3"));
    QVERIFY(grid.requestRowResize("r:15", 40));
    QVERIFY(grid.insertGeneratedRowsAt(0, 2));
    QCOMPARE(grid.rowIdAt(0), QStringLiteral("r:1048577"));
    QCOMPARE(grid.rowIndex("r:10"), 11);
    QCOMPARE(grid.ranges().first().toMap().value("firstRow").toInt(), 11);
    QVERIFY(grid.insertRowsAt(5, {"custom-a", "custom-b"}));
    QCOMPARE(grid.rowIndex("custom-b"), 6);
    QVERIFY(grid.removeRowsAt(0, 3));
    QCOMPARE(grid.rowIdAt(0), QStringLiteral("r:2"));
    QCOMPARE(grid.cellRect(grid.rowIndex("r:15"), 0, 0, 0).height(), 40.0);
    grid.setDefaultRowSize(30);
    QCOMPARE(grid.cellRect(grid.rowIndex("r:15"), 0, 0, 0).height(), 40.0);
    QCOMPARE(grid.cellRect(grid.rowIndex("r:16"), 0, 0, 0).height(), 30.0);
    QCOMPARE(grid.visibleCells(900000, 32, 16000, 20).size(), 640);
    const qint64 after = rssKiB();
    qInfo().noquote() << QStringLiteral("METRIC grid.1048576x16384.setupMs %1").arg(setupMs);
    qInfo().noquote() << QStringLiteral("METRIC grid.1048576x16384.rssDeltaKiB %1").arg(after - before);
    QVERIFY2(after - before < 64 * 1024, "a compact axis must not cost O(rows) strings");
}

void EditableGridDomainTest::sourceCommitsKeepEditTextDistinctFromDisplay()
{
    EditableGridModel grid;
    DomainSource source;
    source.inputs.insert(DomainSource::key("r:1", "c:1"), QStringLiteral("=5+1"));
    grid.setSource(&source);
    QVERIFY(grid.setDimensions(4, 4));
    grid.setSourceCommits(true);
    QCOMPARE(grid.cellValue("r:1", "c:1").toString(), QStringLiteral("computed"));
    QCOMPARE(grid.editValue("r:1", "c:1").toString(), QStringLiteral("=5+1"));
    QSignalSpy committed(&grid, &EditableGridModel::committed);
    // The typed literal equals the displayed value but not the edit text.
    QVERIFY(grid.beginEdit());
    QVERIFY(grid.stageCell("r:1", "c:1", QStringLiteral("computed")));
    QVERIFY(grid.commitEdit());
    QCOMPARE(source.commits.size(), 1);
    QCOMPARE(source.inputs.value(DomainSource::key("r:1", "c:1")), QStringLiteral("computed"));
    QCOMPARE(committed.size(), 1);
    QCOMPARE(grid.sparseCellCount(), 0);
    QVERIFY(!grid.canUndo());
    QVERIFY(!grid.undo());
    source.rejectNext = true;
    QVERIFY(grid.beginEdit());
    QVERIFY(grid.stageCell("r:2", "c:2", QStringLiteral("x")));
    QVERIFY(!grid.commitEdit());
    QCOMPARE(grid.lastError(), QStringLiteral("domain says no"));
    QVERIFY(grid.editing()); // the typed value is still open for correction
    grid.cancelEdit();
    QVERIFY(grid.paste(0, 0, {QVariant(QVariantList{"p", "q"})}));
    QCOMPARE(source.commits.size(), 2);
    QCOMPARE(source.commits.last().size(), 2);
    grid.setSourceCommits(false);
    QVERIFY(grid.beginEdit());
    QVERIFY(grid.stageCell("r:3", "c:3", QStringLiteral("local")));
    QVERIFY(grid.commitEdit());
    QCOMPARE(grid.sparseCellCount(), 1); // plain grids keep their override layer
}

void EditableGridDomainTest::notificationsStylesMergesAndFills()
{
    EditableGridModel grid;
    DomainSource source;
    grid.setSource(&source);
    QVERIFY(grid.setDimensions(10, 6));
    grid.setSourceCommits(true);
    QSignalSpy data(&grid, &QAbstractItemModel::dataChanged);
    const QString revision = grid.revision();
    grid.notifySourceChanged(8, 5, 4, 10);
    QCOMPARE(data.size(), 1);
    QCOMPARE(data.first().at(0).toModelIndex().row(), 8);
    QCOMPARE(data.first().at(1).toModelIndex().row(), 9);
    QCOMPARE(data.first().at(1).toModelIndex().column(), 5);
    QVERIFY(grid.revision() != revision);
    const QVariantList cells = grid.visibleCells(0, 2, 0, 1);
    QCOMPARE(cells[0].toMap().value("style").toMap().value("horizontalAlignment").toString(), QStringLiteral("right"));
    QVERIFY(!cells[1].toMap().contains("style"));
    const QVariantList merges = grid.mergedRegions(0, 3, 0, 2);
    QCOMPARE(merges.size(), 1);
    QCOMPARE(merges.first().toMap().value("lastRow").toInt(), 3);
    QCOMPARE(merges.first().toMap().value("rowId").toString(), QStringLiteral("r:2"));
    QCOMPARE(grid.mergeAt(3, 2).value("firstColumn").toInt(), 1);
    QVERIFY(grid.mergeAt(0, 0).isEmpty());
    QVERIFY(grid.fillRange(0, 0, 1, 1, 1, 0, 4, 1));
    QCOMPARE(source.fills.size(), 1);
    QCOMPARE(source.fills.first().second, QRect(0, 1, 1, 4));
    QVERIFY(grid.fill(0, 0, 1, 2, 5, 0)); // legacy block fill routes to the domain too
    QCOMPARE(source.fills.size(), 2);
    source.supportFill = false;
    source.inputs.insert(DomainSource::key("r:1", "c:6"), QStringLiteral("a"));
    source.inputs.insert(DomainSource::key("r:2", "c:6"), QStringLiteral("b"));
    QVERIFY(grid.fillRange(0, 5, 2, 1, 2, 5, 5, 1)); // unsupported: repeat edit values
    QCOMPARE(source.inputs.value(DomainSource::key("r:3", "c:6")), QStringLiteral("a"));
    QCOMPARE(source.inputs.value(DomainSource::key("r:4", "c:6")), QStringLiteral("b"));
    QCOMPARE(source.inputs.value(DomainSource::key("r:7", "c:6")), QStringLiteral("a"));
    QVERIFY(!grid.fillRange(0, 0, 1, 1, 9, 0, 5, 1));
}

QTEST_GUILESS_MAIN(EditableGridDomainTest)
#include "tst_editable_grid_domain.moc"
