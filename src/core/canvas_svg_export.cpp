// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_svg_export.h"
#include "canvas_appearance.h"
#include "canvas_path.h"
#include <QSaveFile>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>

namespace QindaTK::CanvasSvgExport {
namespace {
QString num(qreal n) { return QString::number(n, 'g', 17); }
QString point(QPointF p) { return num(p.x()) + QLatin1Char(' ') + num(p.y()); }
QString cssColor(const QColor &color) { return color.name(QColor::HexRgb); }
QString matrix(const QTransform &t)
{
    return QStringLiteral("matrix(%1 %2 %3 %4 %5 %6)")
        .arg(num(t.m11()), num(t.m12()), num(t.m21()), num(t.m22()), num(t.dx()), num(t.dy()));
}
QString reference(const QString &prefix, const CanvasObject &o)
{
    return QStringLiteral("%1-%2").arg(prefix).arg(o.id);
}

// AGENT-NOTE (D-109): stroke widths and dashes are document units while SVG
// strokes scale with the element transform. A similarity transform stays an
// attribute with the stroke divided by its scale; any other transform is
// applied to the geometry, which is then written as a document-space path.
struct Placement {
    bool documentSpace = false;
    qreal scale = 1;
};
Placement placementOf(const CanvasObject &o)
{
    const QTransform &t = o.transform;
    const qreal a = std::hypot(t.m11(), t.m12());
    const qreal b = std::hypot(t.m21(), t.m22());
    const qreal dot = t.m11() * t.m21() + t.m12() * t.m22();
    const bool similar = t.type() != QTransform::TxProject && a > 0
        && std::abs(a - b) <= 1e-9 * std::max(a, b) && std::abs(dot) <= 1e-9 * a * b;
    return similar ? Placement{false, a} : Placement{true, 1.0};
}

QString pathData(const QVector<CanvasPathContour> &contours, const QTransform &map)
{
    QString d;
    for (const CanvasPathContour &contour : contours) {
        const auto &nodes = contour.nodes;
        if (nodes.isEmpty()) continue;
        if (!d.isEmpty()) d += QLatin1Char(' ');
        d += QStringLiteral("M ") + point(map.map(nodes.first().anchor));
        const qsizetype count = contour.closed ? nodes.size() : nodes.size() - 1;
        for (qsizetype i = 0; i < count; ++i) {
            const auto &a = nodes[i];
            const auto &b = nodes[(i + 1) % nodes.size()];
            if (a.out == a.anchor && b.in == b.anchor)
                d += QStringLiteral(" L ") + point(map.map(b.anchor));
            else
                d += QStringLiteral(" C ") + point(map.map(a.out)) + QLatin1Char(' ')
                    + point(map.map(b.in)) + QLatin1Char(' ') + point(map.map(b.anchor));
        }
        if (contour.closed) d += QStringLiteral(" Z");
    }
    return d;
}

// Which paint an element carries: an object may be split into its fill and a
// masked, doubled stroke, and a mask repeats its geometry in white or black.
enum class Paint { FillAndStroke, FillOnly, AlignedStroke, MaskWhite, MaskBlack };

void writePaint(QXmlStreamWriter &xml, const CanvasObject &o, const Placement &placement, Paint paint)
{
    const bool fill = paint == Paint::FillAndStroke || paint == Paint::FillOnly;
    if (paint == Paint::MaskWhite || paint == Paint::MaskBlack) {
        xml.writeAttribute(QStringLiteral("fill"), paint == Paint::MaskWhite ? QStringLiteral("white") : QStringLiteral("black"));
    } else if (fill && o.hasPattern()) {
        xml.writeAttribute(QStringLiteral("fill"), QStringLiteral("url(#%1)").arg(reference(QStringLiteral("canvas-pattern"), o)));
    } else {
        xml.writeAttribute(QStringLiteral("fill"), fill && o.hasFill() ? cssColor(o.fill) : QStringLiteral("none"));
        if (fill && o.hasFill() && o.fill.alpha() < 255)
            xml.writeAttribute(QStringLiteral("fill-opacity"), num(o.fill.alphaF()));
    }
    xml.writeAttribute(QStringLiteral("fill-rule"), o.fillRule == Qt::WindingFill
        ? QStringLiteral("nonzero") : QStringLiteral("evenodd"));
    const bool stroke = (paint == Paint::FillAndStroke || paint == Paint::AlignedStroke) && o.hasStroke();
    xml.writeAttribute(QStringLiteral("stroke"), stroke ? cssColor(o.stroke) : QStringLiteral("none"));
    if (stroke) {
        const qreal visible = std::max(o.strokeWidth, 1.0);
        const qreal width = paint == Paint::AlignedStroke ? 2 * visible : visible;
        xml.writeAttribute(QStringLiteral("stroke-width"), num(width / placement.scale));
        if (o.stroke.alpha() < 255)
            xml.writeAttribute(QStringLiteral("stroke-opacity"), num(o.stroke.alphaF()));
        xml.writeAttribute(QStringLiteral("stroke-linecap"), o.strokeCap == Qt::RoundCap
            ? QStringLiteral("round") : o.strokeCap == Qt::SquareCap
            ? QStringLiteral("square") : QStringLiteral("butt"));
        xml.writeAttribute(QStringLiteral("stroke-linejoin"), o.strokeJoin == Qt::RoundJoin
            ? QStringLiteral("round") : o.strokeJoin == Qt::BevelJoin
            ? QStringLiteral("bevel") : QStringLiteral("miter"));
        if (o.appearance.miterLimit != 4)
            xml.writeAttribute(QStringLiteral("stroke-miterlimit"), num(o.appearance.miterLimit));
        if (!o.dashPattern.isEmpty()) {
            QStringList values;
            for (qreal dash : o.dashPattern) values << num(dash * visible / placement.scale);
            xml.writeAttribute(QStringLiteral("stroke-dasharray"), values.join(QLatin1Char(' ')));
        }
    }
    if (!placement.documentSpace)
        xml.writeAttribute(QStringLiteral("transform"), matrix(o.transform));
}

void writeIdentity(QXmlStreamWriter &xml, const CanvasObject &o, const CanvasDocument &document)
{
    xml.writeAttribute(QStringLiteral("id"), reference(QStringLiteral("canvas-object"), o));
    if (!o.name.isEmpty()) xml.writeAttribute(QStringLiteral("aria-label"), o.name);
    if (!document.effectiveVisible(o)) xml.writeAttribute(QStringLiteral("display"), QStringLiteral("none"));
}

void writeShape(QXmlStreamWriter &xml, const CanvasObject &o, const Placement &placement,
                const CanvasDocument *identity, Paint paint)
{
    const bool local = !placement.documentSpace;
    const QString tag = !local || o.shape == CanvasShape::Path ? QStringLiteral("path")
        : o.shape == CanvasShape::Rect ? QStringLiteral("rect")
        : o.shape == CanvasShape::Ellipse ? QStringLiteral("ellipse") : QStringLiteral("polyline");
    xml.writeStartElement(tag);
    if (identity) writeIdentity(xml, o, *identity);
    if (!local) {
        xml.writeAttribute(QStringLiteral("d"), pathData(CanvasPath::contours(o), o.transform));
    } else if (o.shape == CanvasShape::Rect) {
        xml.writeAttribute(QStringLiteral("x"), num(o.rect.x()));
        xml.writeAttribute(QStringLiteral("y"), num(o.rect.y()));
        xml.writeAttribute(QStringLiteral("width"), num(o.rect.width()));
        xml.writeAttribute(QStringLiteral("height"), num(o.rect.height()));
    } else if (o.shape == CanvasShape::Ellipse) {
        xml.writeAttribute(QStringLiteral("cx"), num(o.rect.center().x()));
        xml.writeAttribute(QStringLiteral("cy"), num(o.rect.center().y()));
        xml.writeAttribute(QStringLiteral("rx"), num(o.rect.width() / 2));
        xml.writeAttribute(QStringLiteral("ry"), num(o.rect.height() / 2));
    } else if (o.shape == CanvasShape::Polyline) {
        QStringList vertices;
        for (QPointF p : o.points) vertices << point(p);
        xml.writeAttribute(QStringLiteral("points"), vertices.join(QLatin1Char(' ')));
    } else {
        xml.writeAttribute(QStringLiteral("d"), pathData(CanvasPath::contours(o), QTransform()));
    }
    writePaint(xml, o, placement, paint);
    xml.writeEndElement();
}

void writeRect(QXmlStreamWriter &xml, const QRectF &r, const QString &fill, const QColor &color = {})
{
    xml.writeStartElement(QStringLiteral("rect"));
    xml.writeAttribute(QStringLiteral("x"), num(r.x()));
    xml.writeAttribute(QStringLiteral("y"), num(r.y()));
    xml.writeAttribute(QStringLiteral("width"), num(r.width()));
    xml.writeAttribute(QStringLiteral("height"), num(r.height()));
    xml.writeAttribute(QStringLiteral("fill"), color.isValid() ? cssColor(color) : fill);
    if (color.isValid() && color.alpha() < 255)
        xml.writeAttribute(QStringLiteral("fill-opacity"), num(color.alphaF()));
    xml.writeEndElement();
}

// One pattern cell in local units: the fill colour behind the ink, the ink
// shapes the painter draws, rotated about the local origin (D-109).
void writePattern(QXmlStreamWriter &xml, const CanvasObject &o, const Placement &placement)
{
    const CanvasPattern &p = o.appearance.pattern;
    const qreal s = p.spacing, w = p.weight;
    xml.writeStartElement(QStringLiteral("pattern"));
    xml.writeAttribute(QStringLiteral("id"), reference(QStringLiteral("canvas-pattern"), o));
    xml.writeAttribute(QStringLiteral("patternUnits"), QStringLiteral("userSpaceOnUse"));
    xml.writeAttribute(QStringLiteral("x"), QStringLiteral("0"));
    xml.writeAttribute(QStringLiteral("y"), QStringLiteral("0"));
    xml.writeAttribute(QStringLiteral("width"), num(s));
    xml.writeAttribute(QStringLiteral("height"), num(s));
    const QString rotate = QStringLiteral("rotate(%1)").arg(num(p.angle));
    if (placement.documentSpace)
        xml.writeAttribute(QStringLiteral("patternTransform"), matrix(o.transform) + QLatin1Char(' ') + rotate);
    else if (p.angle != 0)
        xml.writeAttribute(QStringLiteral("patternTransform"), rotate);
    if (o.hasFill()) writeRect(xml, QRectF(0, 0, s, s), {}, o.fill);
    const QString ink = cssColor(p.ink);
    const auto inkOpacity = [&xml, &p] {
        if (p.ink.alpha() < 255) xml.writeAttribute(QStringLiteral("fill-opacity"), num(p.ink.alphaF()));
    };
    const auto square = [&](qreal x, qreal y, qreal width, qreal height) {
        xml.writeStartElement(QStringLiteral("rect"));
        xml.writeAttribute(QStringLiteral("x"), num(x));
        xml.writeAttribute(QStringLiteral("y"), num(y));
        xml.writeAttribute(QStringLiteral("width"), num(width));
        xml.writeAttribute(QStringLiteral("height"), num(height));
        xml.writeAttribute(QStringLiteral("fill"), ink);
        inkOpacity();
        xml.writeEndElement();
    };
    if (p.kind == CanvasPatternKind::Lines) {
        square(0, (s - w) / 2, s, w);
    } else if (p.kind == CanvasPatternKind::Grid) {
        // One nonzero path, so the crossing is inked once like the canvas.
        const qreal a = (s - w) / 2, b = (s + w) / 2;
        xml.writeStartElement(QStringLiteral("path"));
        xml.writeAttribute(QStringLiteral("d"), QStringLiteral("M 0 %1 H %2 V %3 H 0 Z M %1 0 H %3 V %2 H %1 Z")
                                                    .arg(num(a), num(s), num(b)));
        xml.writeAttribute(QStringLiteral("fill"), ink);
        inkOpacity();
        xml.writeAttribute(QStringLiteral("fill-rule"), QStringLiteral("nonzero"));
        xml.writeEndElement();
    } else if (p.kind == CanvasPatternKind::Dots) {
        xml.writeStartElement(QStringLiteral("circle"));
        xml.writeAttribute(QStringLiteral("cx"), num(s / 2));
        xml.writeAttribute(QStringLiteral("cy"), num(s / 2));
        xml.writeAttribute(QStringLiteral("r"), num(w / 2));
        xml.writeAttribute(QStringLiteral("fill"), ink);
        inkOpacity();
        xml.writeEndElement();
    } else if (p.kind == CanvasPatternKind::Checker) {
        square(0, 0, s / 2, s / 2);
        square(s / 2, s / 2, s / 2, s / 2);
    }
    xml.writeEndElement();
}

// The doubled stroke is masked in document space: inside keeps the fill
// area (white), outside keeps everything but it.
void writeStrokeMask(QXmlStreamWriter &xml, const CanvasObject &o, const Placement &placement)
{
    const qreal reach = std::max(o.strokeWidth, 1.0) * std::max(o.appearance.miterLimit, 1.0) + 1;
    const QRectF region = CanvasGeometry::documentBounds(o).adjusted(-reach, -reach, reach, reach);
    const bool outside = o.appearance.strokeAlignment == CanvasStrokeAlignment::Outside;
    xml.writeStartElement(QStringLiteral("mask"));
    xml.writeAttribute(QStringLiteral("id"), reference(QStringLiteral("canvas-stroke-mask"), o));
    xml.writeAttribute(QStringLiteral("maskUnits"), QStringLiteral("userSpaceOnUse"));
    xml.writeAttribute(QStringLiteral("x"), num(region.x()));
    xml.writeAttribute(QStringLiteral("y"), num(region.y()));
    xml.writeAttribute(QStringLiteral("width"), num(region.width()));
    xml.writeAttribute(QStringLiteral("height"), num(region.height()));
    if (outside) writeRect(xml, region, QStringLiteral("white"));
    writeShape(xml, o, placement, nullptr, outside ? Paint::MaskBlack : Paint::MaskWhite);
    xml.writeEndElement();
}

// SVG 1.1 filter primitives chained in stack order: each effect reads the
// previous result, like the canvas layer (D-109).
void writeFilter(QXmlStreamWriter &xml, const CanvasObject &o)
{
    const QRectF region = CanvasGeometry::paintBounds(o);
    xml.writeStartElement(QStringLiteral("filter"));
    xml.writeAttribute(QStringLiteral("id"), reference(QStringLiteral("canvas-effects"), o));
    xml.writeAttribute(QStringLiteral("filterUnits"), QStringLiteral("userSpaceOnUse"));
    xml.writeAttribute(QStringLiteral("x"), num(region.x()));
    xml.writeAttribute(QStringLiteral("y"), num(region.y()));
    xml.writeAttribute(QStringLiteral("width"), num(region.width()));
    xml.writeAttribute(QStringLiteral("height"), num(region.height()));
    xml.writeAttribute(QStringLiteral("color-interpolation-filters"), QStringLiteral("sRGB"));
    const auto primitive = [&xml](const char *name, std::initializer_list<std::pair<const char *, QString>> attributes) {
        xml.writeStartElement(QLatin1String(name));
        for (const auto &[key, value] : attributes) xml.writeAttribute(QLatin1String(key), value);
        xml.writeEndElement();
    };
    QString source = QStringLiteral("SourceGraphic");
    int step = 0;
    for (const CanvasEffect &effect : o.appearance.effects) {
        if (!effect.enabled || !CanvasAppearanceRules::supportedEffect(effect.kind)) continue;
        const QString result = QStringLiteral("fx%1").arg(step++);
        if (effect.kind == QLatin1String("drop-shadow")) {
            const auto s = CanvasAppearanceRules::shadow(effect);
            QString alpha = source;
            if (s.blur > 0) {
                alpha = result + QStringLiteral("-blur");
                primitive("feGaussianBlur", {{"in", source}, {"stdDeviation", num(s.blur)}, {"result", alpha}});
            }
            primitive("feOffset", {{"in", alpha}, {"dx", num(s.offset.x())}, {"dy", num(s.offset.y())},
                                   {"result", result + QStringLiteral("-offset")}});
            primitive("feFlood", {{"flood-color", cssColor(s.color)}, {"flood-opacity", num(s.color.alphaF())},
                                  {"result", result + QStringLiteral("-color")}});
            primitive("feComposite", {{"in", result + QStringLiteral("-color")},
                                      {"in2", result + QStringLiteral("-offset")}, {"operator", QStringLiteral("in")},
                                      {"result", result + QStringLiteral("-shadow")}});
            xml.writeStartElement(QStringLiteral("feMerge"));
            xml.writeAttribute(QStringLiteral("result"), result);
            primitive("feMergeNode", {{"in", result + QStringLiteral("-shadow")}});
            primitive("feMergeNode", {{"in", source}});
            xml.writeEndElement();
        } else {
            primitive("feGaussianBlur", {{"in", source},
                                         {"stdDeviation", num(CanvasAppearanceRules::blurRadius(effect))},
                                         {"result", result}});
        }
        source = result;
    }
    xml.writeEndElement();
}

void writeObject(QXmlStreamWriter &xml, const CanvasObject &o, const CanvasDocument &document)
{
    const Placement placement = placementOf(o);
    const CanvasAppearance &a = o.appearance;
    const bool aligned = o.hasStroke() && o.closesArea() && a.strokeAlignment != CanvasStrokeAlignment::Center;
    const bool effects = CanvasAppearanceRules::hasRenderedEffects(o);
    if (o.hasPattern() || aligned || effects) {
        xml.writeStartElement(QStringLiteral("defs"));
        if (o.hasPattern()) writePattern(xml, o, placement);
        if (aligned) writeStrokeMask(xml, o, placement);
        if (effects) writeFilter(xml, o);
        xml.writeEndElement();
    }
    if (!aligned && !effects && a.opacity >= 1 && a.blend == CanvasBlendMode::Normal) {
        writeShape(xml, o, placement, &document, Paint::FillAndStroke);
        return;
    }
    // Opacity, blend and filters apply to the object as a whole, in document space.
    xml.writeStartElement(QStringLiteral("g"));
    writeIdentity(xml, o, document);
    if (a.opacity < 1) xml.writeAttribute(QStringLiteral("opacity"), num(a.opacity));
    if (a.blend != CanvasBlendMode::Normal)
        xml.writeAttribute(QStringLiteral("style"), QStringLiteral("mix-blend-mode:")
                                                     + CanvasAppearanceRules::blendName(a.blend));
    if (effects)
        xml.writeAttribute(QStringLiteral("filter"), QStringLiteral("url(#%1)").arg(reference(QStringLiteral("canvas-effects"), o)));
    if (aligned) {
        if (o.fillsArea()) writeShape(xml, o, placement, nullptr, Paint::FillOnly);
        xml.writeStartElement(QStringLiteral("g"));
        xml.writeAttribute(QStringLiteral("mask"), QStringLiteral("url(#%1)").arg(reference(QStringLiteral("canvas-stroke-mask"), o)));
        writeShape(xml, o, placement, nullptr, Paint::AlignedStroke);
        xml.writeEndElement();
    } else {
        writeShape(xml, o, placement, nullptr, Paint::FillAndStroke);
    }
    xml.writeEndElement();
}

OperationError unsupported(const QString &message, const QString &field)
{
    return {ErrorCode::Unsupported, message, field, QStringLiteral("canvas.svg")};
}
} // namespace

QByteArray encode(const CanvasDocument &document, OperationError *error)
{
    return encodeRegion(document, document.pageRect(), document.pageColor(), error);
}

QByteArray encodeRegion(const CanvasDocument &document, const QRectF &region,
                        const QColor &background, OperationError *error)
{
    if (error) *error = {};
    const auto fail = [error](const OperationError &e) {
        if (error) *error = e;
        return QByteArray();
    };
    if (!document.outputProfileIcc().isEmpty())
        return fail(unsupported(QStringLiteral("SVG export does not carry the document output ICC profile"),
                                QStringLiteral("outputProfileIcc")));
    QVector<const CanvasObject *> objects;
    for (CanvasObjectId id : document.objectIds()) {
        const CanvasObject *o = document.object(id);
        if (!o) continue;
        if (o->shape == CanvasShape::Image || !o->managedFill.isEmpty())
            return fail(unsupported(QStringLiteral("SVG export supports flat-color vectors only; image and managed fills require another format"),
                                    QStringLiteral("objects")));
        if (!CanvasGeometry::isRenderable(*o))
            return fail(unsupported(QStringLiteral("SVG export cannot represent a degenerate canvas object"),
                                    QStringLiteral("objects.geometry")));
        for (const CanvasEffect &effect : o->appearance.effects)
            if (effect.enabled && !CanvasAppearanceRules::supportedEffect(effect.kind))
                return fail(unsupported(QStringLiteral("SVG export cannot represent the \"%1\" appearance effect; bypass it to export")
                                            .arg(effect.kind),
                                        QStringLiteral("objects.appearance.effects")));
        objects.append(o);
    }
    std::sort(objects.begin(), objects.end(), [&document](auto *a, auto *b) {
        const qint64 ka = document.paintKey(*a), kb = document.paintKey(*b);
        return ka == kb ? a->id < b->id : ka < kb;
    });
    QByteArray output;
    QXmlStreamWriter xml(&output);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("svg"));
    xml.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
    const QRectF page = region;
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    xml.writeAttribute(QStringLiteral("viewBox"), num(page.x()) + QLatin1Char(' ') + num(page.y())
        + QLatin1Char(' ') + num(page.width()) + QLatin1Char(' ') + num(page.height()));
    writeRect(xml, page, cssColor(background), background);
    // Artboard backgrounds are document data below every object (D-093),
    // as the renderers paint them.
    for (const CanvasBackdrop &backdrop : document.backdrops())
        writeRect(xml, backdrop.rect, cssColor(backdrop.color), backdrop.color);
    for (const CanvasObject *o : objects) writeObject(xml, *o, document);
    xml.writeEndElement();
    xml.writeEndDocument();
    return output;
}

bool write(const CanvasDocument &document, const QString &filePath, OperationError *error)
{
    return writeRegion(document, document.pageRect(), document.pageColor(), filePath, error);
}

bool writeRegion(const CanvasDocument &document, const QRectF &region, const QColor &background,
                 const QString &filePath, OperationError *error)
{
    const QByteArray data = encodeRegion(document, region, background, error);
    if (data.isEmpty()) return false;
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = {ErrorCode::BackendFailure, QStringLiteral("Cannot write SVG file"),
                             QStringLiteral("filePath"), QStringLiteral("canvas.svg")};
        return false;
    }
    return true;
}
} // namespace QindaTK::CanvasSvgExport
