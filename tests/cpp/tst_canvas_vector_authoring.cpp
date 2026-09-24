// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <QPainter>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include "canvas_document_format.h"
#include "canvas_geometry.h"
#include "canvas_render.h"
#include "canvas_style_model.h"
#include "canvas_svg_export.h"
#include "clipboard_service.h"
#include "hit_test_service.h"

using namespace QindaTK;

class TstCanvasVectorAuthoring : public QObject {
    Q_OBJECT
private slots:
    void editPersistRenderExport();
    void contourHoleAndClipboard();
};

void TstCanvasVectorAuthoring::editPersistRenderExport()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0,0,256,256));
    const QVariantList nodes{
        QVariantMap{{"x",40},{"y",60},{"outX",80},{"outY",8}},
        QVariantMap{{"x",190},{"y",60},{"inX",150},{"inY",8}},
        QVariantMap{{"x",190},{"y",190}},
        QVariantMap{{"x",40},{"y",190}}
    };
    const CanvasObjectId id = doc.addPath(nodes, true, QColor("#38b6aa"), QColor("#ffcc00"), 8);
    QVERIFY(id != 0);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    QVERIFY(styles.apply({QString::number(id)}, {{"dash", QVariantList{3.0,2.0}},
        {"cap", "round"}, {"join", "bevel"}, {"fillRule", "nonzero"}}));
    const CanvasObject styled = *doc.object(id);
    QCOMPARE(styled.strokeCap, Qt::RoundCap);
    QCOMPARE(styled.strokeJoin, Qt::BevelJoin);
    QCOMPARE(styled.fillRule, Qt::WindingFill);
    QCOMPARE(styled.dashPattern, (QVector<qreal>{3,2}));

    QVERIFY(doc.movePathPart(id, 0, QStringLiteral("out"), 90, 4));
    QCOMPARE(doc.object(id)->pathNodes[0].out, QPointF(90,4));
    QVERIFY(doc.undo());
    QCOMPARE(*doc.object(id), styled);
    QVERIFY(doc.insertPathNode(id, 2, {{"x",205},{"y",120}}));
    QCOMPARE(doc.pathNodes(id).size(), 5);
    QVERIFY(doc.removePathNode(id, 2));
    QCOMPARE(*doc.object(id), styled);
    QVERIFY(doc.movePathPart(id, 1, QStringLiteral("anchor"), 200, 70));
    QCOMPARE(doc.object(id)->pathNodes[1].in, QPointF(160,18));
    QVERIFY(doc.undo());
    QCOMPARE(*doc.object(id), styled);

    OperationError error;
    const QByteArray native = CanvasDocumentFormat::encode(doc.state(), &error);
    QVERIFY2(!native.isEmpty(), qPrintable(error.message));
    const auto state = CanvasDocumentFormat::decode(native, {}, &error);
    QVERIFY2(state.has_value(), qPrintable(error.message));
    QCOMPARE(state->objects.value(id), styled);
    auto reopened = CanvasDocument::fromState(*state);
    QVERIFY(reopened);
    QCOMPARE(*reopened->object(id), styled);

    const QByteArray svg = CanvasSvgExport::encode(*reopened, &error);
    QVERIFY2(!svg.isEmpty(), qPrintable(error.message));
    QVERIFY(svg.contains(" C "));
    QVERIFY(svg.contains("stroke-dasharray=\"24 16\""));
    QVERIFY(svg.contains("stroke-linecap=\"round\""));
    QVERIFY(svg.contains("fill-rule=\"nonzero\""));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString svgFile = dir.filePath(QStringLiteral("styled.svg"));
    QVERIFY(reopened->exportSvg(svgFile));
    QFile saved(svgFile);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), svg);

    const QImage canvas = CanvasPainter::renderTile(*reopened, QRectF(0,0,256,256), 256, {});
    QVERIFY(!canvas.isNull());
    QSvgRenderer renderer(svg);
    QVERIFY(renderer.isValid());
    QImage svgImage(256,256,QImage::Format_ARGB32_Premultiplied);
    svgImage.fill(Qt::transparent);
    { QPainter painter(&svgImage); renderer.render(&painter); }
    const QPoint interior(110,120);
    QCOMPARE(canvas.pixelColor(interior), QColor("#38b6aa"));
    QCOMPARE(svgImage.pixelColor(interior), canvas.pixelColor(interior));
    QVERIFY(!doc.addPath({QVariantMap{{"x",1},{"y",1}}}, false,
                         QColor(Qt::red), {}, 0));
    const QVariantList twice{
        QVariantMap{{"x",40},{"y",40}}, QVariantMap{{"x",120},{"y",40}},
        QVariantMap{{"x",120},{"y",120}}, QVariantMap{{"x",40},{"y",120}},
        QVariantMap{{"x",40},{"y",40}}, QVariantMap{{"x",120},{"y",40}},
        QVariantMap{{"x",120},{"y",120}}, QVariantMap{{"x",40},{"y",120}}};
    const auto windingId = doc.addPath(twice, true, QColor(Qt::red), {}, 0);
    QVERIFY(windingId);
    QVERIFY(!CanvasGeometry::fillContains(*doc.object(windingId), QPointF(80,80)));
    QVERIFY(styles.apply({QString::number(windingId)}, {{"fillRule", "nonzero"}}));
    QVERIFY(CanvasGeometry::fillContains(*doc.object(windingId), QPointF(80,80)));
    QVERIFY(doc.setObjectTransform(windingId, {1,0,0,1,24,0}));
    QVERIFY(CanvasGeometry::fillContains(*doc.object(windingId), QPointF(104,80)));
    QVERIFY(!CanvasGeometry::fillContains(*doc.object(windingId), QPointF(60,80)));
    const QString capture = qEnvironmentVariable("QINDATK_WP044_CAPTURE");
    if (!capture.isEmpty()) {
        QVERIFY(canvas.save(capture + QStringLiteral("-canvas.png")));
        QVERIFY(svgImage.save(capture + QStringLiteral("-svg.png")));
    }
}

class MemoryCanvasClipboard final : public ClipboardAdapter {
public:
    const QMimeData *mimeData() const override { return data.get(); }
    bool setMimeData(QMimeData *value) override { data.reset(value); emit changed(); return true; }
    std::unique_ptr<QMimeData> data;
};

void TstCanvasVectorAuthoring::contourHoleAndClipboard()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0,0,200,200));
    const QVariantList outer{QVariantMap{{"x",20},{"y",20}}, QVariantMap{{"x",180},{"y",20}},
        QVariantMap{{"x",180},{"y",180}}, QVariantMap{{"x",20},{"y",180}}};
    const QVariantList inner{QVariantMap{{"x",65},{"y",65}}, QVariantMap{{"x",135},{"y",65}},
        QVariantMap{{"x",135},{"y",135}}, QVariantMap{{"x",65},{"y",135}}};
    const CanvasObjectId id = doc.addPath(outer, true, QColor("#00a9b7"), QColor("#f5cc48"), 2);
    QVERIFY(id);
    QVERIFY(doc.addPathContour(id, inner, true));
    QCOMPARE(doc.pathContourCount(id), 2);
    QVERIFY(!CanvasGeometry::fillContains(*doc.object(id), QPointF(100,100)));
    QVERIFY(CanvasGeometry::fillContains(*doc.object(id), QPointF(40,40)));
    QVERIFY(HitTestService::hitPoint(doc, QPointF(100,100), 0).isEmpty());
    const auto innerHits = HitTestService::hitPoint(doc, QPointF(65,100), 2);
    QVERIFY(!innerHits.isEmpty());
    QCOMPARE(innerHits.first().object, id);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    QVERIFY(styles.apply({QString::number(id)}, {{"fillRule", "nonzero"}}));
    QVERIFY(CanvasGeometry::fillContains(*doc.object(id), QPointF(100,100)));
    QVERIFY(doc.removePathContour(id, 1));
    QVERIFY(doc.addPathContour(id, {inner[3], inner[2], inner[1], inner[0]}, true));
    QVERIFY(!CanvasGeometry::fillContains(*doc.object(id), QPointF(100,100)));
    QVERIFY(doc.undo());
    QVERIFY(doc.undo());
    QVERIFY(styles.apply({QString::number(id)}, {{"fillRule", "evenodd"}}));
    const CanvasObject beforeEdit = *doc.object(id);
    QVERIFY(doc.movePathContourPart(id, 1, 0, QStringLiteral("anchor"), 60, 60));
    QCOMPARE(doc.pathContourNodes(id, 1).first().toMap().value(QStringLiteral("x")).toDouble(), 60.0);
    QVERIFY(doc.undo());
    QCOMPARE(*doc.object(id), beforeEdit);
    QVERIFY(doc.removePathContour(id, 1));
    QCOMPARE(doc.pathContourCount(id), 1);
    QVERIFY(doc.undo());
    QCOMPARE(*doc.object(id), beforeEdit);
    OperationError error;
    const QByteArray native = CanvasDocumentFormat::encode(doc.state(), &error);
    QVERIFY2(!native.isEmpty(), qPrintable(error.message));
    const auto state = CanvasDocumentFormat::decode(native, {}, &error);
    QVERIFY2(state.has_value(), qPrintable(error.message));
    QCOMPARE(state->objects.value(id), beforeEdit);
    const QByteArray svg = CanvasSvgExport::encode(doc, &error);
    QVERIFY2(!svg.isEmpty(), qPrintable(error.message));
    QVERIFY(svg.count("M ") == 2);
    QVERIFY(svg.contains("fill-rule=\"evenodd\""));
    QSvgRenderer svgRenderer(svg);
    QVERIFY(svgRenderer.isValid());
    QImage svgImage(200,200,QImage::Format_ARGB32_Premultiplied);
    svgImage.fill(Qt::transparent);
    { QPainter painter(&svgImage); svgRenderer.render(&painter); }
    const QImage canvas = CanvasPainter::renderTile(doc, QRectF(0,0,200,200), 200, {});
    QCOMPARE(canvas.pixelColor(100,100), doc.pageColor());
    QCOMPARE(svgImage.pixelColor(100,100), doc.pageColor());
    QCOMPARE(canvas.pixelColor(40,40), QColor("#00a9b7"));
    QCOMPARE(svgImage.pixelColor(40,40), QColor("#00a9b7"));
    const QString capture = qEnvironmentVariable("QINDATK_WP044_CONTOUR_CAPTURE");
    if (!capture.isEmpty()) QVERIFY(canvas.save(capture));

    MemoryCanvasClipboard adapter;
    ClipboardService clipboard;
    clipboard.setAdapter(&adapter);
    QVERIFY(clipboard.copyCanvasPaths(&doc, {QString::number(id)}));
    QVERIFY(adapter.mimeData()->hasFormat(QStringLiteral("image/svg+xml")));
    const QStringList pasted = clipboard.pasteCanvasPaths(&doc, 5, 7);
    QCOMPARE(pasted.size(), 1);
    const CanvasObjectId newId = pasted.first().toULongLong();
    QVERIFY(newId != id);
    QCOMPARE(doc.object(newId)->extraContours, beforeEdit.extraContours);
    QCOMPARE(doc.object(newId)->transform, beforeEdit.transform * QTransform::fromTranslate(5,7));
    QVERIFY(doc.undo());
    QVERIFY(!doc.object(newId));
    QVERIFY(doc.object(id));
}

QTEST_MAIN(TstCanvasVectorAuthoring)
#include "tst_canvas_vector_authoring.moc"
