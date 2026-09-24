// SPDX-License-Identifier: LGPL-3.0-or-later
// Appearance rendering for the software provider (D-107): pattern ink over
// the fill, inside/outside strokes and the offscreen layer that effects,
// opacity and blend modes need. The RHI provider rasterizes tiles holding
// such objects through this painter, so both backends agree.
#include "canvas_appearance_paint.h"
#include "canvas_appearance.h"
#include "canvas_effects.h"
#include "canvas_geometry.h"
#include "canvas_path.h"
#include "canvas_render.h"
#include "canvas_transform.h"

#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace QindaTK::CanvasAppearancePaint {
namespace {
// AGENT-GUARD: layers are bounded. A layer that would exceed this side (very
// large blurs at high zoom) is rendered at a lower resolution and scaled up.
constexpr int MaximumLayerSide = 4096;
// Pattern cells smaller than this on screen draw as their average coverage,
// which also bounds the primitives per tile.
constexpr qreal MinimumCellPixels = 2.0;
constexpr qreal MaximumCells = 250000;

qreal inkCoverage(const CanvasPattern &p)
{
    const qreal w = std::clamp(p.weight / p.spacing, 0.0, 1.0);
    switch (p.kind) {
    case CanvasPatternKind::Lines: return w;
    case CanvasPatternKind::Grid: return 1 - (1 - w) * (1 - w);
    case CanvasPatternKind::Dots: return std::min(1.0, M_PI * w * w / 4);
    case CanvasPatternKind::Checker: return 0.5;
    case CanvasPatternKind::None: break;
    }
    return 0;
}

// Ink of cells i0..i1 x j0..j1 in pattern space; cell (i, j) spans
// [i*s, (i+1)*s) x [j*s, (j+1)*s). A line row is one band, so rows have no
// seams, and the winding rule fills grid crossings once.
QPainterPath inkPath(const CanvasPattern &p, int i0, int i1, int j0, int j1)
{
    const qreal s = p.spacing;
    const qreal w = p.weight;
    QPainterPath ink;
    ink.setFillRule(Qt::WindingFill);
    const qreal left = i0 * s, width = (i1 - i0 + 1) * s;
    const qreal top = j0 * s, height = (j1 - j0 + 1) * s;
    switch (p.kind) {
    case CanvasPatternKind::Lines:
    case CanvasPatternKind::Grid:
        for (int j = j0; j <= j1; ++j) ink.addRect(QRectF(left, j * s + (s - w) / 2, width, w));
        if (p.kind == CanvasPatternKind::Grid)
            for (int i = i0; i <= i1; ++i) ink.addRect(QRectF(i * s + (s - w) / 2, top, w, height));
        break;
    case CanvasPatternKind::Dots:
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
                ink.addEllipse(QPointF(i * s + s / 2, j * s + s / 2), w / 2, w / 2);
        break;
    case CanvasPatternKind::Checker:
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                ink.addRect(QRectF(i * s, j * s, s / 2, s / 2));
                ink.addRect(QRectF(i * s + s / 2, j * s + s / 2, s / 2, s / 2));
            }
        break;
    case CanvasPatternKind::None:
        break;
    }
    return ink;
}

// Pattern ink in local space, rotated about the local origin, clipped to the
// document-space fill area. Only cells meeting the visible area are built.
void paintPattern(QPainter *painter, const CanvasObject &o, const QPainterPath &area, qreal scale)
{
    const CanvasPattern &p = o.appearance.pattern;
    QTransform rotation;
    rotation.rotate(p.angle);
    const QTransform toDocument = rotation * o.transform;
    const auto toPattern = CanvasTransform::invert(toDocument);
    QRectF visible = area.boundingRect();
    if (painter->hasClipping()) visible &= painter->clipBoundingRect();
    if (!toPattern || visible.isEmpty()) return;
    const qreal s = p.spacing;
    const QRectF cells = toPattern->mapRect(visible);
    const qreal i0 = std::floor(cells.left() / s), i1 = std::floor(cells.right() / s);
    const qreal j0 = std::floor(cells.top() / s), j1 = std::floor(cells.bottom() / s);
    const qreal count = (i1 - i0 + 1) * (j1 - j0 + 1);
    const qreal cellPixels = s * std::sqrt(std::abs(toDocument.determinant())) * scale;
    constexpr qreal indexLimit = 1e9;
    const bool indexable = std::abs(i0) < indexLimit && std::abs(i1) < indexLimit
        && std::abs(j0) < indexLimit && std::abs(j1) < indexLimit;
    painter->save();
    painter->setClipPath(area, Qt::IntersectClip);
    painter->setPen(Qt::NoPen);
    if (cellPixels < MinimumCellPixels || !(count <= MaximumCells) || !indexable) {
        QColor average = p.ink;
        average.setAlphaF(float(p.ink.alphaF() * inkCoverage(p)));
        painter->fillPath(area, average);
    } else {
        painter->setTransform(toDocument * painter->transform());
        painter->setBrush(p.ink);
        painter->drawPath(inkPath(p, int(i0), int(i1), int(j0), int(j1)));
    }
    painter->restore();
}

// A stroke twice as wide, masked by the fill area: inside keeps the inner
// half, outside removes it. Dashes stay multiples of the visible width.
void alignedStroke(QPainter *painter, const CanvasObject &o, const QPainterPath &area, qreal pixel)
{
    const QPaintDevice *device = painter->device();
    if (!device || device->width() <= 0 || device->height() <= 0) return;
    QPen pen = CanvasPath::pen(o, pixel);
    pen.setWidthF(pen.widthF() * 2);
    if (!o.dashPattern.isEmpty()) {
        QVector<qreal> dashes = o.dashPattern;
        for (qreal &dash : dashes) dash /= 2;
        pen.setDashPattern(dashes);
    }
    QImage stroke(device->width(), device->height(), QImage::Format_ARGB32_Premultiplied);
    stroke.fill(Qt::transparent);
    {
        QPainter band(&stroke);
        band.setRenderHint(QPainter::Antialiasing, true);
        band.setTransform(painter->transform());
        band.setPen(pen);
        band.setBrush(Qt::NoBrush);
        band.drawPath(area);
        if (o.appearance.strokeAlignment == CanvasStrokeAlignment::Outside) {
            band.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            band.fillPath(area, Qt::black);
        } else {
            // DestinationIn must cover the whole image to clear the outer half.
            QImage inside(stroke.size(), QImage::Format_ARGB32_Premultiplied);
            inside.fill(Qt::transparent);
            {
                QPainter mask(&inside);
                mask.setRenderHint(QPainter::Antialiasing, true);
                mask.setTransform(painter->transform());
                mask.fillPath(area, Qt::black);
            }
            band.resetTransform();
            band.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            band.drawImage(QPoint(0, 0), inside);
        }
    }
    painter->save();
    painter->resetTransform();
    painter->drawImage(QPoint(0, 0), stroke);
    painter->restore();
}
} // namespace

void paintVector(QPainter *painter, const CanvasObject &o, const QBrush &fillBrush, qreal scale)
{
    const qreal pixel = 1.0 / std::max(scale, 1e-9);
    const bool path = o.shape == CanvasShape::Path;
    QPolygonF outline;
    QPainterPath area;
    if (path) {
        area = CanvasPath::document(o);
    } else {
        const QRectF bounds = CanvasGeometry::documentBounds(o);
        const qreal radius = std::max(bounds.width(), bounds.height()) * scale / 2.0;
        outline = CanvasGeometry::documentOutline(o, CanvasPainter::ellipseSegments(radius));
    }
    const bool aligned = o.hasStroke() && o.closesArea()
        && o.appearance.strokeAlignment != CanvasStrokeAlignment::Center;
    if (!path && (o.hasPattern() || aligned)) {
        area.setFillRule(o.fillRule);
        area.addPolygon(outline);
        area.closeSubpath();
    }
    if (o.hasFill()) {
        if (path) {
            painter->fillPath(area, fillBrush);
        } else {
            painter->setPen(Qt::NoPen);
            painter->setBrush(fillBrush);
            painter->drawPolygon(outline, o.fillRule);
        }
    }
    if (o.hasPattern()) paintPattern(painter, o, area, scale);
    if (!o.hasStroke()) return;
    if (aligned) {
        alignedStroke(painter, o, area, pixel);
        return;
    }
    painter->setPen(CanvasPath::pen(o, pixel));
    painter->setBrush(Qt::NoBrush);
    if (path) painter->drawPath(area);
    else if (CanvasGeometry::isClosed(o)) painter->drawPolygon(outline);
    else painter->drawPolyline(outline);
}

void paintLayered(QPainter *painter, const CanvasObject &o, const QRectF &documentRect,
                  qreal scale, const LayerContent &content)
{
    if (o.appearance.opacity <= 0) return;
    const QTransform base = painter->transform();
    const auto toDocument = CanvasTransform::invert(base);
    if (!toDocument) return;
    // Effects read up to `margin` pixels away, so the layer covers the tile
    // plus that margin (limited to where the object paints): no tile seams.
    const qreal margin = CanvasAppearanceRules::effectInputReach(o) * scale + 2;
    const QRectF tile = base.mapRect(documentRect).adjusted(-margin, -margin, margin, margin);
    const QRectF painted = base.mapRect(CanvasGeometry::paintBounds(o))
                               .adjusted(-margin, -margin, margin, margin);
    const QRect area = tile.intersected(painted).toAlignedRect();
    if (area.isEmpty()) return;
    const qreal factor = std::min(1.0, qreal(MaximumLayerSide) / std::max(area.width(), area.height()));
    const QSize size(std::max(1, int(std::ceil(area.width() * factor))),
                     std::max(1, int(std::ceil(area.height() * factor))));
    QImage layer(size, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    const qreal pixels = scale * factor;
    {
        QPainter layerPainter(&layer);
        layerPainter.setRenderHint(QPainter::Antialiasing, true);
        layerPainter.setTransform(base * QTransform::fromTranslate(-area.left(), -area.top())
                                  * QTransform::fromScale(factor, factor));
        content(&layerPainter, toDocument->mapRect(QRectF(area)), pixels);
    }
    for (const CanvasEffect &effect : o.appearance.effects) {
        if (!effect.enabled) continue;
        if (effect.kind == QLatin1String("drop-shadow")) {
            const CanvasAppearanceRules::Shadow s = CanvasAppearanceRules::shadow(effect);
            // Offsets snap to whole layer pixels; SVG output keeps them exact.
            const QPoint offset(int(std::lround(s.offset.x() * pixels)),
                                int(std::lround(s.offset.y() * pixels)));
            CanvasEffects::dropShadow(layer, offset, s.blur * pixels, s.color);
        } else if (effect.kind == QLatin1String("gaussian-blur")) {
            CanvasEffects::gaussianBlur(layer, CanvasAppearanceRules::blurRadius(effect) * pixels);
        }
    }
    painter->save();
    painter->resetTransform();
    painter->setOpacity(painter->opacity() * o.appearance.opacity);
    painter->setCompositionMode(CanvasAppearanceRules::compositionMode(o.appearance.blend));
    if (factor < 1) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(QRectF(area), layer);
    } else {
        painter->drawImage(area.topLeft(), layer);
    }
    painter->restore();
}

} // namespace QindaTK::CanvasAppearancePaint
