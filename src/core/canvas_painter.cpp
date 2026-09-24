// SPDX-License-Identifier: LGPL-3.0-or-later
// Software rendering provider: document -> QPainter -> tile images.
#include "canvas_render.h"
#include "canvas_transform.h"
#include "canvas_managed_paint.h"
#include "canvas_path.h"

#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <memory>

namespace QindaTK::CanvasPainter {

using namespace CanvasGeometry;

int ellipseSegments(qreal radiusPixels)
{
    if (!std::isfinite(radiusPixels) || radiusPixels <= 0) {
        return 8;
    }
    return std::clamp(int(std::ceil(4.0 * std::sqrt(radiusPixels))), 8, kEllipseSegments);
}

QVector<const CanvasObject *> paintOrder(const CanvasDocumentAdapter &doc, const QRectF &rect,
                                         CanvasPaintStats *stats)
{
    const QVector<CanvasObjectId> ids = doc.candidates(rect);
    QVector<const CanvasObject *> objects;
    objects.reserve(ids.size());
    for (CanvasObjectId id : ids) {
        const CanvasObject *o = doc.object(id);
        if (!o) {
            continue;
        }
        if (!isRenderable(*o) || !doc.effectiveVisible(*o)) {
            if (stats) {
                ++stats->skipped;
            }
            continue;
        }
        objects.append(o);
    }
    if (stats) {
        stats->candidates += ids.size();
    }
    std::sort(objects.begin(), objects.end(), [&doc](const CanvasObject *a, const CanvasObject *b) {
        const qint64 ka = doc.paintKey(*a);
        const qint64 kb = doc.paintKey(*b);
        return ka != kb ? ka < kb : a->id < b->id;
    });
    return objects;
}

namespace {

void paintImage(QPainter *painter, const CanvasDocumentAdapter &doc, const CanvasObject &o,
                const QRectF &rect, const QTransform &base)
{
    const CanvasTiledImagePtr image = doc.image(o.image);
    const auto inverse = CanvasTransform::invert(o.transform);
    if (!image || !inverse || image->size.isEmpty() || o.rect.isEmpty()) {
        return;
    }
    // Visible part of the object in image pixel space.
    const qreal sx = image->size.width() / o.rect.width();
    const qreal sy = image->size.height() / o.rect.height();
    const QRectF local = inverse->mapRect(rect).intersected(o.rect);
    const QRectF pixels((local.left() - o.rect.left()) * sx, (local.top() - o.rect.top()) * sy,
                        local.width() * sx, local.height() * sy);
    const int c0 = std::max(0, int(std::floor(pixels.left() / image->tileSize)));
    const int r0 = std::max(0, int(std::floor(pixels.top() / image->tileSize)));
    const int c1 = std::min(image->columns() - 1, int(std::floor(pixels.right() / image->tileSize)));
    const int r1 = std::min(image->rows() - 1, int(std::floor(pixels.bottom() / image->tileSize)));
    painter->setTransform(o.transform * base);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (int row = r0; row <= r1; ++row) {
        for (int column = c0; column <= c1; ++column) {
            const QRect src = image->tileRect(column, row);
            const QImage &tile = image->tiles.at(row * image->columns() + column);
            const QRectF target(o.rect.left() + src.left() / sx, o.rect.top() + src.top() / sy,
                                src.width() / sx, src.height() / sy);
            painter->drawImage(target, tile, QRectF(0, 0, src.width(), src.height()));
        }
    }
    painter->setTransform(base);
}

} // namespace

void paint(QPainter *painter, const CanvasDocumentAdapter &doc, const QRectF &rect, qreal scale,
           CanvasPaintStats *stats)
{
    const QTransform base = painter->transform();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF page = doc.pageRect().intersected(rect);
    if (!page.isEmpty()) {
        painter->fillRect(page, doc.pageColor());
    }
    const qreal pixel = 1.0 / std::max(scale, 1e-9);
    std::unique_ptr<ColorEngine> engine;
    for (const CanvasObject *o : paintOrder(doc, rect, stats)) {
        const QRectF bounds = documentBounds(*o);
        OperationError colorError;
        if (!o->managedFill.isEmpty() && !engine) engine = std::make_unique<ColorEngine>();
        const QBrush fillBrush = o->managedFill.isEmpty() ? QBrush(o->fill)
            : CanvasManagedPaint::brush(o->managedFill, bounds, *engine, &colorError);
        if (o->shape == CanvasShape::Image) {
            paintImage(painter, doc, *o, rect, base);
            if (stats) {
                ++stats->drawn;
            }
            continue;
        }
        if (bounds.width() < pixel && bounds.height() < pixel) {
            // Sub-pixel: one device pixel keeps the density visible without
            // spending a path on it.
            const QColor c = o->hasFill() ? o->fill : o->stroke;
            painter->fillRect(QRectF(bounds.center() - QPointF(pixel / 2, pixel / 2),
                                     QSizeF(pixel, pixel)), c);
            if (stats) {
                ++stats->dots;
            }
            continue;
        }
        const qreal radius = std::max(bounds.width(), bounds.height()) * scale / 2.0;
        if (o->shape == CanvasShape::Path) {
            const QPainterPath path = CanvasPath::document(*o);
            if (o->hasFill()) painter->fillPath(path, colorError ? QBrush(o->fill) : fillBrush);
            if (o->hasStroke()) {
                painter->setPen(CanvasPath::pen(*o, pixel));
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(path);
            }
            if (stats) ++stats->drawn;
            continue;
        }
        const QPolygonF outline = documentOutline(*o, ellipseSegments(radius));
        if (o->hasFill()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(colorError ? QBrush(o->fill) : fillBrush);
            painter->drawPolygon(outline, o->fillRule);
        }
        if (o->hasStroke()) {
            painter->setPen(CanvasPath::pen(*o, pixel));
            painter->setBrush(Qt::NoBrush);
            if (isClosed(*o)) {
                painter->drawPolygon(outline);
            } else {
                painter->drawPolyline(outline);
            }
        }
        if (stats) {
            ++stats->drawn;
        }
    }
}

QImage renderTile(const CanvasDocumentAdapter &doc, const QRectF &rect, int pixels,
                  const CanvasColorContext &color, CanvasPaintStats *stats)
{
    if (!color.validationError().isEmpty()) return {};
    QImage tile(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::transparent);
    tile.setColorSpace(color.working);
    {
        QPainter painter(&tile);
        const qreal scale = pixels / rect.width();
        painter.scale(scale, scale);
        painter.translate(-rect.topLeft());
        painter.setClipRect(rect);
        paint(&painter, doc, rect, scale, stats);
    }
    // AGENT-GUARD: convert only after the complete tile has been composited.
    // Per-object conversion changes alpha blending and makes CPU/GPU differ.
    if (color.needsDisplayTransform())
        return tile.convertedToColorSpace(color.display, QImage::Format_ARGB32_Premultiplied);
    return tile;
}

} // namespace QindaTK::CanvasPainter
