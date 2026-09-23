// SPDX-License-Identifier: LGPL-3.0-or-later
// Scene-graph rendering provider: document -> vertex-coloured triangles and
// image-tile textures, built on the GUI thread, uploaded by the render thread.
#include "canvas_render.h"
#include "canvas_transform.h"

#include <algorithm>
#include <cmath>

namespace QindaTK::CanvasTessellator {

using namespace CanvasGeometry;

namespace {

CanvasVertex vertex(QPointF p, const QColor &c)
{
    const qreal a = c.alphaF();
    return {float(p.x()), float(p.y()), uchar(std::lround(c.red() * a)),
            uchar(std::lround(c.green() * a)), uchar(std::lround(c.blue() * a)),
            uchar(c.alpha())};
}

void triangle(QVector<CanvasVertex> &out, QPointF a, QPointF b, QPointF c, const QColor &color)
{
    out << vertex(a, color) << vertex(b, color) << vertex(c, color);
}

void quad(QVector<CanvasVertex> &out, const QRectF &r, const QColor &color)
{
    triangle(out, r.topLeft(), r.topRight(), r.bottomRight(), color);
    triangle(out, r.topLeft(), r.bottomRight(), r.bottomLeft(), color);
}

// Affine images of rectangles and ellipses are convex, so a centroid fan is
// an exact triangulation.
void convexFill(QVector<CanvasVertex> &out, const QPolygonF &outline, const QColor &color)
{
    if (outline.size() < 3) {
        return;
    }
    QPointF centre;
    for (const QPointF &p : outline) {
        centre += p;
    }
    centre /= qreal(outline.size());
    for (int i = 0; i < outline.size(); ++i) {
        triangle(out, centre, outline[i], outline[(i + 1) % outline.size()], color);
    }
}

QPointF normal(QPointF a, QPointF b)
{
    const QPointF d = b - a;
    const qreal len = std::hypot(d.x(), d.y());
    return len > 0 ? QPointF(-d.y() / len, d.x() / len) : QPointF();
}

// Miter-joined stroke with the painter's settings (miter limit 4, flat caps),
// so both providers draw the same outline.
void stroke(QVector<CanvasVertex> &out, const QPolygonF &line, bool closed, qreal half,
            const QColor &color)
{
    const int n = int(line.size());
    if (n < 2) {
        return;
    }
    const int segments = closed ? n : n - 1;
    QVector<QPointF> left(n);
    QVector<QPointF> right(n);
    for (int i = 0; i < n; ++i) {
        const bool hasPrev = closed || i > 0;
        const bool hasNext = closed || i < n - 1;
        const QPointF nPrev = hasPrev ? normal(line[(i - 1 + n) % n], line[i]) : QPointF();
        const QPointF nNext = hasNext ? normal(line[i], line[(i + 1) % n]) : QPointF();
        QPointF miter = hasPrev && hasNext ? nPrev + nNext : (hasPrev ? nPrev : nNext);
        const qreal len = std::hypot(miter.x(), miter.y());
        qreal scale = half;
        if (hasPrev && hasNext && len > 1e-9) {
            miter /= len;
            const qreal cosHalf = QPointF::dotProduct(miter, nNext);
            scale = half / std::max(cosHalf, 0.25); // miter limit 4
        } else if (len > 1e-9) {
            miter /= len;
        }
        left[i] = line[i] + miter * scale;
        right[i] = line[i] - miter * scale;
    }
    for (int i = 0; i < segments; ++i) {
        const int j = (i + 1) % n;
        triangle(out, left[i], left[j], right[j], color);
        triangle(out, left[i], right[j], right[i], color);
    }
}

void imageRuns(QVector<CanvasGeometryRun> &runs, const CanvasDocumentAdapter &doc,
               const CanvasObject &o, const QRectF &rect)
{
    const CanvasTiledImagePtr image = doc.image(o.image);
    const auto inverse = CanvasTransform::invert(o.transform);
    if (!image || !inverse || image->size.isEmpty() || o.rect.isEmpty()) {
        return;
    }
    const qreal sx = image->size.width() / o.rect.width();
    const qreal sy = image->size.height() / o.rect.height();
    const QRectF local = inverse->mapRect(rect).intersected(o.rect);
    const QRectF pixels((local.left() - o.rect.left()) * sx, (local.top() - o.rect.top()) * sy,
                        local.width() * sx, local.height() * sy);
    const int c0 = std::max(0, int(std::floor(pixels.left() / image->tileSize)));
    const int r0 = std::max(0, int(std::floor(pixels.top() / image->tileSize)));
    const int c1 = std::min(image->columns() - 1, int(std::floor(pixels.right() / image->tileSize)));
    const int r1 = std::min(image->rows() - 1, int(std::floor(pixels.bottom() / image->tileSize)));
    for (int row = r0; row <= r1; ++row) {
        for (int column = c0; column <= c1; ++column) {
            const QRect src = image->tileRect(column, row);
            CanvasGeometryRun run;
            run.kind = CanvasGeometryRun::Image;
            // Crop edge tiles so the texture maps 1:1 onto localRect.
            const QImage &tile = image->tiles.at(row * image->columns() + column);
            run.image = src.size() == tile.size() ? tile : tile.copy(QRect(QPoint(0, 0), src.size()));
            run.localRect = QRectF(o.rect.left() + src.left() / sx, o.rect.top() + src.top() / sy,
                                   src.width() / sx, src.height() / sy);
            run.toDocument = o.transform;
            runs << run;
        }
    }
}

} // namespace

CanvasTileContent build(const CanvasDocumentAdapter &doc, const QRectF &rect, qreal scale,
                        CanvasPaintStats *stats)
{
    CanvasTileContent content;
    CanvasGeometryRun current;
    auto flush = [&] {
        if (!current.vertices.isEmpty()) {
            content.bytes += current.vertices.size() * qsizetype(sizeof(CanvasVertex));
            content.runs << std::move(current);
            current = CanvasGeometryRun{};
        }
    };
    const QRectF page = doc.pageRect().intersected(rect);
    if (!page.isEmpty()) {
        quad(current.vertices, page, doc.pageColor());
    }
    const qreal pixel = 1.0 / std::max(scale, 1e-9);
    for (const CanvasObject *o : CanvasPainter::paintOrder(doc, rect, stats)) {
        const QRectF bounds = documentBounds(*o);
        if (o->shape == CanvasShape::Image) {
            flush();
            const qsizetype before = content.runs.size();
            imageRuns(content.runs, doc, *o, rect);
            // Image pixels are shared with the document, but each run costs
            // a texture upload; account for it against the cache budget.
            for (qsizetype i = before; i < content.runs.size(); ++i) {
                content.bytes += content.runs[i].image.sizeInBytes();
            }
            if (stats) {
                ++stats->drawn;
            }
            continue;
        }
        if (bounds.width() < pixel && bounds.height() < pixel) {
            quad(current.vertices, QRectF(bounds.center() - QPointF(pixel / 2, pixel / 2),
                                          QSizeF(pixel, pixel)),
                 o->hasFill() ? o->fill : o->stroke);
            if (stats) {
                ++stats->dots;
            }
            continue;
        }
        const qreal radius = std::max(bounds.width(), bounds.height()) * scale / 2.0;
        const QPolygonF outline = documentOutline(*o, CanvasPainter::ellipseSegments(radius));
        if (o->hasFill()) {
            convexFill(current.vertices, outline, o->fill);
        }
        if (o->hasStroke()) {
            stroke(current.vertices, outline, isClosed(*o), std::max(o->strokeWidth, pixel) / 2.0,
                   o->stroke);
        }
        if (stats) {
            ++stats->drawn;
        }
    }
    flush();
    return content;
}

} // namespace QindaTK::CanvasTessellator
