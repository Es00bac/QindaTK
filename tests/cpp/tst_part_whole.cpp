// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <QPainter>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include "part_whole_model.h"
#include "part_whole_layout.h"
#include "part_whole_renderer.h"
using namespace QindaTK;
class PartWholeTest : public QObject {
    Q_OBJECT
private slots:
    void exactAnglesAreasAndExports();
    void hierarchyTotalsMissingAndAtomicRejection();
    void boundsAndStableSelection();
};
void PartWholeTest::exactAnglesAreasAndExports()
{
    PartWholeModel model; model.setUnit("kg");
    QVERIFY(model.replaceNodes({QVariantMap{{"id","root"},{"value",4}},
        QVariantMap{{"id","a"},{"parentId","root"},{"label","Apples"},{"value",1}},
        QVariantMap{{"id","b"},{"parentId","root"},{"label","Bananas"},{"value",3}}}));
    QCOMPARE(model.knownTotal(),4.0); QCOMPARE(model.visibleRootIds(),QStringList({"a","b"}));
    PartWholeRenderer view; view.setModel(&model); view.setWidth(200); view.setHeight(200);
    view.setSliceColors({QColor(Qt::red),QColor(Qt::green)});
    view.setBackgroundColor(Qt::black); view.setBorderColor(Qt::black);
    view.setMetrics({{"margin",0},{"bottomMargin",0},{"lineWidth",0},{"fontSize",8}});
    QCOMPARE(view.geometryFor("a").value("angleSpan").toDouble(),90.0);
    QCOMPARE(view.geometryFor("b").value("angleSpan").toDouble(),270.0);
    QCOMPARE(view.hitAt(150,50),QString("a"));
    QCOMPARE(view.hitAt(50,150),QString("b"));
    QVERIFY(view.selectId("a")); QCOMPARE(view.selectedNode().value("label").toString(),"Apples");
    view.setMode(PartWholeRenderer::Donut);
    QCOMPARE(view.geometryFor("a").value("innerRadius").toDouble(),0.5);
    QCOMPARE(view.hitAt(100,100),QString());
    view.setMode(PartWholeRenderer::Treemap);
    const auto a=view.geometryFor("a"),b=view.geometryFor("b");
    QCOMPARE(a.value("width").toDouble()*a.value("height").toDouble(),0.25);
    QCOMPARE(b.value("width").toDouble()*b.value("height").toDouble(),0.75);
    QCOMPARE(view.hitAt(20,50),QString("a"));
    const QImage image=view.renderImage({200,200});
    QCOMPARE(image.pixelColor(20,50),QColor(Qt::red));
    QCOMPARE(image.pixelColor(150,50),QColor(Qt::green));
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString png=directory.filePath("parts.png"),svg=directory.filePath("parts.svg");
    QVERIFY(view.exportPng(png,{200,200})); QVERIFY(view.exportSvg(svg,{200,200}));
    const QImage saved(png);
    QCOMPARE(saved.size(),QSize(200,200));
    QCOMPARE(saved.pixelColor(20,50),image.pixelColor(20,50));
    QCOMPARE(saved.pixelColor(150,50),image.pixelColor(150,50));
    QSvgRenderer svgRenderer(svg); QVERIFY(svgRenderer.isValid());
    QImage vectorImage({200,200},QImage::Format_ARGB32_Premultiplied);
    vectorImage.fill(Qt::transparent);
    QPainter vectorPainter(&vectorImage); svgRenderer.render(&vectorPainter); vectorPainter.end();
    QCOMPARE(vectorImage.pixelColor(20,50),image.pixelColor(20,50));
    QCOMPARE(vectorImage.pixelColor(150,50),image.pixelColor(150,50));
}
void PartWholeTest::hierarchyTotalsMissingAndAtomicRejection()
{
    PartWholeModel model;
    QVERIFY(model.replaceNodes({QVariantMap{{"id","root"}},
        QVariantMap{{"id","branch"},{"parentId","root"},{"value",2}},
        QVariantMap{{"id","leaf"},{"parentId","branch"},{"value",2}},
        QVariantMap{{"id","zero"},{"parentId","root"},{"value",0}},
        QVariantMap{{"id","gap"},{"parentId","root"},{"value",QVariant()}}}));
    QCOMPARE(model.knownTotal(),2.0); QCOMPARE(model.missingCount(),1);
    QCOMPARE(model.zeroCount(),1);
    QCOMPARE(model.node("zero").value("status").toString(),"zero");
    QCOMPARE(model.node("gap").value("status").toString(),"missing");
    QCOMPARE(model.node("root").value("status").toString(),"missing");
    PartWholeRenderer view; view.setModel(&model); view.setMode(PartWholeRenderer::Sunburst);
    QCOMPARE(view.geometryFor("branch").value("angleSpan").toDouble(),360.0);
    QCOMPARE(view.geometryFor("leaf").value("innerRadius").toDouble(),0.5);
    QVERIFY(view.geometryFor("gap").isEmpty());
    QVERIFY(!model.replaceNodes({QVariantMap{{"id","bad"},{"value",-1}}}));
    QCOMPARE(model.count(),5);
    QVERIFY(!model.replaceNodes({QVariantMap{{"id","p"},{"value",3}},
        QVariantMap{{"id","c"},{"parentId","p"},{"value",2}}}));
    QCOMPARE(model.count(),5);
    QVERIFY(!model.replaceNodes({QVariantMap{{"id","p"},{"value",3}},
        QVariantMap{{"id","missing"},{"parentId","p"},{"value",QVariant()}}}));
    QCOMPARE(model.count(),5);
}
void PartWholeTest::boundsAndStableSelection()
{
    PartWholeModel model;
    QVariantList deep;
    for (int i=0;i<10;++i) deep.append(QVariantMap{{"id",QString::number(i)},
        {"parentId",i==0 ? QString() : QString::number(i-1)},{"value",1}});
    QVERIFY(!model.replaceNodes(deep));
    QVariantList tooMany;
    for (int i=0;i<4097;++i) tooMany.append(QVariantMap{{"id",QString::number(i)},{"value",1}});
    QVERIFY(!model.replaceNodes(tooMany));
    QVERIFY(model.replaceNodes({QVariantMap{{"id","a"},{"value",1}},
                                QVariantMap{{"id","b"},{"value",2}}}));
    PartWholeRenderer view; view.setModel(&model);
    QVERIFY(view.selectId("b"));
    QVERIFY(model.replaceNodes({QVariantMap{{"id","b"},{"value",4}},
                                QVariantMap{{"id","c"},{"value",1}}}));
    QCOMPARE(view.selectedId(),QString("b"));
    QVERIFY(view.navigateSelection(Qt::Key_Down)); QCOMPARE(view.selectedId(),QString("c"));
    QVERIFY(model.replaceNodes({QVariantMap{{"id","a"},{"value",1}}}));
    QCOMPARE(view.selectedId(),QString());
    QVERIFY(view.renderImage({8193,1}).isNull());
}
QTEST_MAIN(PartWholeTest)
#include "tst_part_whole.moc"
