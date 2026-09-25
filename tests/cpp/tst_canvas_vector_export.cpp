// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-044 SVG output of appearance: pattern cells, masked aligned strokes,
// chained filter effects, opacity and blend groups, transform-accurate stroke
// widths, and explicit refusal of effects SVG export cannot represent.
#include <QtTest>
#include <QPainter>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include "canvas_appearance_model.h"
#include "canvas_render.h"
#include "canvas_style_model.h"
#include "canvas_svg_export.h"

using namespace QindaTK;

namespace {
struct Element {
    QString name;
    QXmlStreamAttributes attributes;
    int parent = -1; // index of the enclosing element
    QString value(const char *key) const { return attributes.value(QLatin1String(key)).toString(); }
};

QVector<Element> parse(const QByteArray &svg)
{
    QVector<Element> elements;
    QVector<int> open;
    QXmlStreamReader reader(svg);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            elements.append({reader.name().toString(), reader.attributes(), open.isEmpty() ? -1 : open.last()});
            open.append(int(elements.size() - 1));
        } else if (reader.isEndElement()) {
            open.removeLast();
        }
    }
    return elements;
}

int find(const QVector<Element> &elements, const QString &id)
{
    for (qsizetype i = 0; i < elements.size(); ++i)
        if (elements[i].value("id") == id) return int(i);
    return -1;
}

QVector<int> childrenOf(const QVector<Element> &elements, int parent)
{
    QVector<int> result;
    for (qsizetype i = 0; i < elements.size(); ++i)
        if (elements[i].parent == parent) result.append(int(i));
    return result;
}

QString objectId(const char *prefix, CanvasObjectId id)
{
    return QStringLiteral("%1-%2").arg(QLatin1String(prefix)).arg(id);
}
} // namespace

class TstCanvasVectorExport : public QObject {
    Q_OBJECT
private slots:
    void appearanceExportsAsSvg();
    void unsupportedEffectsBlockExport();
};

void TstCanvasVectorExport::appearanceExportsAsSvg()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 200, 200));
    const CanvasObjectId framed = doc.addRect(10, 10, 60, 40, QColor("#3366cc"), QColor("#cc3333"), 4);
    const CanvasObjectId shadowed = doc.addEllipse(100, 10, 40, 40, QColor("#22aa66"), {}, 0);
    // addRect keeps the rectangle at the origin and places it with the
    // transform, so these transforms carry the placement too.
    const CanvasObjectId scaled = doc.addRect(0, 0, 20, 20, QColor("#ffcc00"), QColor("#0000ff"), 4);
    const CanvasObjectId stretched = doc.addRect(0, 0, 10, 10, QColor("#aa22aa"), {}, 0);
    QVERIFY(doc.setObjectTransform(scaled, {2, 0, 0, 2, 20, 160})); // document 20,160 40x40
    QVERIFY(doc.setObjectTransform(stretched, {8, 0, 0, 2, 100, 100}));
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    QVERIFY(styles.apply({QString::number(framed)}, {{"alignment", "outside"}, {"pattern", "hatch"}}));
    QVERIFY(styles.apply({QString::number(shadowed)}, {{"opacity", 0.6}, {"blend", "multiply"}}));
    CanvasAppearanceModel effects;
    effects.setDocument(&doc);
    effects.setObjectId(QString::number(shadowed));
    QVERIFY(!effects.addEffect(QStringLiteral("drop-shadow"),
        {{"offsetX", 3}, {"offsetY", 4}, {"blur", 2}, {"color", "#80000000"}}).isEmpty());
    QVERIFY(!effects.addEffect(QStringLiteral("gaussian-blur"), {{"radius", 1}}).isEmpty());

    OperationError error;
    const QByteArray svg = CanvasSvgExport::encode(doc, &error);
    QVERIFY2(!svg.isEmpty(), qPrintable(error.message));
    const QVector<Element> elements = parse(svg);

    // Pattern cell in local units: the fill behind a hatch band inked with the stroke colour.
    const int pattern = find(elements, objectId("canvas-pattern", framed));
    QVERIFY(pattern >= 0);
    QCOMPARE(elements[pattern].name, QStringLiteral("pattern"));
    QCOMPARE(elements[pattern].value("patternUnits"), QStringLiteral("userSpaceOnUse"));
    QCOMPARE(elements[pattern].value("width"), QStringLiteral("8"));
    QCOMPARE(elements[pattern].value("patternTransform"), QStringLiteral("rotate(45)"));
    const QVector<int> cell = childrenOf(elements, pattern);
    QCOMPARE(cell.size(), 2);
    QCOMPARE(elements[cell[0]].value("fill"), QStringLiteral("#3366cc"));
    QCOMPARE(elements[cell[1]].value("fill"), QStringLiteral("#cc3333"));
    QCOMPARE(elements[cell[1]].value("y"), QStringLiteral("3"));
    QCOMPARE(elements[cell[1]].value("height"), QStringLiteral("2"));

    // Outside stroke: the fill, then a doubled stroke under a mask that removes the inside.
    const int mask = find(elements, objectId("canvas-stroke-mask", framed));
    QVERIFY(mask >= 0);
    const QVector<int> maskParts = childrenOf(elements, mask);
    QCOMPARE(maskParts.size(), 2);
    QCOMPARE(elements[maskParts[0]].value("fill"), QStringLiteral("white"));
    QCOMPARE(elements[maskParts[1]].value("fill"), QStringLiteral("black"));
    const int framedGroup = find(elements, objectId("canvas-object", framed));
    QCOMPARE(elements[framedGroup].name, QStringLiteral("g"));
    const QVector<int> framedParts = childrenOf(elements, framedGroup);
    QCOMPARE(framedParts.size(), 2);
    QCOMPARE(elements[framedParts[0]].value("fill"), QStringLiteral("url(#%1)").arg(objectId("canvas-pattern", framed)));
    QCOMPARE(elements[framedParts[0]].value("stroke"), QStringLiteral("none"));
    QCOMPARE(elements[framedParts[1]].value("mask"), QStringLiteral("url(#%1)").arg(objectId("canvas-stroke-mask", framed)));
    const QVector<int> strokePart = childrenOf(elements, framedParts[1]);
    QCOMPARE(strokePart.size(), 1);
    QCOMPARE(elements[strokePart[0]].value("fill"), QStringLiteral("none"));
    QCOMPARE(elements[strokePart[0]].value("stroke-width"), QStringLiteral("8"));

    // Effects chain in stack order inside a filter over the painted bounds.
    const int filter = find(elements, objectId("canvas-effects", shadowed));
    QVERIFY(filter >= 0);
    QCOMPARE(elements[filter].value("x"), QStringLiteral("94"));
    QCOMPARE(elements[filter].value("y"), QStringLiteral("5"));
    QCOMPARE(elements[filter].value("width"), QStringLiteral("58"));
    QCOMPARE(elements[filter].value("color-interpolation-filters"), QStringLiteral("sRGB"));
    QStringList primitives;
    for (qsizetype i = filter + 1; i < elements.size() && elements[i].name.startsWith(QLatin1String("fe")); ++i)
        primitives << elements[i].name;
    QCOMPARE(primitives, (QStringList{"feGaussianBlur", "feOffset", "feFlood", "feComposite", "feMerge",
                                      "feMergeNode", "feMergeNode", "feGaussianBlur"}));
    const Element &shadowBlur = elements[filter + 1];
    QCOMPARE(shadowBlur.value("in"), QStringLiteral("SourceGraphic"));
    QCOMPARE(shadowBlur.value("stdDeviation"), QStringLiteral("2"));
    QCOMPARE(elements[filter + 2].value("dx"), QStringLiteral("3"));
    QCOMPARE(elements[filter + 2].value("dy"), QStringLiteral("4"));
    QCOMPARE(elements[filter + 3].value("flood-color"), QStringLiteral("#000000"));
    QVERIFY(elements[filter + 3].value("flood-opacity").startsWith(QLatin1String("0.50196")));
    QCOMPARE(elements[filter + 4].value("operator"), QStringLiteral("in"));
    QCOMPARE(elements[filter + 5].value("result"), QStringLiteral("fx0"));
    QCOMPARE(elements[filter + 8].value("in"), QStringLiteral("fx0"));
    QCOMPARE(elements[filter + 8].value("stdDeviation"), QStringLiteral("1"));
    const Element &shadowGroup = elements[find(elements, objectId("canvas-object", shadowed))];
    QCOMPARE(shadowGroup.name, QStringLiteral("g"));
    QCOMPARE(shadowGroup.value("opacity").toDouble(), 0.6);
    QCOMPARE(shadowGroup.value("style"), QStringLiteral("mix-blend-mode:multiply"));
    QCOMPARE(shadowGroup.value("filter"), QStringLiteral("url(#%1)").arg(objectId("canvas-effects", shadowed)));

    // Strokes are document units: a uniform scale divides the width, any
    // other transform is written into document-space geometry.
    const Element &scaledRect = elements[find(elements, objectId("canvas-object", scaled))];
    QCOMPARE(scaledRect.value("transform"), QStringLiteral("matrix(2 0 0 2 20 160)"));
    QCOMPARE(scaledRect.value("stroke-width"), QStringLiteral("2"));
    const Element &stretchedPath = elements[find(elements, objectId("canvas-object", stretched))];
    QCOMPARE(stretchedPath.name, QStringLiteral("path"));
    QVERIFY(!stretchedPath.attributes.hasAttribute(QLatin1String("transform")));
    // Closed contours write their closing segment explicitly (it may be a
    // curve), as the path writer always has.
    QCOMPARE(stretchedPath.value("d"), QStringLiteral("M 100 100 L 180 100 L 180 120 L 100 120 L 100 100 Z"));

    // Qt's SVG renderer and the canvas agree on those strokes and geometry.
    const QImage canvas = CanvasPainter::renderTile(doc, QRectF(0, 0, 200, 200), 200, {});
    QSvgRenderer renderer(svg);
    QVERIFY(renderer.isValid());
    QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        renderer.render(&painter);
    }
    QCOMPARE(canvas.pixelColor(140, 110), QColor("#aa22aa"));
    QCOMPARE(canvas.pixelColor(20, 180), QColor("#0000ff")); // stroke band x 18..22
    QCOMPARE(canvas.pixelColor(23, 180), QColor("#ffcc00"));
    for (const QPoint point : {QPoint(140, 110), QPoint(20, 180), QPoint(23, 180)})
        QCOMPARE(image.pixelColor(point), canvas.pixelColor(point));
}

void TstCanvasVectorExport::unsupportedEffectsBlockExport()
{
    CanvasDocument doc;
    const CanvasObjectId id = doc.addRect(10, 10, 40, 40, QColor("#aa22aa"), {}, 0);
    CanvasAppearanceModel model;
    model.setDocument(&doc);
    model.setObjectId(QString::number(id));
    const QString glow = model.addEffect(QStringLiteral("outer-glow"), {{"size", 3}});
    QCOMPARE(glow, QStringLiteral("outer-glow-1"));
    OperationError error;
    QVERIFY(CanvasSvgExport::encode(doc, &error).isEmpty());
    QCOMPARE(error.code, ErrorCode::Unsupported);
    QCOMPARE(error.fieldPath, QStringLiteral("objects.appearance.effects"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!doc.exportSvg(dir.filePath(QStringLiteral("glow.svg"))));
    QVERIFY(!doc.lastError().isEmpty());
    // A bypassed effect is not drawn, so it does not block export.
    QVERIFY(model.setEffectEnabled(glow, false));
    const QByteArray svg = CanvasSvgExport::encode(doc, &error);
    QVERIFY2(!svg.isEmpty(), qPrintable(error.message));
    QVERIFY(!svg.contains("<filter"));
    QVERIFY(doc.exportSvg(dir.filePath(QStringLiteral("bypassed.svg"))));
}

QTEST_MAIN(TstCanvasVectorExport)
#include "tst_canvas_vector_export.moc"
