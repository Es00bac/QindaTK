// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_dataset.h"
#include "scientific_grid.h"
#include "uncertainty_renderer.h"

#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <limits>

using namespace QindaTK;

class ScientificPlotTest : public QObject {
    Q_OBJECT
private slots:
    void analyticSamplesAndAtomicValidation();
    void logCursorAndExport();
    void asymmetricAndXUncertainty();
    void scatterAnd3DSamples();
    void nonFiniteAndBoundedAccess();
    void gridSemanticsAndInterpolation();
};

void ScientificPlotTest::analyticSamplesAndAtomicValidation()
{
    ScientificDataset data;
    data.setSourceId(QStringLiteral("calibration-2026-A"));
    data.setXUnit(QStringLiteral("s"));
    data.setYUnit(QStringLiteral("m"));
    data.setParameters({{QStringLiteral("function"),QStringLiteral("y=x²")}});
    QVariantList samples;
    for (int x=1; x<=4; ++x)
        samples.append(QVariantMap{{"id",QStringLiteral("p%1").arg(x)},
            {"x",double(x)},{"y",double(x*x)},{"sigmaY",0.1*double(x)}});
    samples.append(QVariantMap{{"id","gap"},{"x",5.0},{"missingY",true}});
    QVERIFY(data.replaceSamples(samples));
    QCOMPARE(data.count(),5);
    QCOMPARE(data.missingCount(),1);
    QCOMPARE(data.shape(),QStringLiteral("[5,2]"));
    QCOMPARE(data.intervals()->count(),4);
    QCOMPARE(data.sample(2).value("y").toDouble(),9.0);
    QCOMPARE(data.sampleById("p3").value("y").toDouble(),9.0);
    QCOMPARE(data.intervals()->interval("p3").value("low").toDouble(),8.7);
    QCOMPARE(data.data(data.index(4),Qt::UserRole+5).toBool(),true);
    const auto metadata=QJsonDocument::fromJson(data.metadataJson().toUtf8()).object();
    QCOMPARE(metadata.value("precision").toString(),QStringLiteral("float64"));
    QCOMPARE(metadata.value("samples").toArray().size(),5);
    QVERIFY(metadata.value("samplesSha256").toString().size()==64);
    const QString before=data.metadataJson();
    samples[1]=QVariantMap{{"id","p2"},{"x",2.0},{"y",4.0},{"sigmaY",-1.0}};
    QVERIFY(!data.replaceSamples(samples));
    QCOMPARE(data.metadataJson(),before);
    QCOMPARE(data.count(),5);
    QVERIFY(data.csvData().contains(QStringLiteral("\"gap\",\"5\",\"\",\"\",\"true\"")));
}

void ScientificPlotTest::logCursorAndExport()
{
    ScientificDataset data;
    data.setSourceId(QStringLiteral("known-log-curve"));
    data.setXUnit(QStringLiteral("Hz"));
    data.setYUnit(QStringLiteral("V"));
    QVERIFY(data.replaceSamples({QVariantMap{{"id","a"},{"x",1.0},{"y",1.0},{"sigmaY",0.1}},
        QVariantMap{{"id","b"},{"x",10.0},{"y",10.0},{"sigmaY",1.0}},
        QVariantMap{{"id","c"},{"x",100.0},{"y",100.0},{"sigmaY",10.0}}}));
    ChartAxis x,y;
    x.setKind(ChartAxis::Logarithmic); x.setMinimum(1); x.setMaximum(100); x.setUnit("Hz");
    y.setKind(ChartAxis::Logarithmic); y.setMinimum(0.1); y.setMaximum(1000); y.setUnit("V");
    UncertaintyRenderer plot;
    plot.setModel(data.intervals()); plot.setXAxis(&x); plot.setYAxis(&y);
    plot.setTitle(QStringLiteral("Known log response"));
    plot.setWidth(640); plot.setHeight(400);
    plot.setBackgroundColor(Qt::white); plot.setGridColor(Qt::gray);
    plot.setLineColor(Qt::blue); plot.setTextColor(Qt::black);
    plot.setMetrics({{"leftMargin",64},{"rightMargin",16},{"topMargin",16},
        {"bottomMargin",48},{"fontSize",11},{"fontFamily","Sans Serif"},
        {"lineWidth",1},{"pointRadius",3},{"capHalfWidth",4},{"labelGap",3},
        {"tickLabelWidth",60},{"hitRadius",8}});
    const auto pixel=plot.projected("b");
    QVERIFY(!pixel.isEmpty());
    const auto cursor=plot.dataAt(pixel.value("x").toDouble(),pixel.value("estimateY").toDouble());
    QVERIFY(std::abs(cursor.value("x").toDouble()-10.0)<1e-9);
    QVERIFY(std::abs(cursor.value("y").toDouble()-10.0)<1e-9);
    QCOMPARE(cursor.value("xUnit").toString(),QStringLiteral("Hz"));
    QVERIFY(plot.selectId("b"));
    QCOMPARE(plot.selectedInterval().value("sourceId").toString(),QStringLiteral("b"));
    QTemporaryDir temporary; QVERIFY(temporary.isValid());
    const QSize size(640,400);
    const QString png=temporary.filePath("figure.png"),svg=temporary.filePath("figure.svg");
    QVERIFY(plot.exportPng(png,size)); QVERIFY(plot.exportSvg(svg,size));
    const QImage image(png); QCOMPARE(image.size(),size);
    QVERIFY(image.pixelColor(64,16)!=image.pixelColor(300,180));
    const auto embedded=QJsonDocument::fromJson(image.text("QindaTK.IntervalData").toUtf8()).object();
    QCOMPARE(embedded.value("figure").toObject().value("sourceId").toString(),QStringLiteral("known-log-curve"));
    QCOMPARE(embedded.value("figure").toObject().value("samples").toArray().size(),3);
    QCOMPARE(embedded.value("render").toObject().value("title").toString(),QStringLiteral("Known log response"));
    QCOMPARE(embedded.value("render").toObject().value("xAxis").toObject().value("minimum").toDouble(),1.0);
    QFile file(svg); QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray svgData=file.readAll();
    QVERIFY(svgData.contains("qindatk-intervals"));
    QVERIFY(svgData.contains("<svg"));
}

void ScientificPlotTest::asymmetricAndXUncertainty()
{
    ScientificDataset data;
    data.setSourceId(QStringLiteral("asymmetric"));
    data.setXUnit(QStringLiteral("s")); data.setYUnit(QStringLiteral("V"));
    QVERIFY(data.replaceSamples({QVariantMap{{"id","a"},{"x",1.0},{"y",10.0},{"sigmaYLow",1.0},{"sigmaYHigh",3.0},{"sigmaX",0.5}},
        QVariantMap{{"id","b"},{"x",2.0},{"y",20.0},{"sigmaY",2.0}}}));
    QCOMPARE(data.intervals()->interval("a").value("low").toDouble(),9.0);
    QCOMPARE(data.intervals()->interval("a").value("high").toDouble(),13.0);
    const QVariantMap a=data.sample(0);
    QVERIFY(a.value("sigmaY").isNull());
    QCOMPARE(a.value("sigmaYLow").toDouble(),1.0);
    QCOMPARE(a.value("sigmaYHigh").toDouble(),3.0);
    QCOMPARE(a.value("sigmaXLow").toDouble(),0.5);
    QCOMPARE(a.value("sigmaXHigh").toDouble(),0.5);
    QCOMPARE(data.sample(1).value("sigmaY").toDouble(),2.0);
    const QVariantMap extent=data.extent();
    QCOMPARE(extent.value("xMinimum").toDouble(),0.5);
    QCOMPARE(extent.value("xMaximum").toDouble(),2.0);
    QCOMPARE(extent.value("yMinimum").toDouble(),9.0);
    QCOMPARE(extent.value("yMaximum").toDouble(),22.0);
    QVERIFY(data.csvData().contains(QStringLiteral("\"a\",\"1\",\"10\",\"\",\"false\",\"1\",\"3\",\"0.5\",\"0.5\"")));
    QCOMPARE(data.shape(),QStringLiteral("[2,2]"));
    // Mixed, half-specified or negative offsets reject the whole batch.
    QVERIFY(!data.replaceSamples({QVariantMap{{"id","m"},{"x",1.0},{"y",1.0},{"sigmaY",1.0},{"sigmaYLow",1.0},{"sigmaYHigh",1.0}}}));
    QVERIFY(!data.lastError().isEmpty());
    QVERIFY(!data.replaceSamples({QVariantMap{{"id","h"},{"x",1.0},{"y",1.0},{"sigmaYLow",1.0}}}));
    QVERIFY(!data.replaceSamples({QVariantMap{{"id","n"},{"x",1.0},{"y",1.0},{"sigmaX",-0.1}}}));
    QCOMPARE(data.count(),2);
}

void ScientificPlotTest::scatterAnd3DSamples()
{
    ScientificDataset cloud;
    cloud.setSourceId(QStringLiteral("cloud")); cloud.setKind(ScientificDataset::Scatter); cloud.setDimensions(3);
    cloud.setXUnit("m"); cloud.setYUnit("m"); cloud.setZUnit("K");
    QVERIFY(cloud.replaceSamples({QVariantMap{{"id","p1"},{"x",3.0},{"y",1.0},{"z",2.0}},
        QVariantMap{{"id","p2"},{"x",1.0},{"y",1.0},{"z",5.0}},
        QVariantMap{{"id","p3"},{"x",1.0},{"y",2.0},{"z",0.0}}}));
    QCOMPARE(cloud.shape(),QStringLiteral("[3,3]"));
    QCOMPARE(cloud.intervals()->count(),0);
    QCOMPARE(cloud.sample(1).value("z").toDouble(),5.0);
    QCOMPARE(cloud.extent().value("zMaximum").toDouble(),5.0);
    QVERIFY(!cloud.replaceSamples({QVariantMap{{"id","q"},{"x",0.0},{"y",0.0}}})); // present 3D row without z
    cloud.setDimensions(2); // refused once samples exist
    QCOMPARE(cloud.dimensions(),3);
    cloud.setKind(ScientificDataset::Trace); // unordered x cannot become a trace
    QCOMPARE(cloud.kind(),ScientificDataset::Scatter);
    ScientificDataset trace;
    trace.setSourceId(QStringLiteral("trace"));
    QVERIFY(!trace.replaceSamples({QVariantMap{{"id","a"},{"x",2.0},{"y",1.0}},QVariantMap{{"id","b"},{"x",1.0},{"y",1.0}}}));
    QVERIFY(!trace.replaceSamples({QVariantMap{{"id","a"},{"x",2.0},{"y",1.0},{"z",1.0}}})); // z needs dimensions 3
    QCOMPARE(trace.metadataJson().contains(QStringLiteral("\"kind\":\"trace\"")),true);
}

void ScientificPlotTest::nonFiniteAndBoundedAccess()
{
    ScientificDataset data;
    data.setSourceId(QStringLiteral("finite"));
    const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    QVERIFY(!data.replaceSamples({QVariantMap{{"id","a"},{"x",1.0},{"y",nan}}}));
    QVERIFY(!data.replaceSamples({QVariantMap{{"id","a"},{"x",inf},{"y",1.0}}}));
    QVERIFY(!data.replaceSamples({QVariantMap{{"id","a"},{"x",1.0},{"y",1.0},{"sigmaY",inf}}}));
    QVERIFY(!data.replaceSamples({QVariantMap{{"id","a"},{"x",1.0},{"y",QStringLiteral("1")}}}));
    QVERIFY(!data.replaceSamples({QVariantMap{{"id","a"},{"x",1.0},{"y",1e308},{"sigmaY",1e308}}})); // endpoint overflows
    QVariantList rows;
    for (int i=0;i<10;++i) rows.append(QVariantMap{{"id",QStringLiteral("s%1").arg(i)},{"x",double(i)},{"y",double(i)}});
    rows[4]=QVariantMap{{"id","s4"},{"x",4.0},{"missingY",true}};
    QVERIFY(data.replaceSamples(rows));
    QCOMPARE(data.missingCount(),1);
    QCOMPARE(data.samplesInRange(2,5,100).size(),4);
    QCOMPARE(data.samplesInRange(2,5,2).size(),2);
    QCOMPARE(data.samplesInRange(0,9,20000).size(),10);
    QVERIFY(data.samplesInRange(5,2,100).isEmpty());
    QCOMPARE(data.samplesInRange(4,4,10).first().toMap().value("missingY").toBool(),true);
}

void ScientificPlotTest::gridSemanticsAndInterpolation()
{
    ScientificGrid grid;
    grid.setSourceId(QStringLiteral("plane")); grid.setXUnit("m"); grid.setYUnit("m"); grid.setZUnit("K");
    // z = 2x + 3(y/10), row-major with row 0 at the smallest y.
    QVERIFY(grid.replaceGrid({0.0,1.0,2.0},{0.0,10.0},{0.0,2.0,4.0,3.0,5.0,7.0}));
    QCOMPARE(grid.shape(),QStringLiteral("[2,3]"));
    QCOMPARE(grid.zMinimum(),0.0);
    QCOMPARE(grid.zMaximum(),7.0);
    QCOMPARE(grid.interpolate(0.5,5,ScientificGrid::Bilinear).value("z").toDouble(),2.5);
    QCOMPARE(grid.interpolate(1.5,2.5,ScientificGrid::Bilinear).value("z").toDouble(),3.75);
    QCOMPARE(grid.interpolate(0.6,4.9,ScientificGrid::Nearest).value("z").toDouble(),2.0);
    QCOMPARE(grid.interpolate(0.5,0,ScientificGrid::Nearest).value("z").toDouble(),0.0); // tie -> lower index
    const QVariantMap outside=grid.interpolate(3,0,ScientificGrid::Bilinear);
    QCOMPARE(outside.value("inside").toBool(),false);
    QCOMPARE(outside.value("missing").toBool(),true);
    QVERIFY(grid.replaceGrid({0.0,1.0,2.0},{0.0,10.0},{0.0,QVariant(),4.0,3.0,5.0,7.0}));
    QCOMPARE(grid.missingCount(),1);
    QCOMPARE(grid.interpolate(0.5,5,ScientificGrid::Bilinear).value("missing").toBool(),true);
    QCOMPARE(grid.interpolate(0,0,ScientificGrid::Bilinear).value("z").toDouble(),0.0); // zero-weight hole ignored
    QCOMPARE(grid.interpolate(2,10,ScientificGrid::Bilinear).value("z").toDouble(),7.0);
    const QString before=grid.metadataJson();
    QVERIFY(!grid.replaceGrid({0.0,1.0,2.0},{0.0,10.0},{0.0,std::numeric_limits<double>::quiet_NaN(),4.0,3.0,5.0,7.0}));
    QVERIFY(!grid.replaceGrid({0.0,0.0,2.0},{0.0,10.0},{0.0,1.0,4.0,3.0,5.0,7.0}));
    QVERIFY(!grid.replaceGrid({0.0,1.0,2.0},{0.0,10.0},{0.0,1.0}));
    QCOMPARE(grid.metadataJson(),before);
    QCOMPARE(grid.missingCount(),1);
    const QVariantMap window=grid.window(0,1,0,2,1,2);
    QCOMPARE(window.value("rows").toInt(),1);
    QCOMPARE(window.value("columns").toInt(),2);
    QCOMPARE(window.value("xs").toList(),(QVariantList{0.0,2.0}));
    QCOMPARE(window.value("values").toList().first().toList(),(QVariantList{0.0,4.0}));
    QCOMPARE(grid.rowCount(),6);
    QCOMPARE(grid.data(grid.index(1),grid.roleNames().key("missing")).toBool(),true);
    QCOMPARE(QJsonDocument::fromJson(before.toUtf8()).object().value("valuesSha256").toString().size(),64);
    QVERIFY(grid.csvData().contains(QStringLiteral("\"plane\",\"0\",\"1\",\"1\",\"0\",\"\",\"true\"")));
    ScientificGrid same;
    same.setSourceId(QStringLiteral("plane")); same.setXUnit("m"); same.setYUnit("m"); same.setZUnit("K");
    QVERIFY(same.replaceGrid({0.0,1.0,2.0},{0.0,10.0},{0.0,QVariant(),4.0,3.0,5.0,7.0}));
    QCOMPARE(same.metadataJson(),before);
}

QTEST_MAIN(ScientificPlotTest)
#include "tst_scientific_plot.moc"
