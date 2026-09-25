// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_geometry.h"
#include "canvas_appearance.h"
#include "canvas_transform.h"
#include "canvas_path.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace QindaTK::CanvasGeometry {

bool isRenderable(const CanvasObject &object)
{
    if (!CanvasTransform::isInvertible(object.transform)) {
        return false;
    }
    if (object.shape == CanvasShape::Polyline) {
        return !object.points.isEmpty();
    }
    if (object.shape == CanvasShape::Path)
        return object.pathNodes.size() >= (object.pathClosed ? 3 : 2);
    return object.rect.isValid();
}

bool isClosed(const CanvasObject &object)
{
    return object.shape == CanvasShape::Path ? object.pathClosed
                                            : object.shape != CanvasShape::Polyline;
}

QPolygonF documentOutline(const CanvasObject &object, int ellipseSegments)
{
    QPolygonF local;
    switch (object.shape) {
    case CanvasShape::Rect:
    case CanvasShape::Image:
        local << object.rect.topLeft() << object.rect.topRight()
              << object.rect.bottomRight() << object.rect.bottomLeft();
        break;
    case CanvasShape::Ellipse: {
        const int n = std::max(8, ellipseSegments);
        const QPointF c = object.rect.center();
        const qreal rx = object.rect.width() / 2.0;
        const qreal ry = object.rect.height() / 2.0;
        local.reserve(n);
        for (int i = 0; i < n; ++i) {
            const qreal a = 2.0 * M_PI * i / n;
            local << QPointF(c.x() + rx * std::cos(a), c.y() + ry * std::sin(a));
        }
        break;
    }
    case CanvasShape::Polyline:
        local = QPolygonF(object.points);
        break;
    case CanvasShape::Path: {
        const auto polygons = CanvasPath::local(object).toSubpathPolygons();
        if (!polygons.isEmpty()) local = polygons.first();
        if (local.size() > 1 && local.first() == local.last()) local.removeLast();
        break;
    }
    }
    return object.transform.map(local);
}

QRectF documentBounds(const CanvasObject &object)
{
    if (!isRenderable(object)) {
        return {};
    }
    QRectF bounds;
    if (object.shape == CanvasShape::Path) {
        bounds = CanvasPath::document(object).boundingRect();
    } else if (object.shape == CanvasShape::Polyline) {
        bounds = documentOutline(object).boundingRect();
    } else {
        // The ellipse fits inside its mapped rectangle, which is cheaper and
        // conservative; picking refines against the exact shape.
        bounds = object.transform.map(QPolygonF(object.rect)).boundingRect();
    }
    const qreal pad = CanvasAppearanceRules::strokeOutset(object);
    return bounds.adjusted(-pad, -pad, pad, pad);
}

QRectF paintBounds(const CanvasObject &object)
{
    const QRectF bounds = documentBounds(object);
    if (bounds.isNull() || object.appearance->effects.isEmpty()) return bounds;
    return bounds.marginsAdded(CanvasAppearanceRules::effectMargins(object));
}

bool fillContains(const CanvasObject &object, QPointF documentPoint)
{
    if (!object.fillsArea()) {
        return false;
    }
    const auto inverse = CanvasTransform::invert(object.transform);
    if (!inverse) {
        return false;
    }
    const QPointF p = inverse->map(documentPoint);
    const QRectF &r = object.rect;
    switch (object.shape) {
    case CanvasShape::Rect:
    case CanvasShape::Image:
        return p.x() >= r.left() && p.x() <= r.right() && p.y() >= r.top() && p.y() <= r.bottom();
    case CanvasShape::Ellipse: {
        const qreal rx = r.width() / 2.0;
        const qreal ry = r.height() / 2.0;
        if (rx <= 0 || ry <= 0) {
            return false;
        }
        const qreal dx = (p.x() - r.center().x()) / rx;
        const qreal dy = (p.y() - r.center().y()) / ry;
        return dx * dx + dy * dy <= 1.0;
    }
    case CanvasShape::Polyline:
        return false;
    case CanvasShape::Path:
        return object.hasClosedContour() && CanvasPath::local(object).contains(p);
    }
    return false;
}

qreal distanceToSegment(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    qreal t = len2 > 0 ? QPointF::dotProduct(p - a, ab) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF d = p - (a + t * ab);
    return std::hypot(d.x(), d.y());
}

qreal distanceToOutline(QPointF p, const QPolygonF &outline, bool closed, int *segment)
{
    qreal best = std::numeric_limits<qreal>::infinity();
    int bestIndex = -1;
    const int n = int(outline.size());
    if (n == 1) {
        const QPointF d = p - outline.first();
        best = std::hypot(d.x(), d.y());
        bestIndex = 0;
    }
    const int segments = closed ? n : n - 1;
    for (int i = 0; i < segments && n > 1; ++i) {
        const qreal d = distanceToSegment(p, outline[i], outline[(i + 1) % n]);
        if (d < best) {
            best = d;
            bestIndex = i;
        }
    }
    if (segment) {
        *segment = bestIndex;
    }
    return best;
}

bool polygonContains(const QPolygonF &polygon, QPointF p)
{
    return polygon.size() >= 3 && polygon.containsPoint(p, Qt::OddEvenFill);
}

namespace {
// Closed-interval overlap: unlike QRectF::intersects it accepts zero-width
// bounds, which a straight unstroked polyline legitimately has.
bool touches(const QRectF &a, const QRectF &b)
{
    return a.left() <= b.right() && b.left() <= a.right()
        && a.top() <= b.bottom() && b.top() <= a.bottom();
}
qreal cross(QPointF o, QPointF a, QPointF b)
{
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}
bool onSegment(QPointF a, QPointF b, QPointF p)
{
    return std::min(a.x(), b.x()) <= p.x() && p.x() <= std::max(a.x(), b.x())
        && std::min(a.y(), b.y()) <= p.y() && p.y() <= std::max(a.y(), b.y());
}
} // namespace

bool segmentsIntersect(QPointF a, QPointF b, QPointF c, QPointF d)
{
    const qreal d1 = cross(c, d, a);
    const qreal d2 = cross(c, d, b);
    const qreal d3 = cross(a, b, c);
    const qreal d4 = cross(a, b, d);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
        return true;
    }
    return (d1 == 0 && onSegment(c, d, a)) || (d2 == 0 && onSegment(c, d, b))
        || (d3 == 0 && onSegment(a, b, c)) || (d4 == 0 && onSegment(a, b, d));
}

bool outlineTouchesRegion(const QPolygonF &outline, bool closed, const QPolygonF &region)
{
    if (outline.isEmpty() || region.size() < 3) {
        return false;
    }
    for (const QPointF &p : outline) {
        if (polygonContains(region, p)) {
            return true;
        }
    }
    if (closed && polygonContains(outline, region.first())) {
        return true; // region entirely inside a filled/closed outline
    }
    const int n = int(outline.size());
    const int m = int(region.size());
    const int segments = closed ? n : n - 1;
    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < m; ++j) {
            if (segmentsIntersect(outline[i], outline[(i + 1) % n], region[j], region[(j + 1) % m])) {
                return true;
            }
        }
    }
    return false;
}

bool outlineInsideRegion(const QPolygonF &outline, const QPolygonF &region)
{
    if (outline.isEmpty() || region.size() < 3) {
        return false;
    }
    return std::all_of(outline.cbegin(), outline.cend(),
                       [&](QPointF p) { return polygonContains(region, p); });
}

// ---- SpatialIndex ---------------------------------------------------------

quint64 SpatialIndex::key(int x, int y)
{
    return (quint64(quint32(x)) << 32) | quint32(y);
}

QRect SpatialIndex::cellRange(const QRectF &b) const
{
    constexpr qreal limit = 1 << 30;
    auto cell = [&](qreal v) {
        return int(std::clamp(std::floor(v / m_cellSize), -limit, limit));
    };
    return QRect(QPoint(cell(b.left()), cell(b.top())), QPoint(cell(b.right()), cell(b.bottom())));
}

static qint64 cellCount(const QRect &cells)
{
    // QRect::width()/height() are int and overflow when the clamped cell
    // interval spans both ends of the ±2^30 index range.
    const qint64 columns = qMax<qint64>(0, qint64(cells.right()) - cells.left() + 1);
    const qint64 rows = qMax<qint64>(0, qint64(cells.bottom()) - cells.top() + 1);
    return columns * rows;
}

void SpatialIndex::insert(CanvasObjectId id, const QRectF &bounds)
{
    remove(id);
    if (bounds.isNull() || !std::isfinite(bounds.left()) || !std::isfinite(bounds.right())
        || !std::isfinite(bounds.top()) || !std::isfinite(bounds.bottom())) {
        return; // degenerate objects are not indexed
    }
    Entry entry{bounds, false};
    const QRect cells = cellRange(bounds);
    if (cellCount(cells) > kMaxCellsPerObject) {
        entry.oversize = true;
        m_oversize.append(id);
    } else {
        for (int y = cells.top(); y <= cells.bottom(); ++y) {
            for (int x = cells.left(); x <= cells.right(); ++x) {
                m_cells[key(x, y)].append(id);
            }
        }
    }
    m_entries.insert(id, entry);
}

void SpatialIndex::remove(CanvasObjectId id)
{
    const auto it = m_entries.constFind(id);
    if (it == m_entries.cend()) {
        return;
    }
    if (it->oversize) {
        m_oversize.removeOne(id);
    } else {
        const QRect cells = cellRange(it->bounds);
        for (int y = cells.top(); y <= cells.bottom(); ++y) {
            for (int x = cells.left(); x <= cells.right(); ++x) {
                const quint64 k = key(x, y);
                auto cell = m_cells.find(k);
                if (cell != m_cells.end()) {
                    cell->removeOne(id);
                    if (cell->isEmpty()) {
                        m_cells.erase(cell);
                    }
                }
            }
        }
    }
    m_entries.erase(it);
}

void SpatialIndex::clear()
{
    m_cells.clear();
    m_entries.clear();
    m_oversize.clear();
}

QVector<CanvasObjectId> SpatialIndex::query(const QRectF &region) const
{
    QVector<CanvasObjectId> result;
    m_lastVisited = 0;
    // A zero-size region is a legitimate point query (zero tolerance).
    if (m_entries.isEmpty() || !std::isfinite(region.left()) || !std::isfinite(region.top())
        || !std::isfinite(region.right()) || !std::isfinite(region.bottom())) {
        return result;
    }
    const QRect cells = cellRange(region);
    if (cellCount(cells) > m_entries.size()) {
        // Cheaper to scan every entry than to walk mostly empty cells.
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            ++m_lastVisited;
            if (touches(it->bounds, region)) {
                result.append(it.key());
            }
        }
    } else {
        for (int y = cells.top(); y <= cells.bottom(); ++y) {
            for (int x = cells.left(); x <= cells.right(); ++x) {
                const auto cell = m_cells.constFind(key(x, y));
                if (cell == m_cells.cend()) {
                    continue;
                }
                for (CanvasObjectId id : *cell) {
                    ++m_lastVisited;
                    const QRectF &b = m_entries.value(id).bounds;
                    if (touches(b, region)) {
                        result.append(id);
                    }
                }
            }
        }
        for (CanvasObjectId id : m_oversize) {
            ++m_lastVisited;
            if (touches(m_entries.value(id).bounds, region)) {
                result.append(id);
            }
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace QindaTK::CanvasGeometry
