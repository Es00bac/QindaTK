// SPDX-License-Identifier: LGPL-3.0-or-later
// Software rendering provider: document -> QPainter -> tile images.
#include "canvas_render.h"
#include "canvas_transform.h"
#include "canvas_appearance.h"
#include "canvas_appearance_paint.h"
#include "canvas_managed_paint.h"

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
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF page = doc.pageRect().intersected(rect);
    if (!page.isEmpty()) {
        painter->fillRect(page, doc.pageColor());
    }
    // Artboard backgrounds are document data below every object (D-093).
    for (const CanvasBackdrop &backdrop : doc.backdrops()) {
        const QRectF board = backdrop.rect.intersected(rect);
        if (!board.isEmpty()) painter->fillRect(board, backdrop.color);
    }
    const qreal pixel = 1.0 / std::max(scale, 1e-9);
    std::unique_ptr<ColorEngine> engine;
    for (const CanvasObject *o : paintOrder(doc, rect, stats)) {
        const QRectF bounds = documentBounds(*o);
        OperationError colorError;
        if (!o->managedFill.isEmpty() && !engine) engine = std::make_unique<ColorEngine>();
        const QBrush fillBrush = o->managedFill.isEmpty() ? QBrush(o->fill)
            : CanvasManagedPaint::brush(o->managedFill, bounds, *engine, &colorError);
        const QBrush brush = colorError ? QBrush(o->fill) : fillBrush;
        const bool image = o->shape == CanvasShape::Image;
        if (!image && bounds.width() < pixel && bounds.height() < pixel) {
            // Sub-pixel: one device pixel keeps the density visible without
            // spending a path on it.
            const QColor c = o->hasFill() ? o->fill
                : o->hasPattern() ? o->appearance->pattern.ink : o->stroke;
            painter->fillRect(QRectF(bounds.center() - QPointF(pixel / 2, pixel / 2),
                                     QSizeF(pixel, pixel)), c);
            if (stats) {
                ++stats->dots;
            }
            continue;
        }
        const auto content = [&](QPainter *target, const QRectF &visible, qreal pixels) {
            if (image) paintImage(target, doc, *o, visible, target->transform());
            else CanvasAppearancePaint::paintVector(target, *o, brush, pixels);
        };
        // Alignment, effects, opacity and blend paint through a layer (D-107).
        if (CanvasAppearanceRules::needsLayer(*o))
            CanvasAppearancePaint::paintLayered(painter, *o, rect, scale, content);
        else
            content(painter, rect, scale);
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
