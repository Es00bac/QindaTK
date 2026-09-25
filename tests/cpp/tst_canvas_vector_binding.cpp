// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-044 panel binding: exact intent for mixed selections, the canvas
// selection driving the style and appearance models, stroke units, fill-kind
// recall and the gradient fill, the paint entries and the colour adjustment.
#include <QtTest>
#include <QTemporaryDir>
#include "canvas_appearance_model.h"
#include "canvas_document_format.h"
#include "canvas_render.h"
#include "canvas_selection_model.h"
#include "canvas_style_model.h"
#include "gradient_editor_model.h"

using namespace QindaTK;

namespace {
QVariantList ids(std::initializer_list<CanvasObjectId> values)
{
    QVariantList list;
    for (CanvasObjectId id : values) list.append(QString::number(id));
    return list;
}

QVariantMap with(QVariantMap descriptor, const QVariantMap &patch)
{
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) descriptor.insert(it.key(), it.value());
    descriptor.insert(QStringLiteral("changed"), QStringList(patch.keys()));
    return descriptor;
}

QVariantMap fillRequest(const CanvasStyleModel &styles, const QString &kind, const QVariantMap &solidPatch = {})
{
    QVariantMap fill = styles.fillStyle();
    QStringList changed;
    if (kind != fill.value(QStringLiteral("kind")).toString()) changed << QStringLiteral("kind");
    QVariantMap settings = fill.value(QStringLiteral("settings")).toMap();
    if (!solidPatch.isEmpty()) {
        settings.insert(QStringLiteral("solid"), solidPatch);
        changed << QStringLiteral("color");
    }
    fill.insert(QStringLiteral("kind"), kind);
    fill.insert(QStringLiteral("settings"), settings);
    fill.insert(QStringLiteral("changed"), changed);
    return fill;
}
} // namespace

class TstCanvasVectorBinding : public QObject {
    Q_OBJECT
private slots:
    void mixedSelectionKeepsExactIntent();
    void canvasSelectionDrivesTheModels();
    void strokeUnitsKeepExactIntent();
    void fillKindChangesRestorePriorSettings();
    void gradientFillBindsThroughTheNestedEditor();
    void appearanceListsPaintOrderAndAdjusts();
};

void TstCanvasVectorBinding::mixedSelectionKeepsExactIntent()
{
    CanvasDocument doc;
    const CanvasObjectId a = doc.addRect(0, 0, 40, 40, QColor("#112233"), QColor("#445566"), 2);
    const CanvasObjectId b = doc.addRect(60, 0, 40, 40, QColor("#778899"), QColor("#aabbcc"), 5);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    styles.setSelectedIds(ids({a, b}));
    // The panel shows the first object's values and names the mixed fields.
    QCOMPARE(styles.strokeStyle().value(QStringLiteral("width")).toDouble(), 2.0);
    const QStringList mixed = styles.strokeStyle().value(QStringLiteral("mixed")).toStringList();
    QVERIFY(mixed.contains(QStringLiteral("width")));
    QVERIFY(mixed.contains(QStringLiteral("color")));
    QVERIFY(styles.fillStyle().value(QStringLiteral("mixed")).toStringList().contains(QStringLiteral("color")));

    // Setting the width to the shown value means "make every object 2".
    QVERIFY(styles.applyStrokeStyle(with(styles.strokeStyle(), {{"width", 2.0}})));
    QCOMPARE(doc.object(a)->strokeWidth, 2.0);
    QCOMPARE(doc.object(b)->strokeWidth, 2.0);
    QVERIFY(!styles.strokeStyle().value(QStringLiteral("mixed")).toStringList().contains(QStringLiteral("width")));
    // The same for a colour equal to the first object's.
    QVERIFY(styles.applyStrokeStyle(with(styles.strokeStyle(), {{"color", QColor("#445566")}})));
    QCOMPARE(doc.object(b)->stroke, QColor("#445566"));
    QVERIFY(styles.applyFillStyle(fillRequest(styles, QStringLiteral("solid"), {{"color", QColor("#112233")}})));
    QCOMPARE(doc.object(b)->fill, QColor("#112233"));
    // Fields the request does not name keep each object's own value.
    QVERIFY(doc.undo());
    QVERIFY(doc.undo());
    QVERIFY(doc.undo());
    QVERIFY(styles.applyStrokeStyle(with(styles.strokeStyle(), {{"cap", QStringLiteral("round")}})));
    QCOMPARE(doc.object(b)->strokeWidth, 5.0);
    QCOMPARE(doc.object(b)->stroke, QColor("#aabbcc"));
    QCOMPARE(doc.object(a)->strokeCap, Qt::RoundCap);
    QCOMPARE(doc.object(b)->strokeCap, Qt::RoundCap);
}

void TstCanvasVectorBinding::canvasSelectionDrivesTheModels()
{
    CanvasDocument doc;
    const CanvasObjectId a = doc.addRect(0, 0, 40, 40, QColor("#112233"), QColor("#445566"), 2);
    const CanvasObjectId b = doc.addEllipse(60, 0, 40, 40, QColor("#112233"), QColor("#445566"), 7);
    CanvasSelectionModel selection;
    selection.setDocument(&doc);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    styles.setSelectionModel(&selection);
    CanvasAppearanceModel appearance;
    appearance.setDocument(&doc);
    appearance.setSelectionModel(&selection);
    QCOMPARE(styles.selectionStyle().value(QStringLiteral("count")).toInt(), 0);
    QVERIFY(appearance.objectId().isEmpty());

    // What the canvas (its ToolController) selects is what the panels edit.
    selection.setSelection({b});
    QCOMPARE(styles.selectedIds(), ids({b}));
    QCOMPARE(styles.strokeStyle().value(QStringLiteral("width")).toDouble(), 7.0);
    QCOMPARE(appearance.objectId(), QString::number(b));
    QVERIFY(styles.applyStrokeStyle(with(styles.strokeStyle(), {{"width", 3.0}})));
    QCOMPARE(doc.object(b)->strokeWidth, 3.0);
    QCOMPARE(doc.object(a)->strokeWidth, 2.0);
    selection.setSelection({a, b});
    QCOMPARE(styles.selectionStyle().value(QStringLiteral("count")).toInt(), 2);
    QCOMPARE(appearance.objectId(), QString::number(a));
    // Writing the model's selection selects on the canvas.
    styles.setSelectedIds(ids({b}));
    QCOMPARE(selection.selection(), QVector<CanvasObjectId>{b});
    // Removing the selected object empties the panels.
    QVERIFY(doc.removeObject(b));
    QCOMPARE(styles.selectionStyle().value(QStringLiteral("count")).toInt(), 0);
    QVERIFY(appearance.objectId().isEmpty());
}

void TstCanvasVectorBinding::strokeUnitsKeepExactIntent()
{
    CanvasDocument doc;
    const CanvasObjectId a = doc.addRect(0, 0, 40, 40, QColor("#112233"), QColor("#445566"), 2);
    const CanvasObjectId b = doc.addRect(60, 0, 40, 40, QColor("#112233"), QColor("#445566"), 6);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    styles.setSelectedIds(ids({a}));
    QCOMPARE(styles.strokeUnits(), (QStringList{"document", "px", "pt", "mm", "in"}));
    QCOMPARE(styles.strokeStyle().value(QStringLiteral("unit")).toString(), QStringLiteral("document"));
    // Switching units is presentation: the document is untouched.
    const bool couldUndo = doc.canUndo();
    QVERIFY(styles.applyStrokeStyle(with(styles.strokeStyle(), {{"unit", QStringLiteral("pt")}})));
    QCOMPARE(doc.canUndo(), couldUndo);
    QCOMPARE(doc.object(a)->strokeWidth, 2.0);
    QCOMPARE(styles.strokeStyle().value(QStringLiteral("width")).toDouble(), 1.5);
    // 3 pt is 4 document units (1 unit = 1/96 in, as SVG export writes).
    QVERIFY(styles.applyStrokeStyle(with(styles.strokeStyle(), {{"width", 3.0}})));
    QCOMPARE(doc.object(a)->strokeWidth, 4.0);
    // A width typed in millimetres reads back exactly after a round trip.
    QVERIFY(styles.applyStrokeStyle(with(styles.strokeStyle(), {{"unit", QStringLiteral("mm")}})));
    QVERIFY(styles.applyStrokeStyle(with(styles.strokeStyle(), {{"width", 0.7}})));
    const double stored = doc.object(a)->strokeWidth;
    QCOMPARE(stored, 0.7 * 96.0 / 25.4);
    QVERIFY(styles.applyStrokeStyle(with(styles.strokeStyle(), {{"unit", QStringLiteral("in")}})));
    QVERIFY(styles.applyStrokeStyle(with(styles.strokeStyle(), {{"unit", QStringLiteral("mm")}})));
    QVERIFY(styles.strokeStyle().value(QStringLiteral("width")).toDouble() == 0.7); // exactly, not rounded
    QCOMPARE(doc.object(a)->strokeWidth, stored);
    // A mixed selection switches units without touching either width.
    styles.setSelectedIds(ids({a, b}));
    QVERIFY(styles.applyStrokeStyle(with(styles.strokeStyle(), {{"unit", QStringLiteral("px")}})));
    QCOMPARE(doc.object(a)->strokeWidth, stored);
    QCOMPARE(doc.object(b)->strokeWidth, 6.0);
    QVERIFY(!styles.applyStrokeStyle(with(styles.strokeStyle(), {{"unit", QStringLiteral("furlong")}})));
    QVERIFY(!styles.lastError().isEmpty());
}

void TstCanvasVectorBinding::fillKindChangesRestorePriorSettings()
{
    CanvasDocument doc;
    const CanvasObjectId a = doc.addRect(0, 0, 40, 40, QColor("#112233"), QColor("#445566"), 2);
    const CanvasObjectId b = doc.addRect(60, 0, 40, 40, QColor("#cc2200"), QColor("#445566"), 2);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    styles.setSelectedIds(ids({a, b}));
    // none -> solid brings back each object's own colour, not a default.
    QVERIFY(styles.applyFillStyle(fillRequest(styles, QStringLiteral("none"))));
    QVERIFY(!doc.object(a)->hasFill());
    QCOMPARE(styles.fillStyle().value(QStringLiteral("settings")).toMap().value(QStringLiteral("solid")).toMap()
                 .value(QStringLiteral("color")).value<QColor>(), QColor("#112233"));
    QVERIFY(styles.applyFillStyle(fillRequest(styles, QStringLiteral("solid"))));
    QCOMPARE(doc.object(a)->fill, QColor("#112233"));
    QCOMPARE(doc.object(b)->fill, QColor("#cc2200"));

    // A custom pattern survives pattern -> none -> pattern.
    const QVariantMap custom{{"kind", "dots"}, {"ink", "#00ff00"}, {"spacing", 7}, {"angle", 30}, {"weight", 2}};
    QVERIFY(styles.apply(ids({a}), {{"pattern", custom}}));
    styles.setSelectedIds(ids({a}));
    const CanvasPattern pattern = doc.object(a)->appearance->pattern;
    QCOMPARE(pattern.kind, CanvasPatternKind::Dots);
    QVERIFY(styles.fillStyle().value(QStringLiteral("settings")).toMap().value(QStringLiteral("pattern")).toMap()
                .value(QStringLiteral("custom")).toBool());
    QVERIFY(styles.applyFillStyle(fillRequest(styles, QStringLiteral("none"))));
    QCOMPARE(doc.object(a)->appearance->pattern.kind, CanvasPatternKind::None);
    QVERIFY(styles.applyFillStyle(fillRequest(styles, QStringLiteral("pattern"))));
    QCOMPARE(doc.object(a)->appearance->pattern, pattern);
    QCOMPARE(doc.object(a)->fill, QColor("#112233")); // the colour behind the ink too
    // pattern -> solid keeps the colour and drops the ink; undo restores it.
    QVERIFY(styles.applyFillStyle(fillRequest(styles, QStringLiteral("solid"))));
    QCOMPARE(doc.object(a)->appearance->pattern.kind, CanvasPatternKind::None);
    QVERIFY(doc.undo());
    QCOMPARE(doc.object(a)->appearance->pattern, pattern);
    // A null colour clears its channel (it once became Qt::color0, white).
    QVERIFY(styles.apply(ids({b}), {{"stroke", QVariant()}}));
    QVERIFY(!doc.object(b)->hasStroke());
    QVERIFY(doc.undo());
    // Choosing a colour explicitly still applies it to every object.
    styles.setSelectedIds(ids({a, b}));
    QVERIFY(styles.applyFillStyle(fillRequest(styles, QStringLiteral("solid"), {{"color", QColor("#0000ff")}})));
    QCOMPARE(doc.object(a)->fill, QColor("#0000ff"));
    QCOMPARE(doc.object(b)->fill, QColor("#0000ff"));
}

void TstCanvasVectorBinding::gradientFillBindsThroughTheNestedEditor()
{
    CanvasDocument doc;
    const CanvasObjectId a = doc.addRect(0, 0, 40, 40, QColor("#112233"), {}, 0);
    const CanvasObjectId line = doc.addPolyline({QPointF(0, 60), QPointF(40, 60)}, QColor(Qt::black), 1);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    styles.setSelectedIds(ids({a}));
    // Without a gradient editor or an earlier gradient, the refusal says why.
    QVERIFY(!styles.applyFillStyle(fillRequest(styles, QStringLiteral("gradient"))));
    QVERIFY(styles.lastError().contains(QStringLiteral("gradient")));
    GradientEditorModel gradient;
    QVERIFY(gradient.addStop(0.5));
    styles.setGradientEditor(&gradient);
    QVERIFY(styles.applyFillStyle(fillRequest(styles, QStringLiteral("gradient"))));
    const QByteArray payload = doc.object(a)->managedFill;
    QVERIFY(!payload.isEmpty());
    QCOMPARE(styles.fillStyle().value(QStringLiteral("kind")).toString(), QStringLiteral("gradient"));
    const QVariantMap summary = styles.fillStyle().value(QStringLiteral("settings")).toMap()
                                    .value(QStringLiteral("gradient")).toMap().value(QStringLiteral("gradient")).toMap();
    QCOMPARE(summary.value(QStringLiteral("stops")).toList().size(), 3);
    // gradient -> solid -> gradient restores the object's own gradient even
    // after the nested editor has moved on.
    QVERIFY(styles.applyFillStyle(fillRequest(styles, QStringLiteral("solid"))));
    QVERIFY(doc.object(a)->managedFill.isEmpty());
    QCOMPARE(doc.object(a)->fill, QColor("#112233"));
    GradientEditorModel other;
    styles.setGradientEditor(&other);
    QVERIFY(styles.applyFillStyle(fillRequest(styles, QStringLiteral("gradient"))));
    QCOMPARE(doc.object(a)->managedFill, payload);
    // Selecting a gradient object loads its gradient into the nested editor.
    QCOMPARE(other.stops().size(), 3);
    // An open polyline cannot take a gradient: the whole request is refused.
    styles.setSelectedIds(ids({a, line}));
    QVERIFY(styles.applyFillStyle(fillRequest(styles, QStringLiteral("none"))));
    QVERIFY(!styles.applyFillStyle(fillRequest(styles, QStringLiteral("gradient"))));
    QVERIFY(doc.object(a)->managedFill.isEmpty());
    QVERIFY(doc.object(line)->managedFill.isEmpty());
}

void TstCanvasVectorBinding::appearanceListsPaintOrderAndAdjusts()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 100, 100));
    const CanvasObjectId id = doc.addRect(10, 10, 60, 60, QColor(200, 100, 50), QColor("#223344"), 4);
    CanvasAppearanceModel model;
    model.setDocument(&doc);
    model.setObjectId(QString::number(id));
    // Fill, pattern and stroke are inspectable in paint order.
    const QVariantList paints = model.paints();
    QCOMPARE(paints.size(), 3);
    QCOMPARE(paints.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("fill"));
    QCOMPARE(paints.at(1).toMap().value(QStringLiteral("present")).toBool(), false);
    QCOMPARE(paints.at(2).toMap().value(QStringLiteral("detail")).toString(), QStringLiteral("4 center, #223344"));
    QVERIFY(model.supportedKinds().contains(QStringLiteral("color-adjust")));

    // A nondestructive adjustment: saturation 0 greys the painted pixels by
    // the SVG luminance weights; bypass and removal restore them.
    const QString adjust = model.addEffect(QStringLiteral("color-adjust"), {{"saturation", 0}});
    QCOMPARE(adjust, QStringLiteral("color-adjust-1"));
    QCOMPARE(model.effect(adjust).value(QStringLiteral("name")).toString(), QStringLiteral("Colour adjustment"));
    const auto pixel = [&doc] {
        return CanvasPainter::renderTile(doc, QRectF(0, 0, 100, 100), 100, {}).pixelColor(40, 40);
    };
    const int grey = int(std::lround(0.213 * 200 + 0.715 * 100 + 0.072 * 50));
    QCOMPARE(pixel(), QColor(grey, grey, grey));
    // A partial parameter map keeps the other values (saturation stays 0).
    QVERIFY(model.setEffectParameters(adjust, {{"contrast", 2}}));
    const QVariantMap parameters = model.effect(adjust).value(QStringLiteral("parameters")).toMap();
    QCOMPARE(parameters.value(QStringLiteral("saturation")).toDouble(), 0.0);
    const int contrasted = int(std::lround(std::clamp(grey / 255.0 * 2 - 0.5, 0.0, 1.0) * 255));
    QVERIFY(std::abs(pixel().red() - contrasted) <= 1);
    QVERIFY(!model.setEffectParameters(adjust, {{"saturation", 2}}));
    QVERIFY(model.setEffectEnabled(adjust, false));
    QCOMPARE(pixel(), QColor(200, 100, 50));
    QVERIFY(doc.undo());
    QVERIFY(std::abs(pixel().red() - contrasted) <= 1);

    // It persists and exports as SVG filter primitives.
    OperationError error;
    const auto state = CanvasDocumentFormat::decode(CanvasDocumentFormat::encode(doc.state(), &error), {}, &error);
    QVERIFY2(state.has_value(), qPrintable(error.message));
    QCOMPARE(state->objects.value(id), *doc.object(id));
    QTemporaryDir dir;
    const QString svg = dir.filePath(QStringLiteral("adjust.svg"));
    QVERIFY(doc.exportSvg(svg));
    QFile file(svg);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray text = file.readAll();
    QVERIFY(text.contains("<feColorMatrix"));
    QVERIFY(text.contains("type=\"saturate\""));
    QVERIFY(text.contains("<feComponentTransfer"));
    QVERIFY(text.contains("slope=\"2\""));
    QVERIFY(text.contains("intercept=\"-0.5\""));
}

QTEST_MAIN(TstCanvasVectorBinding)
#include "tst_canvas_vector_binding.moc"
