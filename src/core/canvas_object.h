// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>
#include <memory>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#object-model): the reference spatial object
// model. Identity is a stable 64-bit id that is never reused inside one
// CanvasDocument, so a hit result, a selection or an undo step can outlive
// edits to other objects. Geometry is stored in the object's LOCAL space and
// `transform` maps local -> document. Paint order is (layer order, z);
// larger draws later and is hit first.
using CanvasObjectId = quint64;
using CanvasLayerId = quint64;
using CanvasImageId = quint64;

enum class CanvasShape : quint8 { Rect, Ellipse, Polyline, Image };

// Subobject parts a hit can resolve to. `index` on a hit names the vertex,
// segment or image tile; it is -1 for Fill/Stroke of a whole outline.
enum class CanvasPart : quint8 { Fill, Stroke, Vertex, Segment, ImageTile };

struct CanvasObject {
    CanvasObjectId id = 0;
    CanvasLayerId layer = 0;
    CanvasShape shape = CanvasShape::Rect;
    QRectF rect;              // Rect, Ellipse, Image: local rectangle
    QVector<QPointF> points;  // Polyline: local vertices (open path)
    QTransform transform;     // local -> document
    QColor fill;              // invalid or transparent = no fill
    QColor stroke;            // invalid or transparent = no stroke
    qreal strokeWidth = 0.0;  // document units; renderers floor at one device pixel
    CanvasImageId image = 0;  // Image: the document-owned tiled image
    qint64 z = 0;             // order within the layer
    bool visible = true;
    bool locked = false;
    QString name;

    friend bool operator==(const CanvasObject &, const CanvasObject &) = default;

    [[nodiscard]] bool hasFill() const { return fill.isValid() && fill.alpha() > 0 && shape != CanvasShape::Polyline; }
    [[nodiscard]] bool hasStroke() const { return stroke.isValid() && stroke.alpha() > 0; }
};

struct CanvasLayer {
    CanvasLayerId id = 0;
    QString name;
    int order = 0;
    bool visible = true;
    bool locked = false;

    friend bool operator==(const CanvasLayer &, const CanvasLayer &) = default;
};

// AGENT-NOTE: tiled pixel storage for the image kind. Tiles are immutable
// QImages (implicitly shared), so a render tile or a GPU upload can keep a
// reference without copying and without racing a later document edit.
struct CanvasTiledImage {
    CanvasImageId id = 0;
    QSize size;
    int tileSize = 256;
    QVector<QImage> tiles; // row-major, columns() x rows()

    [[nodiscard]] int columns() const { return tileSize > 0 ? (size.width() + tileSize - 1) / tileSize : 0; }
    [[nodiscard]] int rows() const { return tileSize > 0 ? (size.height() + tileSize - 1) / tileSize : 0; }
    [[nodiscard]] QRect tileRect(int column, int row) const
    {
        return QRect(column * tileSize, row * tileSize, tileSize, tileSize)
            .intersected(QRect(QPoint(0, 0), size));
    }
};

using CanvasTiledImagePtr = std::shared_ptr<const CanvasTiledImage>;

} // namespace QindaTK
