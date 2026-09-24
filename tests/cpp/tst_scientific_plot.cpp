// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_dataset.h"
#include "uncertainty_renderer.h"

#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace QindaTK;

class ScientificPlotTest : public QObject {
    Q_OBJECT
private slots:
    void analyticSamplesAndAtomicValidation();
    void logCursorAndExport();
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

QTEST_MAIN(ScientificPlotTest)
#include "tst_scientific_plot.moc"
