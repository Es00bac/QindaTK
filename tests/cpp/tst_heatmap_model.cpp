// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include "heatmap_model.h"
#include "heatmap_renderer.h"
using namespace QindaTK;
class HeatmapModelTest : public QObject {
    Q_OBJECT
private slots:
    void sparseTilesAndIdentity();
    void categoriesAndCancellation();
    void viewportAndPixels();
    void extremaAtOverviewAndTheme();
};
void HeatmapModelTest::sparseTilesAndIdentity()
{
    HeatmapModel m;
    QVERIFY(m.setDimensions(100000,10000));
    QCOMPARE(m.storedCellCount(),0);
    m.setXMinimum(-20); m.setXMaximum(80); m.setXUnit("Hz");
    m.setYMinimum(0); m.setYMaximum(100); m.setYUnit("s");
    m.setValueUnit("dB"); m.setColorMinimum(-10); m.setColorMaximum(10);
    QVERIFY(m.setCell(0,0,"minimum",-10));
    QVERIFY(m.setCell(99999,9999,"maximum",10));
    QVERIFY(m.setCell(50000,5000,"gap",QVariant()));
    QVERIFY(!m.setCell(1,1,"minimum",2));
    QCOMPARE(m.cellAt(50000,5000).value("missing").toBool(),true);
    QCOMPARE(m.cellAt(0,0).value("xUnit").toString(),"Hz");
    const auto bins=m.tile(0,100000,0,10000,64,64);
    QCOMPARE(bins.size(),4096);
    QCOMPARE(m.storedCellCount(),3);
    QCOMPARE(bins.first().toMap().value("minimum").toDouble(),-10);
    QCOMPARE(bins.last().toMap().value("maximum").toDouble(),10);
    QVERIFY(bins.first().toMap().value("missing").toLongLong()>0);
    QVERIFY(m.tile(0,100000,0,10000,65,64).isEmpty());
}
void HeatmapModelTest::categoriesAndCancellation()
{
    HeatmapModel m; QVERIFY(m.setDimensions(2,2));
    m.setXCategories({"left","right"}); m.setYCategories({"top","bottom"});
    m.setValueCategories({"cold","hot"});
    QVERIFY(m.setCell(0,1,"a",QStringLiteral("hot")));
    QVERIFY(!m.setCell(1,1,"bad",QStringLiteral("other")));
    QCOMPARE(m.cellAt(0,1).value("x").toString(),"right");
    QSignalSpy spy(&m,&HeatmapModel::tileReady);
    m.requestTile(0,2,0,2,2,2); m.requestTile(0,1,0,1,1,1);
    QTRY_COMPARE(spy.size(),1);
    QCOMPARE(spy.first().at(2).toList().size(),1);
    m.requestTile(0,2,0,2,1,1); m.cancelTile();
    QCoreApplication::processEvents(); QCOMPARE(spy.size(),1);
}
void HeatmapModelTest::viewportAndPixels()
{
    HeatmapModel m; QVERIFY(m.setDimensions(2,3));
    QVERIFY(m.setCell(0,0,"lo",0)); QVERIFY(m.setCell(0,1,"hi",10));
    HeatmapRenderer r; r.setModel(&m); r.setLowColor(Qt::black); r.setHighColor(Qt::white);
    r.setMissingColor(Qt::red); r.setBackgroundColor(Qt::blue); r.setTextColor(Qt::white);
    r.setMetrics({{"leftMargin",0},{"rightMargin",0},{"topMargin",0},{"bottomMargin",0},{"fontSize",8}});
    r.setWidth(300); r.setHeight(200);
    const QImage image=r.renderImage({300,200});
    QCOMPARE(image.pixelColor(25,25),QColor(Qt::black));
    QCOMPARE(image.pixelColor(125,25),QColor(Qt::white));
    bool sawMissing=false;
    for (int x=200; x<300; ++x) if (image.pixelColor(x,25)==QColor(Qt::red)) sawMissing=true;
    QVERIFY(sawMissing);
    QVERIFY(r.selectCell(0,1)); QCOMPARE(r.selectedCell().value("id").toString(),"hi");
    QVERIFY(r.navigateSelection(Qt::Key_Down)); QCOMPARE(r.selectedCell().value("row").toInt(),1);
    QVERIFY(r.selectRegion(0,2,0,2));
    QCOMPARE(r.selectedRegion().value("lastRow").toInt(),2);
    QCOMPARE(r.selectedRegion().value("lastColumn").toInt(),2);
    r.zoom(2); QVERIFY(r.horizontalViewFraction()<1);
    QVERIFY(r.paintBinCount()<=4096);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString png=directory.filePath("matrix.png"), svg=directory.filePath("matrix.svg");
    QVERIFY(r.exportPng(png,{300,200})); QVERIFY(r.exportSvg(svg,{300,200}));
    QImage decoded(png); QCOMPARE(decoded.size(),QSize(300,200));
    QFile svgFile(svg); QVERIFY(svgFile.open(QIODevice::ReadOnly));
    QVERIFY(svgFile.readAll().contains("<svg"));
}
void HeatmapModelTest::extremaAtOverviewAndTheme()
{
    HeatmapModel m; QVERIFY(m.setDimensions(1,128));
    m.setColorMinimum(-1); m.setColorMaximum(1);
    QVERIFY(m.setCell(0,0,"minimum",-1));
    QVERIFY(m.setCell(0,1,"maximum",1));
    const auto tile=m.tile(0,1,0,128,1,64);
    QCOMPARE(tile.size(),64);
    QCOMPARE(tile.first().toMap().value("minimum").toDouble(),-1);
    QCOMPARE(tile.first().toMap().value("maximum").toDouble(),1);
    QCOMPARE(tile.first().toMap().value("missing").toInt(),0);
    QVERIFY(tile.at(1).toMap().value("missing").toInt()>0);
    HeatmapRenderer r; r.setModel(&m); r.setLowColor(Qt::black); r.setHighColor(Qt::white);
    r.setMissingColor(Qt::red); r.setBackgroundColor(Qt::blue);
    r.setMetrics({{"leftMargin",0},{"rightMargin",0},{"topMargin",0},{"bottomMargin",0},{"fontSize",8}});
    const QImage dark=r.renderImage({640,80});
    QCOMPARE(dark.pixelColor(1,10),QColor(Qt::black));
    QCOMPARE(dark.pixelColor(6,10),QColor(Qt::white));
    bool sawMissing=false;
    for (int x=10; x<20; ++x) if (dark.pixelColor(x,10)==QColor(Qt::red)) sawMissing=true;
    QVERIFY(sawMissing);
    r.setLowColor(Qt::white); r.setHighColor(Qt::black);
    const QImage light=r.renderImage({640,80});
    QCOMPARE(light.pixelColor(1,10),QColor(Qt::white));
    QCOMPARE(light.pixelColor(6,10),QColor(Qt::black));
    r.zoom(2,0,0);
    QVERIFY(r.horizontalViewFraction()<1);
    const QImage close=r.renderImage({640,80});
    QVERIFY(!close.isNull());
    QVERIFY(r.paintBinCount()<=4096);
}
QTEST_MAIN(HeatmapModelTest)
#include "tst_heatmap_model.moc"
