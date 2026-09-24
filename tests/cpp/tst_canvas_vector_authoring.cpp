// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <QCryptographicHash>
#include <QPainter>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QtEndian>
#include <functional>
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
    void parametricShapes();
    void parametricParametersRejectInvalidValues();
    void parametricEditsAreReversibleAndNodeEditsDemote();
    void parametricPersistenceAndOlderVersions();
    void roundedRectangleAtFullRadius();
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

void TstCanvasVectorAuthoring::parametricShapes()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0,0,440,220));
    const CanvasObjectId star = doc.addParametricShape(QStringLiteral("star"), 20,20,100,100,
        {{"points",5},{"innerRadius",0.45}}, QColor("#c52d61"), {}, 0);
    const CanvasObjectId polygon = doc.addParametricShape(QStringLiteral("polygon"), 155,20,100,100,
        {{"sides",6}}, QColor("#2877c6"), {}, 0);
    const CanvasObjectId rounded = doc.addParametricShape(QStringLiteral("rounded-rectangle"),
        290,20,110,100, {{"radius",22}}, QColor("#3a9b69"), {}, 0);
    QVERIFY(star && polygon && rounded);
    QCOMPARE(doc.object(star)->pathNodes.size(), 10);
    QCOMPARE(doc.object(polygon)->pathNodes.size(), 6);
    QCOMPARE(doc.object(rounded)->pathNodes.size(), 8);
    QCOMPARE(doc.parametricParameters(star).value(QStringLiteral("innerRadius")).toDouble(), 0.45);
    QVERIFY(!doc.setParametricParameters(star, {{"points", 2}}));
    const CanvasObject before = *doc.object(star);
    QVERIFY(doc.setParametricParameters(star, {{"points",7},{"innerRadius",0.25}}));
    QCOMPARE(doc.object(star)->pathNodes.size(), 14);
    QVERIFY(doc.undo());
    QCOMPARE(*doc.object(star), before);
    QVERIFY(doc.setParametricParameters(polygon, {{"sides",8}}));
    QCOMPARE(doc.object(polygon)->pathNodes.size(), 8);
    QVERIFY(doc.undo());
    QVERIFY(doc.setParametricParameters(rounded, {{"radius",30}}));
    QVERIFY(doc.undo());
    QVERIFY(doc.movePathPart(star, 0, QStringLiteral("anchor"), 70, 18));
    QCOMPARE(doc.object(star)->parametricKind, CanvasParametricKind::None);
    QVERIFY(doc.undo());
    QCOMPARE(doc.object(star)->parametricKind, CanvasParametricKind::Star);

    OperationError error;
    const QByteArray native = CanvasDocumentFormat::encode(doc.state(), &error);
    QVERIFY2(!native.isEmpty(), qPrintable(error.message));
    const auto state = CanvasDocumentFormat::decode(native, {}, &error);
    QVERIFY2(state.has_value(), qPrintable(error.message));
    QCOMPARE(state->objects.value(star), *doc.object(star));
    QCOMPARE(state->objects.value(polygon), *doc.object(polygon));
    QCOMPARE(state->objects.value(rounded), *doc.object(rounded));
    const auto reopened = CanvasDocument::fromState(*state);
    QVERIFY(reopened);
    QCOMPARE(reopened->parametricParameters(star).value(QStringLiteral("innerRadius")).toDouble(), 0.45);
    const QByteArray svg = CanvasSvgExport::encode(doc, &error);
    QVERIFY2(!svg.isEmpty(), qPrintable(error.message));
    QCOMPARE(svg.count("<path "), 3);
    QVERIFY(svg.contains(" C ")); // rounded corners remain cubic in SVG
    QSvgRenderer renderer(svg);
    QVERIFY(renderer.isValid());
    QImage exported(440,220,QImage::Format_ARGB32_Premultiplied);
    exported.fill(Qt::transparent);
    { QPainter painter(&exported); renderer.render(&painter); }
    const QImage canvas = CanvasPainter::renderTile(doc, QRectF(0,0,440,220), 440, {});
    for (const QPoint sample : {QPoint(70,70), QPoint(205,70), QPoint(345,70), QPoint(291,21)})
        QCOMPARE(exported.pixelColor(sample), canvas.pixelColor(sample));
    QCOMPARE(canvas.pixelColor(70,70), QColor("#c52d61"));
    QCOMPARE(canvas.pixelColor(205,70), QColor("#2877c6"));
    QCOMPARE(canvas.pixelColor(345,70), QColor("#3a9b69"));
    QCOMPARE(canvas.pixelColor(291,21), doc.pageColor());
}

void TstCanvasVectorAuthoring::parametricParametersRejectInvalidValues()
{
    CanvasDocument doc;
    const auto add = [&doc](const char *kind, const QVariantMap &parameters, qreal width = 80) {
        return doc.addParametricShape(QString::fromLatin1(kind), 10, 10, width, 60, parameters,
                                      QColor("#2877c6"), {}, 0);
    };
    // Creation: an unknown kind, empty bounds and out-of-range or foreign keys fail.
    QCOMPARE(add("spiral", {}), 0ull);
    QCOMPARE(add("polygon", {}, 0), 0ull);
    QCOMPARE(add("polygon", {{"sides", 2}}), 0ull);
    QCOMPARE(add("polygon", {{"points", 5}}), 0ull); // the star's key
    QCOMPARE(add("star", {{"points", 65}}), 0ull);
    QCOMPARE(add("star", {{"innerRadius", 1.0}}), 0ull);
    QCOMPARE(add("rounded-rectangle", {{"radius", 30.5}}), 0ull); // half of 60 is 30
    QCOMPARE(doc.objectCount(), 0);
    QVERIFY(!doc.canUndo());

    const CanvasObjectId star = add("star", {});
    const CanvasObjectId polygon = add("polygon", {});
    const CanvasObjectId rounded = add("rounded-rectangle", {});
    QVERIFY(star && polygon && rounded);
    // Defaults: five tips at 0.45, six sides, radius min(18, half the shorter side).
    QCOMPARE(doc.object(star)->pathNodes.size(), 10);
    QCOMPARE(doc.parametricParameters(star).value(QStringLiteral("innerRadius")).toDouble(), 0.45);
    QCOMPARE(doc.object(polygon)->pathNodes.size(), 6);
    QCOMPARE(doc.parametricParameters(rounded).value(QStringLiteral("radius")).toDouble(), 18.0);

    const quint64 revision = doc.revision();
    const QString undoText = doc.undoText();
    const QList<QVariantMap> rejectedForStar{
        {{"points", 2}}, {{"points", 65}}, {{"points", 5.5}}, {{"points", "many"}},
        {{"innerRadius", 0}}, {{"innerRadius", 1}}, {{"innerRadius", qQNaN()}},
        {{"width", 0}}, {{"height", -4}}, {{"x", qInf()}}, {{"x", 1e308}, {"width", 1e308}},
        {{"sides", 6}}, {{"radius", 4}}, {{"kind", "polygon"}}, {{"fill", 1}}};
    for (const QVariantMap &patch : rejectedForStar)
        QVERIFY2(!doc.setParametricParameters(star, patch), qPrintable(patch.firstKey()));
    QVERIFY(!doc.setParametricParameters(polygon, {{"points", 7}}));
    QVERIFY(!doc.setParametricParameters(polygon, {{"innerRadius", 0.5}}));
    QVERIFY(!doc.setParametricParameters(rounded, {{"radius", 30.5}}));
    QVERIFY(!doc.setParametricParameters(rounded, {{"height", 30}})); // radius 18 > 15
    QVERIFY(!doc.setParametricParameters(rounded, {{"sides", 4}}));
    QCOMPARE(doc.revision(), revision); // a rejection makes no edit
    QCOMPARE(doc.undoText(), undoText);

    // The read-back definition, kind included, is an accepted unchanged patch.
    QVERIFY(doc.setParametricParameters(star, doc.parametricParameters(star)));
    QVERIFY(doc.setParametricParameters(rounded, doc.parametricParameters(rounded)));
    QCOMPARE(doc.revision(), revision);
    QVERIFY(doc.setParametricParameters(rounded, {{"height", 30}, {"radius", 15}}));
    QCOMPARE(doc.revision(), revision + 1);

    // Freeform, missing and locked objects have no editable definition.
    const CanvasObjectId rect = doc.addRect(0, 0, 10, 10, Qt::black);
    QVERIFY(doc.parametricParameters(rect).isEmpty());
    QVERIFY(!doc.setParametricParameters(rect, {{"x", 1}}));
    QVERIFY(!doc.setParametricParameters(9999, {{"x", 1}}));
    QVERIFY(doc.setObjectLocked(polygon, true));
    QVERIFY(!doc.setParametricParameters(polygon, {{"sides", 7}}));
}

void TstCanvasVectorAuthoring::parametricEditsAreReversibleAndNodeEditsDemote()
{
    CanvasDocument doc;
    const CanvasObjectId id = doc.addParametricShape(QStringLiteral("polygon"), 20, 20, 100, 80,
        {{"sides", 5}}, QColor("#2877c6"), QColor("#f5c869"), 2);
    QVERIFY(id);
    const CanvasObject parametric = *doc.object(id);

    // Count and bounds change in one step; undo and redo restore exact objects.
    QVERIFY(doc.setParametricParameters(id, {{"sides", 9}, {"x", 30}, {"width", 140}}));
    const CanvasObject edited = *doc.object(id);
    QCOMPARE(edited.pathNodes.size(), 9);
    QCOMPARE(edited.rect, QRectF(30, 20, 140, 80));
    QVERIFY(doc.undo());
    QCOMPARE(*doc.object(id), parametric);
    QVERIFY(doc.redo());
    QCOMPARE(*doc.object(id), edited);
    QVERIFY(doc.undo());

    // Picking follows the generated pentagon, not its bounds.
    const auto centre = HitTestService::hitPoint(doc, QPointF(70, 60), 0);
    QVERIFY(!centre.isEmpty());
    QCOMPARE(centre.first().object, id);
    QVERIFY(HitTestService::hitPoint(doc, QPointF(22, 22), 0).isEmpty());

    // Every direct geometry edit makes the path freeform; undo restores it.
    const QVector<std::function<bool()>> nodeEdits{
        [&] { return doc.movePathPart(id, 0, QStringLiteral("out"), 80, 12); },
        [&] { return doc.movePathPart(id, 1, QStringLiteral("anchor"), 125, 45); },
        [&] { return doc.insertPathNode(id, 1, {{"x", 100}, {"y", 25}}); },
        [&] { return doc.removePathNode(id, 4); },
        [&] { return doc.setPathClosed(id, false); },
        [&] { return doc.addPathContour(id, {QVariantMap{{"x", 60}, {"y", 50}},
                QVariantMap{{"x", 80}, {"y", 50}}, QVariantMap{{"x", 70}, {"y", 70}}}, true); }};
    for (qsizetype i = 0; i < nodeEdits.size(); ++i) {
        QVERIFY2(nodeEdits[i](), qPrintable(QString::number(i)));
        QCOMPARE(doc.object(id)->parametricKind, CanvasParametricKind::None);
        QVERIFY(doc.parametricParameters(id).isEmpty());
        QVERIFY(!doc.setParametricParameters(id, {{"sides", 6}}));
        const CanvasObject freeform = *doc.object(id);
        QVERIFY(doc.undo());
        QCOMPARE(*doc.object(id), parametric);
        QVERIFY(doc.redo());
        QCOMPARE(*doc.object(id), freeform);
        QVERIFY(doc.undo());
    }

    // Edits that leave the nodes alone keep the definition.
    const QPointF first = parametric.pathNodes.first().anchor;
    QVERIFY(doc.movePathPart(id, 0, QStringLiteral("anchor"), first.x(), first.y()));
    QVERIFY(doc.setPathClosed(id, true));
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    QVERIFY(styles.apply({QString::number(id)}, {{"fill", "#123456"}, {"join", "round"}}));
    QVERIFY(doc.moveObjects({id}, 15, 5));
    QCOMPARE(doc.object(id)->parametricKind, CanvasParametricKind::Polygon);
    QCOMPARE(doc.object(id)->pathNodes, parametric.pathNodes);
    QVERIFY(doc.setParametricParameters(id, {{"sides", 7}}));

    // A host edit that moves generated nodes but keeps the definition is
    // rejected; the same geometry is accepted once the definition is cleared.
    CanvasObject moved = *doc.object(id);
    for (QPointF *p : {&moved.pathNodes[0].anchor, &moved.pathNodes[0].in, &moved.pathNodes[0].out})
        *p += QPointF(4, 0);
    CanvasEdit edit;
    edit.changes.append({id, *doc.object(id), moved});
    const quint64 revision = doc.revision();
    const OperationError rejected = doc.commitEdit(edit);
    QVERIFY(rejected);
    QCOMPARE(rejected.fieldPath, QStringLiteral("objects.parametric"));
    QCOMPARE(doc.revision(), revision);
    moved.clearParametric();
    edit.changes = {{id, *doc.object(id), moved}};
    QVERIFY(!doc.commitEdit(edit));
    QCOMPARE(doc.object(id)->parametricKind, CanvasParametricKind::None);
}

void TstCanvasVectorAuthoring::parametricPersistenceAndOlderVersions()
{
    OperationError error;
    CanvasDocument doc;
    const CanvasObjectId star = doc.addParametricShape(QStringLiteral("star"), 10, 10, 100, 90,
        {{"points", 6}, {"innerRadius", 0.5}}, QColor("#c52d61"), QColor("#f5c869"), 3);
    QVERIFY(star);
    // A saved definition must be valid and still describe the saved nodes.
    const auto rejected = [&error](const CanvasDocumentState &state) {
        return !CanvasDocumentFormat::decode(CanvasDocumentFormat::encode(state), {}, &error)
            && error.fieldPath == QLatin1String("objects.parametric");
    };
    CanvasDocumentState state = doc.state();
    state.objects[star].pathNodes[3].anchor += QPointF(0, 2);
    QVERIFY(rejected(state));
    state = doc.state();
    state.objects[star].parametricKind = CanvasParametricKind(9);
    QVERIFY(rejected(state));
    state = doc.state();
    state.objects[star].parametricInnerRatio = 1.5;
    QVERIFY(rejected(state));
    state = doc.state();
    state.objects[star].parametricCornerRadius = 4; // stars have no corner radius
    QVERIFY(rejected(state));
    state = doc.state();
    state.objects[star].rect.setWidth(120); // bounds that no longer match the nodes
    QVERIFY(rejected(state));
    state = doc.state();
    state.objects[star].pathNodes[3].anchor.rx() += 1e-12; // platform sin/cos rounding
    QVERIFY2(CanvasDocumentFormat::decode(CanvasDocumentFormat::encode(state), {}, &error),
             qPrintable(error.message));

    // Older files: the object section ends the body and each version appended
    // one block to every object record, so a one-object version 6 file turns
    // into an older one by dropping the blocks it lacks from the end: a 4-byte
    // empty appearance block (v6), 21 bytes of parametric definition (v5), a
    // 4-byte contour count (v4), the 12-byte path/stroke block (v3) and a
    // 4-byte empty managed fill (v2). Version 1
    // also lacks the 4-byte output ICC length that follows the 44-byte
    // identity header, four 8-byte counters, the page rectangle and colour.
    const auto older = [](const QByteArray &current, quint32 version, qsizetype trailing) {
        QByteArray bytes = current.chopped(32 + trailing); // digest and newer blocks
        if (version == 1) bytes.remove(44 + 32 + 32 + 11, 4);
        qToBigEndian<quint32>(version, bytes.data() + CanvasDocumentFormat::SignatureBytes);
        return bytes + QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    };
    CanvasDocument rectDoc;
    const CanvasObjectId box = rectDoc.addRect(5, 5, 40, 30, QColor("#3a9b69"), QColor("#123456"), 2);
    CanvasDocument pathDoc;
    const CanvasObjectId path = pathDoc.addPath({QVariantMap{{"x", 5}, {"y", 5}, {"outX", 30}, {"outY", 0}},
        QVariantMap{{"x", 60}, {"y", 10}}, QVariantMap{{"x", 30}, {"y", 50}}}, true,
        QColor("#2877c6"), QColor("#f5c869"), 2);
    QVERIFY(box && path);
    const QByteArray rectBytes = CanvasDocumentFormat::encode(rectDoc.state());
    const QByteArray pathBytes = CanvasDocumentFormat::encode(pathDoc.state());
    struct Older { quint32 version; qsizetype trailing; bool path; };
    for (const Older &o : {Older{5, 4, true}, Older{4, 25, true}, Older{3, 29, true},
                           Older{5, 4, false}, Older{4, 25, false}, Older{3, 29, false},
                           Older{2, 41, false}, Older{1, 45, false}}) {
        const auto decoded = CanvasDocumentFormat::decode(
            older(o.path ? pathBytes : rectBytes, o.version, o.trailing), {}, &error);
        QVERIFY2(decoded, qPrintable(QStringLiteral("v%1: %2").arg(o.version).arg(error.message)));
        const CanvasObjectId id = o.path ? path : box;
        QCOMPARE(decoded->objects.value(id), *(o.path ? pathDoc : rectDoc).object(id));
    }
    // Older layouts have no room for the newer blocks.
    QVERIFY(!CanvasDocumentFormat::decode(older(pathBytes, 4, 4), {}, &error));
    QVERIFY(!CanvasDocumentFormat::decode(older(pathBytes, 5, 0), {}, &error));
}

void TstCanvasVectorAuthoring::roundedRectangleAtFullRadius()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 320, 120));
    const CanvasObjectId pill = doc.addParametricShape(QStringLiteral("rounded-rectangle"),
        10, 10, 160, 100, {{"radius", 50}}, QColor("#3a9b69"), QColor("#f5c869"), 6);
    const CanvasObjectId circle = doc.addParametricShape(QStringLiteral("rounded-rectangle"),
        200, 10, 100, 100, {{"radius", 50}}, QColor("#2877c6"), {}, 0);
    QVERIFY(pill && circle);
    // A zero-length side merges its two ends into one smooth node, so the
    // generated path never holds a zero-length segment.
    QCOMPARE(doc.object(pill)->pathNodes.size(), 6);
    QCOMPARE(doc.object(circle)->pathNodes.size(), 4);
    for (const CanvasObjectId id : {pill, circle}) {
        const auto &nodes = doc.object(id)->pathNodes;
        for (qsizetype i = 0; i < nodes.size(); ++i)
            QVERIFY(nodes[i].anchor != nodes[(i + 1) % nodes.size()].anchor);
    }
    const CanvasPathNode top = doc.object(circle)->pathNodes.first();
    QCOMPARE(top.anchor, QPointF(250, 10));
    QVERIFY(top.in.x() < 250 && top.out.x() > 250);
    QCOMPARE(top.in.y(), 10.0); // both handles on the tangent: a smooth node
    QCOMPARE(top.out.y(), 10.0);
    QVERIFY(!HitTestService::hitPoint(doc, QPointF(250, 60), 0).isEmpty());
    QVERIFY(HitTestService::hitPoint(doc, QPointF(203, 13), 0).isEmpty());

    const CanvasObject full = *doc.object(pill);
    QVERIFY(doc.setParametricParameters(pill, {{"radius", 49}}));
    QCOMPARE(doc.object(pill)->pathNodes.size(), 8);
    QVERIFY(doc.undo());
    QCOMPARE(*doc.object(pill), full);
}

QTEST_MAIN(TstCanvasVectorAuthoring)
#include "tst_canvas_vector_authoring.moc"
