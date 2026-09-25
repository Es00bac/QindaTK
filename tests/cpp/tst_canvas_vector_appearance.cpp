// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-044 appearance: stroke alignment, pattern fills, ordered effects,
// opacity and blend, their persistence and the style/appearance models.
#include <QtTest>
#include <QMimeData>
#include <QPainter>
#include <QSignalSpy>
#include <functional>
#include <memory>
#include "canvas_appearance.h"
#include "canvas_appearance_model.h"
#include "canvas_document_format.h"
#include "canvas_geometry.h"
#include "canvas_render.h"
#include "canvas_style_model.h"
#include "clipboard_service.h"
#include "hit_test_service.h"

using namespace QindaTK;

namespace {
QImage render(const CanvasDocument &doc, qreal side)
{
    return CanvasPainter::renderTile(doc, QRectF(0, 0, side, side), int(side), {});
}

bool near(const QColor &actual, const QColor &expected, int tolerance)
{
    const bool close = std::abs(actual.red() - expected.red()) <= tolerance
        && std::abs(actual.green() - expected.green()) <= tolerance
        && std::abs(actual.blue() - expected.blue()) <= tolerance
        && std::abs(actual.alpha() - expected.alpha()) <= tolerance;
    if (!close) qWarning() << "colour" << actual << "expected about" << expected;
    return close;
}

QVariantList ids(std::initializer_list<CanvasObjectId> values)
{
    QVariantList list;
    for (CanvasObjectId id : values) list.append(QString::number(id));
    return list;
}

class MemoryClipboard final : public ClipboardAdapter {
public:
    const QMimeData *mimeData() const override { return data.get(); }
    bool setMimeData(QMimeData *value) override { data.reset(value); emit changed(); return true; }
    std::unique_ptr<QMimeData> data;
};
} // namespace

class TstCanvasVectorAppearance : public QObject {
    Q_OBJECT
private slots:
    void strokeAlignmentPaintsOneSide();
    void patternFillDrawsInkOverTheFill();
    void effectsRenderInStackOrder();
    void blurOpacityAndBlend();
    void appearancePersistsAndInvalidRecordsAreRejected();
    void styleModelBindsTheSelection();
    void appearanceModelEditsTheStack();
};

void TstCanvasVectorAppearance::strokeAlignmentPaintsOneSide()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 200, 200));
    const QColor fill("#2060c0"), stroke("#d02020"), page(Qt::white);
    const CanvasObjectId inside = doc.addRect(20, 20, 60, 60, fill, stroke, 10);
    const CanvasObjectId outside = doc.addRect(120, 20, 60, 60, fill, stroke, 10);
    QVERIFY(inside && outside);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    QVERIFY(styles.apply(ids({inside}), {{"alignment", "inside"}}));
    QVERIFY(styles.apply(ids({outside}), {{"alignment", "outside"}, {"miterLimit", 8}}));
    QCOMPARE(doc.object(inside)->appearance->strokeAlignment, CanvasStrokeAlignment::Inside);
    QCOMPARE(doc.object(outside)->appearance->miterLimit, 8.0);
    // Bounds follow the painted side: nothing outside an inside stroke, the
    // whole width outside an outside stroke.
    QCOMPARE(CanvasGeometry::documentBounds(*doc.object(inside)), QRectF(20, 20, 60, 60));
    QCOMPARE(CanvasGeometry::documentBounds(*doc.object(outside)), QRectF(110, 10, 80, 80));

    const QImage tile = render(doc, 200);
    QCOMPARE(tile.pixelColor(24, 50), stroke);  // inside band x 20..30
    QCOMPARE(tile.pixelColor(40, 50), fill);
    QCOMPARE(tile.pixelColor(15, 50), page);
    QCOMPARE(tile.pixelColor(114, 50), stroke); // outside band x 110..120
    QCOMPARE(tile.pixelColor(124, 50), fill);   // the fill reaches the edge
    QCOMPARE(tile.pixelColor(105, 50), page);
    // Picking reaches the whole aligned width on the painted side.
    const QVector<CanvasHit> hits = HitTestService::hitPoint(doc, QPointF(112, 50), 0);
    QVERIFY(!hits.isEmpty());
    QCOMPARE(hits.first().object, outside);

    QVERIFY(doc.undo());
    QCOMPARE(doc.object(outside)->appearance->strokeAlignment, CanvasStrokeAlignment::Center);
    QCOMPARE(render(doc, 200).pixelColor(114, 50), page); // centred band is 115..125
    // An open path has no side to align to: the whole selection is refused.
    const CanvasObjectId open = doc.addPath({QVariantMap{{"x", 10}, {"y", 150}},
                                             QVariantMap{{"x", 90}, {"y", 150}}},
                                            false, {}, QColor(Qt::black), 2);
    QVERIFY(open);
    const CanvasObject before = *doc.object(inside);
    QVERIFY(!styles.apply(ids({inside, open}), {{"alignment", "outside"}}));
    QVERIFY(!styles.lastError().isEmpty());
    QCOMPARE(*doc.object(inside), before);
    QVERIFY(!styles.apply(ids({inside}), {{"alignment", "sideways"}}));
    QVERIFY(!styles.apply(ids({inside}), {{"miterLimit", 0.5}}));
}

void TstCanvasVectorAppearance::patternFillDrawsInkOverTheFill()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 100, 100));
    const QColor fill("#3060a0"), ink("#f0c000");
    const CanvasObjectId id = doc.addRect(10, 10, 80, 80, fill, {}, 0);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    QVERIFY(styles.apply(ids({id}), {{"pattern", QVariantMap{{"kind", "lines"}, {"ink", "#f0c000"},
                                                                {"spacing", 10}, {"weight", 4}}}}));
    const CanvasPattern pattern = doc.object(id)->appearance->pattern;
    QCOMPARE(pattern.kind, CanvasPatternKind::Lines);
    QCOMPARE(pattern.ink, ink);
    QCOMPARE(pattern.spacing, 10.0);
    QCOMPARE(pattern.weight, 4.0);
    QCOMPARE(pattern.angle, 0.0);
    QImage tile = render(doc, 100);
    // Local bands are y in [10k + 3, 10k + 7]; the fill shows between them.
    QCOMPARE(tile.pixelColor(50, 14), ink);
    QCOMPARE(tile.pixelColor(50, 20), fill);
    QCOMPARE(tile.pixelColor(5, 14), QColor(Qt::white));
    // Zoomed far out, cells under two pixels draw as their average coverage.
    const QImage far = CanvasPainter::renderTile(doc, QRectF(0, 0, 1000, 1000), 100, {});
    QVERIFY(near(far.pixelColor(5, 5), QColor(125, 134, 96), 3));

    // Without a fill colour the pattern still covers the area for picking.
    QVERIFY(styles.apply(ids({id}), {{"fill", QVariant()}}));
    QVERIFY(CanvasGeometry::fillContains(*doc.object(id), QPointF(50, 50)));
    tile = render(doc, 100);
    QCOMPARE(tile.pixelColor(50, 14), ink);
    QCOMPARE(tile.pixelColor(50, 20), QColor(Qt::white));
    QVERIFY(doc.undo());

    // A swatch takes its ink from the stroke; null clears; undo restores.
    const CanvasObjectId dotted = doc.addEllipse(10, 10, 30, 30, fill, QColor("#204020"), 1);
    QVERIFY(styles.apply(ids({dotted}), {{"pattern", "dots"}}));
    const CanvasPattern dots = doc.object(dotted)->appearance->pattern;
    QCOMPARE(dots.kind, CanvasPatternKind::Dots);
    QCOMPARE(dots.ink, QColor("#204020"));
    QCOMPARE(dots.spacing, 8.0);
    QCOMPARE(dots.weight, 3.0);
    QCOMPARE(CanvasAppearanceRules::swatchId(dots), QStringLiteral("dots"));
    QVERIFY(styles.apply(ids({dotted}), {{"pattern", QVariant()}}));
    QVERIFY(doc.object(dotted)->appearance->pattern == CanvasPattern{});
    QVERIFY(doc.undo());
    QCOMPARE(doc.object(dotted)->appearance->pattern, dots);
    // Unknown swatches, overweight lines and open shapes are refused.
    QVERIFY(!styles.apply(ids({dotted}), {{"pattern", "plaid"}}));
    QVERIFY(!styles.apply(ids({dotted}), {{"pattern", QVariantMap{{"kind", "lines"}, {"spacing", 4}, {"weight", 5}}}}));
    const CanvasObjectId line = doc.addPolyline({QPointF(0, 0), QPointF(20, 20)}, QColor(Qt::black), 1);
    QVERIFY(!styles.apply(ids({line}), {{"pattern", "hatch"}}));
    QCOMPARE(doc.object(line)->appearance->pattern.kind, CanvasPatternKind::None);
}

void TstCanvasVectorAppearance::effectsRenderInStackOrder()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 128, 128));
    const CanvasObjectId id = doc.addRect(16, 16, 32, 32, QColor(Qt::black), {}, 0);
    CanvasAppearanceModel model;
    model.setDocument(&doc);
    model.setObjectId(QString::number(id));
    const QString right = model.addEffect(QStringLiteral("drop-shadow"),
        {{"offsetX", 32}, {"offsetY", 0}, {"blur", 0}, {"color", "#ffff0000"}});
    const QString down = model.addEffect(QStringLiteral("drop-shadow"),
        {{"offsetX", 0}, {"offsetY", 32}, {"blur", 0}, {"color", "#ff0000ff"}});
    QCOMPARE(right, QStringLiteral("drop-shadow-1"));
    QCOMPARE(down, QStringLiteral("drop-shadow-2"));
    QCOMPARE(model.effects().size(), 2);
    // Painted bounds grow by both shadows; selection bounds do not.
    QCOMPARE(CanvasGeometry::paintBounds(*doc.object(id)), QRectF(16, 16, 64, 64));
    QCOMPARE(CanvasGeometry::documentBounds(*doc.object(id)), QRectF(16, 16, 32, 32));

    // The second shadow copies everything above it, the first shadow included.
    QImage tile = render(doc, 128);
    QCOMPARE(tile.pixelColor(30, 30), QColor(Qt::black));
    QCOMPARE(tile.pixelColor(60, 30), QColor(Qt::red));
    QCOMPARE(tile.pixelColor(30, 60), QColor(Qt::blue));
    QCOMPARE(tile.pixelColor(60, 60), QColor(Qt::blue));
    QCOMPARE(tile.pixelColor(100, 100), QColor(Qt::white));
    QVERIFY(model.moveEffect(down, 0));
    QCOMPARE(model.effects().first().toMap().value(QStringLiteral("id")).toString(), down);
    tile = render(doc, 128);
    QCOMPARE(tile.pixelColor(60, 60), QColor(Qt::red));
    QCOMPARE(tile.pixelColor(30, 60), QColor(Qt::blue));
    QCOMPARE(tile.pixelColor(60, 30), QColor(Qt::red));
    QVERIFY(doc.undo());
    QCOMPARE(model.effects().first().toMap().value(QStringLiteral("id")).toString(), right);
    QCOMPARE(render(doc, 128).pixelColor(60, 60), QColor(Qt::blue));

    // Bypass keeps the effect in the stack but stops drawing it.
    QVERIFY(model.setEffectEnabled(right, false));
    QCOMPARE(model.effect(right).value(QStringLiteral("enabled")).toBool(), false);
    tile = render(doc, 128);
    QCOMPARE(tile.pixelColor(60, 30), QColor(Qt::white));
    QCOMPARE(tile.pixelColor(30, 60), QColor(Qt::blue));
    QCOMPARE(tile.pixelColor(60, 60), QColor(Qt::white));
    QCOMPARE(CanvasGeometry::paintBounds(*doc.object(id)), QRectF(16, 16, 32, 64));
    QVERIFY(doc.undo());
    QCOMPARE(render(doc, 128).pixelColor(60, 30), QColor(Qt::red));
}

void TstCanvasVectorAppearance::blurOpacityAndBlend()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 128, 128));
    const CanvasObjectId id = doc.addRect(16, 16, 32, 32, QColor(Qt::black), {}, 0);
    CanvasAppearanceModel model;
    model.setDocument(&doc);
    model.setObjectId(QString::number(id));
    const QString blur = model.addEffect(QStringLiteral("gaussian-blur"), {{"radius", 2}});
    QCOMPARE(blur, QStringLiteral("gaussian-blur-1"));
    QCOMPARE(CanvasGeometry::paintBounds(*doc.object(id)), QRectF(10, 10, 44, 44));
    QImage tile = render(doc, 128);
    // Sigma 2 px gives boxes 4, 4 and 5 (reach 5 px): the centre keeps its
    // colour, two pixels outside the edge holds 64/255 black, 12 px out none.
    QCOMPARE(tile.pixelColor(32, 32), QColor(Qt::black));
    QVERIFY(near(tile.pixelColor(14, 32), QColor(191, 191, 191), 1));
    QCOMPARE(tile.pixelColor(4, 32), QColor(Qt::white));
    QVERIFY(model.setEffectEnabled(blur, false));
    QCOMPARE(render(doc, 128).pixelColor(14, 32), QColor(Qt::white));

    CanvasDocument mixed;
    mixed.setPageRect(QRectF(0, 0, 128, 128));
    QVERIFY(mixed.addRect(0, 0, 64, 128, QColor(Qt::red), {}, 0));
    const CanvasObjectId grey = mixed.addRect(32, 0, 64, 64, QColor(128, 128, 128), {}, 0);
    const CanvasObjectId blue = mixed.addRect(32, 64, 64, 64, QColor(Qt::blue), {}, 0);
    CanvasStyleModel styles;
    styles.setDocument(&mixed);
    QVERIFY(styles.apply(ids({grey}), {{"blend", "multiply"}}));
    QVERIFY(styles.apply(ids({blue}), {{"opacity", 0.5}}));
    QCOMPARE(mixed.object(grey)->appearance->blend, CanvasBlendMode::Multiply);
    const QImage composite = render(mixed, 128);
    QVERIFY(near(composite.pixelColor(48, 32), QColor(128, 0, 0), 2));     // grey x red
    QVERIFY(near(composite.pixelColor(80, 32), QColor(128, 128, 128), 2)); // grey x white
    QVERIFY(near(composite.pixelColor(48, 96), QColor(128, 0, 127), 2));   // half blue over red
    QVERIFY(near(composite.pixelColor(80, 96), QColor(127, 127, 255), 2)); // half blue over white
    QVERIFY(!styles.apply(ids({grey}), {{"blend", "hue"}}));
    QVERIFY(!styles.apply(ids({grey}), {{"opacity", 1.5}}));
}

void TstCanvasVectorAppearance::appearancePersistsAndInvalidRecordsAreRejected()
{
    CanvasDocument doc;
    const CanvasObjectId id = doc.addParametricShape(QStringLiteral("star"), 10, 10, 100, 100,
        {{"points", 5}, {"innerRadius", 0.5}}, QColor("#c52d61"), QColor("#f5c869"), 3);
    QVERIFY(id);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    QVERIFY(styles.apply(ids({id}), {{"alignment", "outside"}, {"miterLimit", 6}, {"opacity", 0.75},
        {"blend", "screen"}, {"pattern", QVariantMap{{"kind", "grid"}, {"ink", "#80102030"},
                                                     {"spacing", 12}, {"angle", 30}, {"weight", 2}}}}));
    CanvasAppearanceModel model;
    model.setDocument(&doc);
    model.setObjectId(QString::number(id));
    QVERIFY(!model.addEffect(QStringLiteral("drop-shadow"),
        {{"offsetX", 3}, {"offsetY", -2}, {"blur", 1.5}, {"color", "#40000000"}}).isEmpty());
    QVERIFY(!model.addEffect(QStringLiteral("gaussian-blur"), {{"radius", 0.5}}).isEmpty());
    const QString glow = model.addEffect(QStringLiteral("outer-glow"),
        {{"size", 3}, {"tint", "gold"}, {"stops", QVariantList{0.25, 1}}});
    QCOMPARE(glow, QStringLiteral("outer-glow-1"));
    QCOMPARE(model.effect(glow).value(QStringLiteral("supported")).toBool(), false);
    QVERIFY(!model.effect(glow).value(QStringLiteral("unsupportedReason")).toString().isEmpty());
    QCOMPARE(model.effect(glow).value(QStringLiteral("parameters")).toMap()
                 .value(QStringLiteral("tint")).toString(), QStringLiteral("gold"));
    const CanvasObject styled = *doc.object(id);
    QCOMPARE(styled.appearance->effects.size(), 3);

    OperationError error;
    const QByteArray bytes = CanvasDocumentFormat::encode(doc.state(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error.message));
    const auto state = CanvasDocumentFormat::decode(bytes, {}, &error);
    QVERIFY2(state.has_value(), qPrintable(error.message));
    QCOMPARE(state->objects.value(id), styled);
    const auto reopened = CanvasDocument::fromState(*state);
    QVERIFY(reopened);
    QCOMPARE(*reopened->object(id), styled);

    // The block codec refuses truncated or padded blocks.
    CanvasAppearance decoded;
    const QByteArray block = CanvasAppearanceCodec::encode(*styled.appearance);
    QVERIFY(CanvasAppearanceCodec::encode(CanvasAppearance{}).isEmpty());
    QVERIFY(!CanvasAppearanceCodec::decode(block, &decoded));
    QCOMPARE(decoded, *styled.appearance);
    QVERIFY(CanvasAppearanceCodec::decode(block.chopped(3), &decoded));
    QVERIFY(CanvasAppearanceCodec::decode(block + QByteArray("x"), &decoded));

    // Decoding validates every appearance field.
    const auto rejected = [&](const std::function<void(CanvasObject &)> &tamper, const char *field) {
        CanvasDocumentState s = doc.state();
        tamper(s.objects[id]);
        OperationError failure;
        const bool refused = !CanvasDocumentFormat::decode(CanvasDocumentFormat::encode(s), {}, &failure);
        if (!refused || failure.fieldPath != QLatin1String(field))
            qWarning() << field << "got" << failure.fieldPath << failure.message;
        return refused && failure.fieldPath == QLatin1String(field);
    };
    const auto tampered = [](const std::function<void(CanvasAppearance &)> &change) {
        return [change](CanvasObject &o) { o.appearance.update(change); };
    };
    QVERIFY(rejected(tampered([](CanvasAppearance &a) { a.opacity = 1.5; }), "objects.appearance.opacity"));
    QVERIFY(rejected(tampered([](CanvasAppearance &a) { a.blend = CanvasBlendMode(40); }), "objects.appearance.blend"));
    QVERIFY(rejected(tampered([](CanvasAppearance &a) { a.effects[1].id = a.effects[0].id; }),
                     "objects.appearance.effects.id"));
    QVERIFY(rejected(tampered([](CanvasAppearance &a) { a.effects[0].parameters = "{\"blur\":1}"; }),
                     "objects.appearance.effects.parameters"));
    QVERIFY(rejected(tampered([](CanvasAppearance &a) { a.effects[2].parameters = "{ \"size\": 3 }"; }),
                     "objects.appearance.effects.parameters"));
    QVERIFY(rejected(tampered([](CanvasAppearance &a) { a.pattern.weight = 20; }), "objects.appearance.pattern.spacing"));
    // Commits apply the same rules.
    CanvasObject bad = styled;
    bad.appearance.update([](CanvasAppearance &a) { a.miterLimit = 0.5; });
    CanvasEdit edit;
    edit.label = QStringLiteral("Invalid miter limit");
    edit.changes.append({id, styled, bad});
    const OperationError refused = doc.commitEdit(edit);
    QVERIFY(bool(refused));
    QCOMPARE(refused.fieldPath, QStringLiteral("objects.appearance.miterLimit"));
    QCOMPARE(*doc.object(id), styled);
}

void TstCanvasVectorAppearance::styleModelBindsTheSelection()
{
    CanvasDocument doc;
    const CanvasObjectId a = doc.addRect(0, 0, 40, 40, QColor("#112233"), QColor("#445566"), 2);
    const CanvasObjectId b = doc.addEllipse(60, 0, 40, 40, QColor("#112233"), QColor("#445566"), 5);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    QSignalSpy changed(&styles, &CanvasStyleModel::selectionStyleChanged);
    styles.setSelectedIds(ids({a, b}));
    QVERIFY(changed.count() >= 1);
    QCOMPARE(styles.selectionStyle().value(QStringLiteral("count")).toInt(), 2);
    QVERIFY(styles.selectionStyle().value(QStringLiteral("mixed")).toStringList().contains(QStringLiteral("strokeWidth")));
    QCOMPARE(styles.strokeStyle().value(QStringLiteral("width")).toDouble(), 2.0);
    QVERIFY(styles.strokeStyle().value(QStringLiteral("mixed")).toStringList().contains(QStringLiteral("width")));
    QCOMPARE(styles.fillStyle().value(QStringLiteral("kind")).toString(), QStringLiteral("solid"));

    // A panel request applies only the field that changed: widths stay mixed.
    QVariantMap stroke = styles.strokeStyle();
    stroke.insert(QStringLiteral("alignment"), QStringLiteral("inside"));
    QVERIFY(styles.applyStrokeStyle(stroke));
    QCOMPARE(doc.object(a)->appearance->strokeAlignment, CanvasStrokeAlignment::Inside);
    QCOMPARE(doc.object(b)->appearance->strokeAlignment, CanvasStrokeAlignment::Inside);
    QCOMPARE(doc.object(b)->strokeWidth, 5.0);
    QCOMPARE(styles.strokeStyle().value(QStringLiteral("alignment")).toString(), QStringLiteral("inside"));
    QVERIFY(doc.undo()); // the panel follows undo
    QCOMPARE(styles.strokeStyle().value(QStringLiteral("alignment")).toString(), QStringLiteral("center"));
    stroke = styles.strokeStyle();
    stroke.insert(QStringLiteral("width"), 3.0);
    QVERIFY(styles.applyStrokeStyle(stroke));
    QCOMPARE(doc.object(b)->strokeWidth, 3.0);
    QVERIFY(!styles.strokeStyle().value(QStringLiteral("mixed")).toStringList().contains(QStringLiteral("width")));
    // Choosing "custom" before typing values keeps the custom field offered.
    stroke = styles.strokeStyle();
    stroke.insert(QStringLiteral("dashMode"), QStringLiteral("custom"));
    QVERIFY(styles.applyStrokeStyle(stroke));
    QCOMPARE(styles.strokeStyle().value(QStringLiteral("dashMode")).toString(), QStringLiteral("custom"));
    QVERIFY(doc.object(a)->dashPattern.isEmpty());

    // Fill kinds: a pattern swatch inks with the stroke; solid clears it again.
    QVariantMap fill = styles.fillStyle();
    fill.insert(QStringLiteral("kind"), QStringLiteral("pattern"));
    QVERIFY(styles.applyFillStyle(fill));
    QCOMPARE(doc.object(a)->appearance->pattern.kind, CanvasPatternKind::Lines);
    QCOMPARE(doc.object(b)->appearance->pattern.ink, QColor("#445566"));
    QCOMPARE(styles.fillStyle().value(QStringLiteral("kind")).toString(), QStringLiteral("pattern"));
    QCOMPARE(styles.fillStyle().value(QStringLiteral("settings")).toMap().value(QStringLiteral("pattern"))
                 .toMap().value(QStringLiteral("patternId")).toString(), QStringLiteral("hatch"));
    fill = styles.fillStyle();
    fill.insert(QStringLiteral("kind"), QStringLiteral("solid"));
    QVERIFY(styles.applyFillStyle(fill));
    QCOMPARE(doc.object(a)->appearance->pattern.kind, CanvasPatternKind::None);
    QCOMPARE(doc.object(a)->fill, QColor("#112233"));
    fill.insert(QStringLiteral("kind"), QStringLiteral("gradient"));
    QVERIFY(!styles.applyFillStyle(fill));
    QCOMPARE(styles.patterns().size(), 5);
    QCOMPARE(styles.blendModes().size(), 12);
    QCOMPARE(styles.styleOf(ids({b})).value(QStringLiteral("count")).toInt(), 1);
}

void TstCanvasVectorAppearance::appearanceModelEditsTheStack()
{
    CanvasDocument doc;
    const CanvasObjectId id = doc.addPath({QVariantMap{{"x", 10}, {"y", 10}}, QVariantMap{{"x", 60}, {"y", 10}},
                                           QVariantMap{{"x", 60}, {"y", 60}}, QVariantMap{{"x", 10}, {"y", 60}}},
                                          true, QColor("#2877c6"), {}, 0);
    QVERIFY(id);
    CanvasAppearanceModel model;
    QSignalSpy effectsChanged(&model, &CanvasAppearanceModel::effectsChanged);
    model.setDocument(&doc);
    QVERIFY(model.addEffect(QStringLiteral("drop-shadow")).isEmpty()); // no object yet
    model.setObjectId(QString::number(id));
    QVERIFY(model.editable());
    QCOMPARE(model.supportedKinds(), (QStringList{QStringLiteral("drop-shadow"), QStringLiteral("gaussian-blur"),
                                                  QStringLiteral("color-adjust")}));
    const QString shadow = model.addEffect(QStringLiteral("drop-shadow"));
    QCOMPARE(shadow, QStringLiteral("drop-shadow-1"));
    const QVariantMap record = model.effects().first().toMap();
    QCOMPARE(record.value(QStringLiteral("name")).toString(), QStringLiteral("Drop shadow"));
    QCOMPARE(record.value(QStringLiteral("kind")).toString(), QStringLiteral("drop-shadow"));
    QVERIFY(record.value(QStringLiteral("supported")).toBool());
    QCOMPARE(record.value(QStringLiteral("masked")).toBool(), false);
    const QVariantMap defaults = record.value(QStringLiteral("parameters")).toMap();
    QCOMPARE(defaults.value(QStringLiteral("offsetY")).toDouble(), 4.0);
    QCOMPARE(defaults.value(QStringLiteral("color")).toString(), QStringLiteral("#80000000"));
    const QString blur = model.addEffect(QStringLiteral("gaussian-blur"), {{"radius", 3}});
    QCOMPARE(model.effects().size(), 2);

    QVERIFY(!model.setEffectParameters(shadow, {{"blur", -1}}));
    QVERIFY(!model.lastError().isEmpty());
    QVERIFY(!model.setEffectParameters(shadow, {{"spread", 2}}));
    QVERIFY(model.setEffectParameters(shadow, {{"offsetX", 2}, {"offsetY", 2}, {"blur", 1}, {"color", "#ff00ff00"}}));
    QVERIFY(model.lastError().isEmpty());
    QCOMPARE(model.effect(shadow).value(QStringLiteral("parameters")).toMap()
                 .value(QStringLiteral("color")).toString(), QStringLiteral("#ff00ff00"));
    QVERIFY(model.removeEffect(blur));
    QCOMPARE(model.effects().size(), 1);
    QVERIFY(doc.undo());
    QCOMPARE(model.effects().size(), 2);
    QVERIFY(!model.moveEffect(shadow, 5));
    QVERIFY(!model.removeEffect(QStringLiteral("missing")));
    QVERIFY(!model.addEffect(QStringLiteral("bad kind!")).size());

    // A locked object is read-only.
    QVERIFY(doc.setObjectLocked(id, true));
    QVERIFY(!model.editable());
    QVERIFY(model.addEffect(QStringLiteral("gaussian-blur")).isEmpty());
    QVERIFY(doc.setObjectLocked(id, false));
    QVERIFY(effectsChanged.count() > 0);

    // Clipboard copies carry the whole appearance.
    MemoryClipboard adapter;
    ClipboardService clipboard;
    clipboard.setAdapter(&adapter);
    QVERIFY(clipboard.copyCanvasPaths(&doc, {QString::number(id)}));
    const QStringList pasted = clipboard.pasteCanvasPaths(&doc, 10, 10);
    QCOMPARE(pasted.size(), 1);
    QCOMPARE(doc.object(pasted.first().toULongLong())->appearance, doc.object(id)->appearance);
}

QTEST_MAIN(TstCanvasVectorAppearance)
#include "tst_canvas_vector_appearance.moc"
