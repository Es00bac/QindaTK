// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <limits>
#include "uncertainty_model.h"
#include "uncertainty_renderer.h"
using namespace QindaTK;

class UncertaintyTest : public QObject {
    Q_OBJECT
private slots:
    void typedValidationAndAtomicReplacement();
    void projectionSelectionPixelsAndExport();
    void boundedVisibleWork();
};
void UncertaintyTest::typedValidationAndAtomicReplacement()
{
    UncertaintyModel model;
    model.setSourceDatasetId("survey-17"); model.setXUnit("hours"); model.setYUnit("m");
    model.setMeaning(UncertaintyModel::Credible); model.setCoverage(0.9);
    QVERIFY(model.replaceIntervals({QVariantMap{{"id","a"},{"sourceId","raw-a"},
        {"x",1},{"estimate",5},{"low",3},{"high",7}},
        QVariantMap{{"id","gap"},{"sourceId","raw-gap"},{"x",2},{"estimate",6}},
        QVariantMap{{"id","b"},{"sourceId","raw-b"},{"x",3},{"estimate",5},{"low",2},{"high",8}}}));
    QCOMPARE(model.count(),3); QCOMPARE(model.missingCount(),1);
    QCOMPARE(model.get(1).value("status").toString(),"missing");
    QCOMPARE(model.get(0).value("meaning").toString(),"credible");
    QCOMPARE(model.get(0).value("coverage").toDouble(),0.9);
    QVERIFY(model.csvData().contains("survey-17"));
    QVERIFY(model.csvData().contains("raw-gap"));
    QVERIFY(!model.replaceIntervals({QVariantMap{{"id","bad"},{"sourceId","s"},
        {"x",1},{"estimate",2},{"low",3},{"high",1}}}));
    QCOMPARE(model.count(),3);
    QVERIFY(!model.replaceIntervals({QVariantMap{{"id","bad"},{"sourceId","s"},
        {"x",1},{"estimate",2},{"low",0}}}));
    QVERIFY(!model.replaceIntervals({QVariantMap{{"id","bad"},{"sourceId","s"},
        {"x",1},{"estimate",2},{"low",0},{"high",std::numeric_limits<double>::infinity()}}}));
    QVERIFY(!model.replaceIntervals({QVariantMap{{"id","a"},{"sourceId","s"},{"x",2},{"estimate",2}},
        QVariantMap{{"id","b"},{"sourceId","s"},{"x",1},{"estimate",2}}}));
    QCOMPARE(model.count(),3);
    model.setMeaning(UncertaintyModel::Domain);
    QVERIFY(!model.replaceIntervals({QVariantMap{{"id","c"},{"sourceId","s"},{"x",0},{"estimate",0}}}));
    model.setMeaningDetail("instrument tolerance");
    QVERIFY(model.replaceIntervals({QVariantMap{{"id","c"},{"sourceId","s"},
        {"x",0},{"estimate",-2},{"low",-3},{"high",-1}}}));
    QCOMPARE(model.get(0).value("meaningDetail").toString(),"instrument tolerance");
    QVERIFY(!model.csvData().contains("credible"));
}
void UncertaintyTest::projectionSelectionPixelsAndExport()
{
    UncertaintyModel model; model.setSourceDatasetId("survey-17");
    model.setXUnit("h"); model.setYUnit("m");
    QVERIFY(model.replaceIntervals({QVariantMap{{"id","a"},{"sourceId","raw-a"},
        {"x",1},{"estimate",5},{"low",3},{"high",7}},
        QVariantMap{{"id","b"},{"sourceId","raw-b"},
        {"x",3},{"estimate",5},{"low",3},{"high",7}}}));
    ChartAxis x,y; x.setMinimum(0); x.setMaximum(4); x.setUnit("h");
    y.setMinimum(0); y.setMaximum(10); y.setUnit("m");
    UncertaintyRenderer view; view.setModel(&model); view.setXAxis(&x); view.setYAxis(&y);
    view.setWidth(200); view.setHeight(100);
    view.setMetrics({{"leftMargin",0},{"rightMargin",0},{"topMargin",0},
        {"bottomMargin",0},{"lineWidth",1},{"pointRadius",3},{"capHalfWidth",4},
        {"hitRadius",8},{"fontSize",8}});
    view.setBackgroundColor(Qt::white); view.setGridColor(Qt::white);
    view.setLineColor(Qt::red); view.setBandColor(Qt::blue);
    view.setMissingColor(Qt::magenta); view.setSelectionColor(Qt::green);
    const auto projected=view.projected("a");
    QCOMPARE(projected.value("x").toDouble(),50.0);
    QCOMPARE(projected.value("estimateY").toDouble(),50.0);
    QCOMPARE(projected.value("lowY").toDouble(),70.0);
    QCOMPARE(projected.value("highY").toDouble(),30.0);
    QCOMPARE(view.hitAt(50,40),QString("a"));
    QVERIFY(view.selectId("a")); QCOMPARE(view.selectedInterval().value("sourceId").toString(),"raw-a");
    QVERIFY(view.navigateSelection(Qt::Key_Right)); QCOMPARE(view.selectedId(),QString("b"));
    view.setMode(UncertaintyRenderer::Band);
    const QImage image=view.renderImage({200,100});
    QCOMPARE(image.pixelColor(100,40),QColor(Qt::blue));
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString png=directory.filePath("interval.png"),svg=directory.filePath("interval.svg");
    QVERIFY(view.exportPng(png,{200,100})); QVERIFY(view.exportSvg(svg,{200,100}));
    const QImage raster(png); QCOMPARE(raster.pixelColor(100,40),QColor(Qt::blue));
    const QJsonDocument pngMeta=QJsonDocument::fromJson(raster.text("QindaTK.IntervalData").toUtf8());
    QCOMPARE(pngMeta.object().value("meaning").toString(),"confidence");
    QCOMPARE(pngMeta.object().value("intervals").toArray().at(0).toObject().value("sourceId").toString(),"raw-a");
    QFile svgFile(svg); QVERIFY(svgFile.open(QIODevice::ReadOnly));
    const QByteArray svgBytes=svgFile.readAll();
    const QRegularExpression metadata("<metadata id=\"qindatk-intervals\" data-encoding=\"base64-json\">([^<]+)</metadata>");
    const auto match=metadata.match(QString::fromUtf8(svgBytes)); QVERIFY(match.hasMatch());
    const QJsonDocument svgMeta=QJsonDocument::fromJson(QByteArray::fromBase64(match.captured(1).toLatin1()));
    QCOMPARE(svgMeta.object().value("coverage").toDouble(),0.95);
    QCOMPARE(svgMeta.object().value("intervals").toArray().size(),2);
    QSvgRenderer svgRenderer(svg); QVERIFY(svgRenderer.isValid());
    QImage vector({200,100},QImage::Format_ARGB32_Premultiplied); vector.fill(Qt::transparent);
    QPainter painter(&vector); svgRenderer.render(&painter); painter.end();
    QCOMPARE(vector.pixelColor(100,40),QColor(Qt::blue));
    QVERIFY(model.replaceIntervals({QVariantMap{{"id","a"},{"sourceId","raw-a"},
        {"x",1},{"estimate",5},{"low",3},{"high",7}},
        QVariantMap{{"id","gap"},{"sourceId","raw-gap"},{"x",2},{"estimate",5}},
        QVariantMap{{"id","b"},{"sourceId","raw-b"},
        {"x",3},{"estimate",5},{"low",3},{"high",7}}}));
    QCOMPARE(view.renderImage({200,100}).pixelColor(100,40),QColor(Qt::white));
    QVERIFY(view.renderImage({8193,1}).isNull());
}
void UncertaintyTest::boundedVisibleWork()
{
    UncertaintyModel model; model.setXUnit("s"); model.setYUnit("m");
    QVariantList rows;
    for (int i=0;i<10000;++i) rows.append(QVariantMap{{"id",QString::number(i)},
        {"sourceId",QStringLiteral("source-%1").arg(i)},{"x",i},{"estimate",i%10},
        {"low",i%10-1},{"high",i%10+1}});
    QVERIFY(model.replaceIntervals(rows));
    ChartAxis x,y; x.setMinimum(0); x.setMaximum(10000); x.setUnit("s");
    y.setMinimum(-2); y.setMaximum(12); y.setUnit("m");
    UncertaintyRenderer view; view.setModel(&model); view.setXAxis(&x); view.setYAxis(&y);
    view.setPaintPointLimit(64);
    QVERIFY(view.visiblePaintCount()<=64);
    view.setViewport(0.5,0.501,0,1);
    QVERIFY(view.visiblePaintCount()<=12);
    QVERIFY(view.selectId("5000"));
    QVERIFY(model.replaceIntervals({QVariantMap{{"id","5000"},{"sourceId","source-5000"},
        {"x",5000},{"estimate",2},{"low",1},{"high",3}}}));
    QCOMPARE(view.selectedId(),QString("5000"));
}
QTEST_MAIN(UncertaintyTest)
#include "tst_uncertainty.moc"
