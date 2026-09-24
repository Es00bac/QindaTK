// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"
#include "canvas_transform.h"
#include "color_context.h"

#include <QColorSpace>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <memory>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#rendering): these types cross from the GUI
// thread (which prepares them) to the scene-graph render thread (which only
// reads them while the GUI thread is blocked in sync). Everything here is an
// immutable value or an implicitly shared QImage; nothing points into the
// document, so the render thread never reads document state.

enum class CanvasBackend : quint8 { Software, SceneGraph };

// Plain document QColor values and the composition tile are sRGB. Managed
// fills resolve their owned ICC values before composition; a valid RGB output
// profile converts the complete tile. Proof output remains unsupported.
struct CanvasColorContext {
    QColorSpace working{QColorSpace::SRgb};
    QColorSpace display{QColorSpace::SRgb};
    ColorIntent intent = ColorIntent::RelativeColorimetric;
    bool blackPointCompensation = false;
    bool proofRequested = false;
    quint64 generation = 1;
    [[nodiscard]] QString description() const;
    [[nodiscard]] QString validationError() const;
    [[nodiscard]] bool needsDisplayTransform() const { return working != display; }
};

struct CanvasTileKey {
    int lod = 0;
    int x = 0;
    int y = 0;
    friend bool operator==(const CanvasTileKey &, const CanvasTileKey &) = default;
};
inline size_t qHash(const CanvasTileKey &k, size_t seed = 0)
{
    return qHashMulti(seed, k.lod, k.x, k.y);
}

struct CanvasVertex {
    float x, y;
    uchar r, g, b, a; // premultiplied
};

// One draw step of a scene-graph tile, in paint order.
struct CanvasGeometryRun {
    enum Kind : quint8 { Triangles, Image };
    Kind kind = Triangles;
    QVector<CanvasVertex> vertices;  // Triangles, document coordinates
    QImage image;                    // Image: one document image tile
    QRectF localRect;                // Image: where the tile sits in object space
    QTransform toDocument;           // Image: object transform
};

struct CanvasTileContent {
    QImage raster;                   // Software provider
    QVector<CanvasGeometryRun> runs; // SceneGraph provider
    qsizetype bytes = 0;
};

struct CanvasPaintStats {
    qsizetype candidates = 0; // index hits examined
    qsizetype drawn = 0;      // objects emitted at full detail
    qsizetype dots = 0;       // sub-pixel objects reduced to one device pixel
    qsizetype skipped = 0;    // hidden or degenerate
};

// Document -> pixels, shared by providers and tests.
namespace CanvasPainter {
// Paints `documentRect` of `document` into `painter`, whose transform must
// already map document units to device pixels at `scale` px per unit.
void paint(QPainter *painter, const CanvasDocumentAdapter &document, const QRectF &documentRect,
           qreal scale, CanvasPaintStats *stats = nullptr);
// Renders one tile image (`pixels` square) for `documentRect`.
[[nodiscard]] QImage renderTile(const CanvasDocumentAdapter &document, const QRectF &documentRect,
                                int pixels, const CanvasColorContext &color,
                                CanvasPaintStats *stats = nullptr);
// Renderable, effectively visible objects meeting `documentRect`, in paint
// order (paintKey ascending, ties by id). Hidden/degenerate ones are counted
// in stats->skipped.
[[nodiscard]] QVector<const CanvasObject *> paintOrder(const CanvasDocumentAdapter &document,
                                                       const QRectF &documentRect,
                                                       CanvasPaintStats *stats);
// Segments used to flatten an ellipse of `radiusPixels` on screen.
[[nodiscard]] int ellipseSegments(qreal radiusPixels);
} // namespace CanvasPainter

namespace CanvasTessellator {
// Builds scene-graph runs for `documentRect` at `scale` device px per unit.
[[nodiscard]] CanvasTileContent build(const CanvasDocumentAdapter &document,
                                      const QRectF &documentRect, qreal scale,
                                      CanvasPaintStats *stats = nullptr);
} // namespace CanvasTessellator

// Visible-region tile planning, independent of any backend.
namespace CanvasTiles {
// Level of detail: device pixels per document unit are 2^lod, the smallest
// power of two >= zoom * dpr, so tiles are never magnified on screen.
[[nodiscard]] int levelOfDetail(qreal zoom, qreal devicePixelRatio);
[[nodiscard]] qreal tileDocumentSize(int lod, int tilePixels);
[[nodiscard]] QRectF tileRect(const CanvasTileKey &key, int tilePixels);
// Tiles meeting the (possibly rotated) visible quad, nearest the view centre
// first, ties by (y, x).
[[nodiscard]] QVector<CanvasTileKey> visible(const CanvasViewPose &pose, const QSizeF &viewSize,
                                             qreal devicePixelRatio, int tilePixels);
// Upper bound on visible() for any pose at this view size and ratio.
[[nodiscard]] int maxVisible(const QSizeF &viewSize, qreal devicePixelRatio, int tilePixels);
} // namespace CanvasTiles

struct CanvasRenderRequest {
    quint64 documentRevision = 0;
    CanvasViewPose pose;
    QSizeF viewSize;
    qreal devicePixelRatio = 1.0;
    CanvasColorContext color;
};

struct CanvasRenderItem {
    CanvasTileKey key;
    QRectF documentRect;
    quint64 serial = 0; // changes whenever content changes
    bool stale = false; // shown while a newer version is pending
    std::shared_ptr<const CanvasTileContent> content;
};

struct CanvasRenderFrame {
    CanvasRenderRequest request;
    CanvasBackend backend = CanvasBackend::Software;
    quint64 resourceGeneration = 0;
    int tilePixels = 256;
    QTransform documentToView;
    QVector<CanvasRenderItem> items;
};

} // namespace QindaTK
