// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook.h"
#include <QElapsedTimer>
#include <QFile>
#include <QtTest>

using namespace QindaTK;

// AGENT-NOTE: local measurements for docs/universal-toolkit/implementation/
// evidence/WP-055.md. Evaluation counts are asserted exactly; times and
// memory are reported as METRIC lines, never asserted as budgets.
namespace {
qint64 statusKiB(const char *field)
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray &line : status.readAll().split('\n'))
        if (line.startsWith(field)) return line.mid(qstrlen(field)).trimmed().split(' ').first().toLongLong();
    return -1;
}
void metric(const char *name, qint64 value)
{ qInfo().noquote() << QStringLiteral("METRIC %1 %2").arg(QLatin1String(name)).arg(value); }
qint64 evaluated(Workbook &wb) { return wb.statistics().value(QStringLiteral("lastEvaluated")).toLongLong(); }
}

class WorkbookScaleTest : public QObject {
    Q_OBJECT
private slots:
    void sparseMillionRowSheet();
    void longChainAndLongCycle();
};

void WorkbookScaleTest::sparseMillionRowSheet()
{
    constexpr int Values = 20000, Stride = 50, FanOut = 5000, Rows = 1000000;
    Workbook wb;
    const QString s = wb.addSheet(QStringLiteral("Sparse"), -1, Rows, 100);
    const qint64 rssStart = statusKiB("VmRSS:");
    QElapsedTimer timer;
    timer.start();
    QVERIFY(wb.beginTransaction(QStringLiteral("Seed")));
    for (int i = 0; i < Values; ++i) {
        const int row = i * Stride;
        QVERIFY(wb.setCellInput(s, row, 0, QString::number(i + 1)));
        QVERIFY(wb.setCellInput(s, row, 1, QStringLiteral("=A%1*2").arg(row + 1)));
    }
    QVERIFY(wb.setCellInput(s, 1, 2, QStringLiteral("=SUM(B1:B%1)").arg(Rows)));
    for (int i = 0; i < FanOut; ++i)
        QVERIFY(wb.setCellInput(s, i, 3, QStringLiteral("=$A$1+%1").arg(i)));
    const qint64 stageMs = timer.elapsed();
    timer.restart();
    QVERIFY(wb.commitTransaction());
    const qint64 commitMs = timer.elapsed();
    QCOMPARE(evaluated(wb), qint64(Values + 1 + FanOut));
    QCOMPARE(wb.cellDisplay(s, 1, 2), QStringLiteral("400020000"));
    const qint64 rssSeeded = statusKiB("VmRSS:");

    timer.restart();
    QVERIFY(wb.setCellInput(s, 0, 0, QStringLiteral("1000")));
    const qint64 fanOutUs = timer.nsecsElapsed() / 1000;
    QCOMPARE(evaluated(wb), qint64(2 + FanOut));
    QCOMPARE(wb.cellDisplay(s, 1, 2), QStringLiteral("400021998"));
    QCOMPARE(wb.cellDisplay(s, FanOut - 1, 3), QStringLiteral("5999"));
    timer.restart();
    QVERIFY(wb.setCellInput(s, 6, 25, QStringLiteral("1")));
    const qint64 unrelatedUs = timer.nsecsElapsed() / 1000;
    QCOMPARE(evaluated(wb), 0);
    timer.restart();
    QVERIFY(wb.setCellInput(s, 500000, 0, QStringLiteral("10002")));
    const qint64 deepEditUs = timer.nsecsElapsed() / 1000;
    QCOMPARE(evaluated(wb), 2);
    QCOMPARE(wb.cellDisplay(s, 1, 2), QStringLiteral("400022000"));

    timer.restart();
    const QVariantList window = wb.visibleCells(s, 500000, 40, 0, 20);
    const qint64 windowUs = timer.nsecsElapsed() / 1000;
    QCOMPARE(window.size(), 2);
    QCOMPARE(wb.visibleCells(s, 0, 100, 0, 100).size(), 0); // over the 4096-cell cap

    timer.restart();
    QVERIFY(wb.insertRows(s, 0, 1));
    const qint64 insertMs = timer.elapsed();
    QCOMPARE(evaluated(wb), 0);
    QCOMPARE(wb.cellInput(s, 2, 2), QStringLiteral("=SUM(B2:B1000001)"));
    QCOMPARE(wb.cellInput(s, 2, 3), QStringLiteral("=$A$2+1"));
    timer.restart();
    QVERIFY(wb.undo());
    const qint64 undoInsertMs = timer.elapsed();
    QCOMPARE(wb.cellInput(s, 1, 2), QStringLiteral("=SUM(B1:B1000000)"));
    timer.restart();
    QVERIFY(wb.removeRows(s, 0, 1));
    const qint64 removeMs = timer.elapsed();
    QCOMPARE(evaluated(wb), qint64(FanOut)); // C2's range shrank; 4999 $A$1 became #REF!
    QCOMPARE(wb.cellDisplay(s, 0, 3), QStringLiteral("#REF!"));
    QVERIFY(wb.undo());
    QCOMPARE(wb.cellDisplay(s, 1, 2), QStringLiteral("400022000"));

    timer.restart();
    const QByteArray bytes = wb.serialize();
    const qint64 serializeMs = timer.elapsed();
    Workbook reopened;
    timer.restart();
    QVERIFY(reopened.restore(bytes));
    const qint64 restoreMs = timer.elapsed();
    QCOMPARE(reopened.serialize(), bytes);
    QCOMPARE(reopened.cellDisplay(s, 1, 2), QStringLiteral("400022000"));

    const QVariantMap stats = wb.statistics();
    metric("scale.cells", stats.value("cells").toLongLong());
    metric("scale.formulas", stats.value("formulas").toLongLong());
    metric("scale.dependencyCells", stats.value("dependencyCells").toLongLong());
    metric("scale.dependencyRanges", stats.value("dependencyRanges").toLongLong());
    metric("scale.stageMs", stageMs);
    metric("scale.commitMs", commitMs);
    metric("scale.fanOutEditUs", fanOutUs);
    metric("scale.unrelatedEditUs", unrelatedUs);
    metric("scale.deepEditUs", deepEditUs);
    metric("scale.window40x20Us", windowUs);
    metric("scale.insertRowMs", insertMs);
    metric("scale.undoInsertMs", undoInsertMs);
    metric("scale.removeRowMs", removeMs);
    metric("scale.serializeMs", serializeMs);
    metric("scale.serializedBytes", bytes.size());
    metric("scale.restoreMs", restoreMs);
    metric("scale.rssStartKiB", rssStart);
    metric("scale.rssSeededKiB", rssSeeded);
    metric("scale.peakRssKiB", statusKiB("VmHWM:"));
}

void WorkbookScaleTest::longChainAndLongCycle()
{
    constexpr int Length = 50000;
    Workbook wb;
    const QString s = wb.addSheet(QStringLiteral("Chain"), -1, Length + 10, 4);
    QVariantList rows;
    rows.append(QVariant(QVariantList{QStringLiteral("1")}));
    for (int r = 1; r < Length; ++r) rows.append(QVariant(QVariantList{QStringLiteral("=A%1+1").arg(r)}));
    QElapsedTimer timer;
    timer.start();
    QVERIFY(wb.pasteInputs(s, 0, 0, rows));
    const qint64 pasteMs = timer.elapsed();
    QCOMPARE(evaluated(wb), qint64(Length - 1));
    QCOMPARE(wb.cellDisplay(s, Length - 1, 0), QString::number(Length));
    timer.restart();
    QVERIFY(wb.setCellInput(s, 0, 0, QStringLiteral("10")));
    const qint64 chainUs = timer.nsecsElapsed() / 1000;
    QCOMPARE(evaluated(wb), qint64(Length - 1));
    QCOMPARE(wb.cellDisplay(s, Length - 1, 0), QString::number(Length + 9));
    timer.restart();
    QVERIFY(wb.setCellInput(s, 0, 0, QStringLiteral("=A%1+1").arg(Length)));
    const qint64 cycleUs = timer.nsecsElapsed() / 1000;
    QCOMPARE(wb.statistics().value("lastCycleCells").toLongLong(), qint64(Length));
    QCOMPARE(wb.cellDisplay(s, Length / 2, 0), QStringLiteral("#CYCLE!"));
    QVERIFY(wb.setCellInput(s, 0, 1, QStringLiteral("=A1")));
    QCOMPARE(wb.cellDisplay(s, 0, 1), QStringLiteral("#CYCLE!"));
    timer.restart();
    QVERIFY(wb.undo() && wb.undo());
    const qint64 breakUs = timer.nsecsElapsed() / 1000;
    QCOMPARE(wb.cellDisplay(s, Length - 1, 0), QString::number(Length + 9));
    metric("chain.length", Length);
    metric("chain.pasteMs", pasteMs);
    metric("chain.editUs", chainUs);
    metric("chain.closeCycleUs", cycleUs);
    metric("chain.breakCycleUs", breakUs);
}

QTEST_GUILESS_MAIN(WorkbookScaleTest)
#include "tst_workbook_scale.moc"
