// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_svg_export.h"
#include <QSaveFile>
#include <QXmlStreamWriter>
#include <algorithm>

namespace QindaTK::CanvasSvgExport {
namespace {
QString num(qreal n) { return QString::number(n, 'g', 17); }
QString point(QPointF p) { return num(p.x()) + QLatin1Char(' ') + num(p.y()); }
QString cssColor(const QColor &color) { return color.name(QColor::HexRgb); }
QString pathData(const CanvasObject &object)
{
    const auto &nodes = object.pathNodes;
    QString d = QStringLiteral("M ") + point(nodes.first().anchor);
    const int count = object.pathClosed ? nodes.size() : nodes.size() - 1;
    for (int i = 0; i < count; ++i) {
        const auto &a = nodes[i];
        const auto &b = nodes[(i + 1) % nodes.size()];
        if (a.out == a.anchor && b.in == b.anchor)
            d += QStringLiteral(" L ") + point(b.anchor);
        else
            d += QStringLiteral(" C ") + point(a.out) + QLatin1Char(' ')
                + point(b.in) + QLatin1Char(' ') + point(b.anchor);
    }
    if (object.pathClosed) d += QStringLiteral(" Z");
    return d;
}
void style(QXmlStreamWriter &xml, const CanvasObject &o)
{
    xml.writeAttribute(QStringLiteral("fill"), o.hasFill() ? cssColor(o.fill) : QStringLiteral("none"));
    if (o.hasFill() && o.fill.alpha() < 255)
        xml.writeAttribute(QStringLiteral("fill-opacity"), num(o.fill.alphaF()));
    xml.writeAttribute(QStringLiteral("fill-rule"), o.fillRule == Qt::WindingFill
        ? QStringLiteral("nonzero") : QStringLiteral("evenodd"));
    xml.writeAttribute(QStringLiteral("stroke"), o.hasStroke() ? cssColor(o.stroke) : QStringLiteral("none"));
    if (o.hasStroke()) {
        const qreal exportedWidth = std::max(o.strokeWidth, 1.0);
        xml.writeAttribute(QStringLiteral("stroke-width"), num(exportedWidth));
        if (o.stroke.alpha() < 255)
            xml.writeAttribute(QStringLiteral("stroke-opacity"), num(o.stroke.alphaF()));
        xml.writeAttribute(QStringLiteral("stroke-linecap"), o.strokeCap == Qt::RoundCap
            ? QStringLiteral("round") : o.strokeCap == Qt::SquareCap
            ? QStringLiteral("square") : QStringLiteral("butt"));
        xml.writeAttribute(QStringLiteral("stroke-linejoin"), o.strokeJoin == Qt::RoundJoin
            ? QStringLiteral("round") : o.strokeJoin == Qt::BevelJoin
            ? QStringLiteral("bevel") : QStringLiteral("miter"));
        if (!o.dashPattern.isEmpty()) {
            QStringList values;
            for (qreal dash : o.dashPattern) values << num(dash * exportedWidth);
            xml.writeAttribute(QStringLiteral("stroke-dasharray"), values.join(QLatin1Char(' ')));
        }
    }
    const QTransform &t = o.transform;
    xml.writeAttribute(QStringLiteral("transform"), QStringLiteral("matrix(%1 %2 %3 %4 %5 %6)")
        .arg(num(t.m11()), num(t.m12()), num(t.m21()), num(t.m22()), num(t.dx()), num(t.dy())));
}
}

QByteArray encode(const CanvasDocument &document, OperationError *error)
{
    if (error) *error = {};
    if (!document.outputProfileIcc().isEmpty()) {
        if (error) *error = {ErrorCode::Unsupported,
            QStringLiteral("SVG export does not carry the document output ICC profile"),
            QStringLiteral("outputProfileIcc"), QStringLiteral("canvas.svg")};
        return {};
    }
    QVector<const CanvasObject *> objects;
    for (CanvasObjectId id : document.objectIds()) {
        const CanvasObject *o = document.object(id);
        if (!o) continue;
        if (o->shape == CanvasShape::Image || !o->managedFill.isEmpty()) {
            if (error) *error = {ErrorCode::Unsupported,
                QStringLiteral("SVG export supports flat-color vectors only; image and managed fills require another format"),
                QStringLiteral("objects"), QStringLiteral("canvas.svg")};
            return {};
        }
        if (!CanvasGeometry::isRenderable(*o)) {
            if (error) *error = {ErrorCode::Unsupported,
                QStringLiteral("SVG export cannot represent a degenerate canvas object"),
                QStringLiteral("objects.geometry"), QStringLiteral("canvas.svg")};
            return {};
        }
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
    const QRectF page = document.pageRect();
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    xml.writeAttribute(QStringLiteral("viewBox"), num(page.x()) + QLatin1Char(' ') + num(page.y())
        + QLatin1Char(' ') + num(page.width()) + QLatin1Char(' ') + num(page.height()));
    xml.writeStartElement(QStringLiteral("rect"));
    xml.writeAttribute(QStringLiteral("x"), num(page.x()));
    xml.writeAttribute(QStringLiteral("y"), num(page.y()));
    xml.writeAttribute(QStringLiteral("width"), num(page.width()));
    xml.writeAttribute(QStringLiteral("height"), num(page.height()));
    xml.writeAttribute(QStringLiteral("fill"), cssColor(document.pageColor()));
    if (document.pageColor().alpha() < 255)
        xml.writeAttribute(QStringLiteral("fill-opacity"), num(document.pageColor().alphaF()));
    xml.writeEndElement();
    for (const CanvasObject *o : objects) {
        const QString tag = o->shape == CanvasShape::Rect ? QStringLiteral("rect")
            : o->shape == CanvasShape::Ellipse ? QStringLiteral("ellipse")
            : o->shape == CanvasShape::Polyline ? QStringLiteral("polyline") : QStringLiteral("path");
        xml.writeStartElement(tag);
        xml.writeAttribute(QStringLiteral("id"), QStringLiteral("canvas-object-%1").arg(o->id));
        if (!o->name.isEmpty()) xml.writeAttribute(QStringLiteral("aria-label"), o->name);
        if (!document.effectiveVisible(*o)) xml.writeAttribute(QStringLiteral("display"), QStringLiteral("none"));
        if (o->shape == CanvasShape::Rect) {
            xml.writeAttribute(QStringLiteral("x"), num(o->rect.x()));
            xml.writeAttribute(QStringLiteral("y"), num(o->rect.y()));
            xml.writeAttribute(QStringLiteral("width"), num(o->rect.width()));
            xml.writeAttribute(QStringLiteral("height"), num(o->rect.height()));
        } else if (o->shape == CanvasShape::Ellipse) {
            xml.writeAttribute(QStringLiteral("cx"), num(o->rect.center().x()));
            xml.writeAttribute(QStringLiteral("cy"), num(o->rect.center().y()));
            xml.writeAttribute(QStringLiteral("rx"), num(o->rect.width() / 2));
            xml.writeAttribute(QStringLiteral("ry"), num(o->rect.height() / 2));
        } else if (o->shape == CanvasShape::Polyline) {
            QStringList vertices;
            for (QPointF p : o->points) vertices << point(p);
            xml.writeAttribute(QStringLiteral("points"), vertices.join(QLatin1Char(' ')));
        } else xml.writeAttribute(QStringLiteral("d"), pathData(*o));
        style(xml, *o);
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    return output;
}

bool write(const CanvasDocument &document, const QString &filePath, OperationError *error)
{
    const QByteArray data = encode(document, error);
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
