// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_object.h"

#include <QHash>
#include <QPolygonF>
#include <QRectF>
#include <QVector>

namespace QindaTK::CanvasGeometry {

// AGENT-NOTE: curved outlines are flattened with this many segments. The
// radial error is r * (1 - cos(pi / N)), 0.12% of the radius at 64, which is
// below one screen pixel for any ellipse up to ~800 px on screen. Filled
// containment of ellipses and rectangles is exact (tested in local space).
inline constexpr int kEllipseSegments = 64;

// An object is renderable and pickable only when its transform is finite
// and invertible. A singular transform collapses the shape to a line or a
// point; such objects are "degenerate": kept in the document, excluded from
// drawing and picking, and listed by CanvasDocument::degenerateObjects().
[[nodiscard]] bool isRenderable(const CanvasObject &object);

// Outline in DOCUMENT space. Closed for Rect/Ellipse/Image, open for Polyline.
[[nodiscard]] QPolygonF documentOutline(const CanvasObject &object,
                                        int ellipseSegments = kEllipseSegments);
[[nodiscard]] bool isClosed(const CanvasObject &object);
// Document bounds including the stroke's outward reach (half the width when
// centred, the whole width outside, none inside); empty when degenerate.
[[nodiscard]] QRectF documentBounds(const CanvasObject &object);
// AGENT-CONTRACT (docs/vector-authoring.md#appearance, D-107): documentBounds
// grown by the object's enabled drop shadows and blurs, the area its pixels
// can reach. Spatial indexes and damage use it, so an adapter's candidates()
// must index objects by it; selection, alignment and snapping keep
// documentBounds.
[[nodiscard]] QRectF paintBounds(const CanvasObject &object);

// Exact filled containment of a document point (false for Polyline).
[[nodiscard]] bool fillContains(const CanvasObject &object, QPointF documentPoint);

[[nodiscard]] qreal distanceToSegment(QPointF p, QPointF a, QPointF b);
// Distance from p to the outline, and the index of the nearest segment.
[[nodiscard]] qreal distanceToOutline(QPointF p, const QPolygonF &outline, bool closed,
                                      int *segment = nullptr);
[[nodiscard]] bool polygonContains(const QPolygonF &polygon, QPointF p); // odd-even
[[nodiscard]] bool segmentsIntersect(QPointF a, QPointF b, QPointF c, QPointF d);
// True when any part of the outline (edge or, if closed, interior) meets the
// region polygon.
[[nodiscard]] bool outlineTouchesRegion(const QPolygonF &outline, bool closed,
                                        const QPolygonF &region);
[[nodiscard]] bool outlineInsideRegion(const QPolygonF &outline, const QPolygonF &region);

// Uniform-grid spatial index over document bounds. Queries return candidate
// ids sorted ascending so every consumer iterates deterministically.
class SpatialIndex {
public:
    explicit SpatialIndex(qreal cellSize = 512.0) : m_cellSize(cellSize) {}
    void insert(CanvasObjectId id, const QRectF &bounds);
    void remove(CanvasObjectId id);
    void clear();
    [[nodiscard]] QVector<CanvasObjectId> query(const QRectF &region) const;
    [[nodiscard]] qsizetype size() const { return m_entries.size(); }
    [[nodiscard]] qreal cellSize() const { return m_cellSize; }
    // Index entries examined by the most recent query (bounded-work evidence).
    [[nodiscard]] qsizetype lastVisited() const { return m_lastVisited; }

private:
    // An object spanning more cells than this lives in a flat list instead.
    static constexpr qsizetype kMaxCellsPerObject = 256;
    struct Entry {
        QRectF bounds;
        bool oversize = false;
    };
    [[nodiscard]] QRect cellRange(const QRectF &bounds) const;
    static quint64 key(int x, int y);
    qreal m_cellSize;
    QHash<quint64, QVector<CanvasObjectId>> m_cells;
    QHash<CanvasObjectId, Entry> m_entries;
    QVector<CanvasObjectId> m_oversize;
    mutable qsizetype m_lastVisited = 0;
};

} // namespace QindaTK::CanvasGeometry
