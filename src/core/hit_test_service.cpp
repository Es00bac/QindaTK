// SPDX-License-Identifier: LGPL-3.0-or-later
#include "hit_test_service.h"
#include "canvas_appearance.h"
#include "canvas_transform.h"
#include "canvas_path.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {

namespace {

using namespace CanvasGeometry;

// Local-space vertices a user can grab: rectangle corners, polyline points.
QPolygonF grabVertices(const CanvasObject &o)
{
    switch (o.shape) {
    case CanvasShape::Rect:
    case CanvasShape::Image:
        return o.transform.map(QPolygonF(QVector<QPointF>{o.rect.topLeft(), o.rect.topRight(),
                                                          o.rect.bottomRight(), o.rect.bottomLeft()}));
    case CanvasShape::Polyline:
        return o.transform.map(QPolygonF(o.points));
    case CanvasShape::Path: {
        QPolygonF nodes;
        for (const auto &node : o.pathNodes) nodes << node.anchor;
        for (const auto &contour : o.extraContours)
            for (const auto &node : contour.nodes) nodes << node.anchor;
        return o.transform.map(nodes);
    }
    case CanvasShape::Ellipse:
        return {};
    }
    return {};
}

int imageTileIndex(const CanvasDocumentAdapter &doc, const CanvasObject &o, QPointF docPoint)
{
    const CanvasTiledImagePtr image = doc.image(o.image);
    const auto inverse = CanvasTransform::invert(o.transform);
    if (!image || !inverse || image->tileSize <= 0 || o.rect.isEmpty()) {
        return -1;
    }
    const QPointF local = inverse->map(docPoint) - o.rect.topLeft();
    // The local rect maps onto the full image size.
    const qreal px = local.x() * image->size.width() / o.rect.width();
    const qreal py = local.y() * image->size.height() / o.rect.height();
    const int column = std::clamp(int(std::floor(px / image->tileSize)), 0, image->columns() - 1);
    const int row = std::clamp(int(std::floor(py / image->tileSize)), 0, image->rows() - 1);
    return row * image->columns() + column;
}

std::optional<CanvasHit> probe(const CanvasDocumentAdapter &doc, const CanvasObject &o,
                               QPointF p, qreal tol)
{
    CanvasHit hit;
    hit.object = o.id;
    hit.layer = o.layer;
    hit.locked = doc.effectiveLocked(o);
    hit.paintKey = doc.paintKey(o);

    const QPolygonF vertices = grabVertices(o);
    qreal bestVertex = std::numeric_limits<qreal>::infinity();
    int vertexIndex = -1;
    for (int i = 0; i < vertices.size(); ++i) {
        const QPointF d = vertices[i] - p;
        const qreal dist = std::hypot(d.x(), d.y());
        if (dist < bestVertex) {
            bestVertex = dist;
            vertexIndex = i;
        }
    }
    if (vertexIndex >= 0 && bestVertex <= tol) {
        hit.part = CanvasPart::Vertex;
        hit.index = vertexIndex;
        hit.distance = bestVertex;
        return hit;
    }
    const QPolygonF outline = documentOutline(o);
    int segment = -1;
    qreal edge = distanceToOutline(p, outline, isClosed(o), &segment);
    if (o.shape == CanvasShape::Path && !o.extraContours.isEmpty()) {
        int base = o.pathNodes.size();
        const auto polygons = CanvasPath::document(o).toSubpathPolygons();
        for (int c = 1; c < polygons.size() && c <= o.extraContours.size(); ++c) {
            int localSegment = -1;
            const qreal distance = distanceToOutline(p, polygons[c],
                o.extraContours[c - 1].closed, &localSegment);
            if (distance < edge) { edge = distance; segment = base + localSegment; }
            base += o.extraContours[c - 1].nodes.size();
        }
    }
    // An inside or outside stroke covers its whole width on one side (D-106).
    const qreal reach = tol + CanvasAppearanceRules::strokeReach(o);
    if (edge <= reach) {
        const bool segmented = o.shape != CanvasShape::Ellipse;
        hit.part = segmented ? CanvasPart::Segment : CanvasPart::Stroke;
        hit.index = segmented ? segment : -1;
        hit.distance = edge;
        return hit;
    }
    if (fillContains(o, p)) {
        const bool image = o.shape == CanvasShape::Image;
        hit.part = image ? CanvasPart::ImageTile : CanvasPart::Fill;
        hit.index = image ? imageTileIndex(doc, o, p) : -1;
        hit.distance = 0;
        return hit;
    }
    return std::nullopt;
}

bool orderedBefore(const CanvasHit &a, const CanvasHit &b)
{
    if (a.paintKey != b.paintKey) {
        return a.paintKey > b.paintKey;
    }
    return a.object > b.object;
}

} // namespace

void HitTestService::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document != document) {
        m_document = document;
        emit changed();
    }
}

void HitTestService::setTolerance(qreal pixels)
{
    pixels = std::isfinite(pixels) ? std::max(0.0, pixels) : 0.0;
    if (!qFuzzyCompare(pixels + 1, m_tolerance + 1)) {
        m_tolerance = pixels;
        emit changed();
    }
}

qreal HitTestService::documentTolerance(qreal tolerancePixels, qreal zoom)
{
    if (!std::isfinite(zoom) || zoom <= 0 || !std::isfinite(tolerancePixels)) {
        return 0.0;
    }
    return std::max(0.0, tolerancePixels) / zoom;
}

QVector<CanvasHit> HitTestService::hitPoint(const CanvasDocumentAdapter &doc, QPointF p,
                                            qreal tol, const CanvasHitPolicy &policy,
                                            CanvasHitStats *stats)
{
    QVector<CanvasHit> hits;
    if (!std::isfinite(p.x()) || !std::isfinite(p.y())) {
        return hits;
    }
    tol = std::isfinite(tol) ? std::max(0.0, tol) : 0.0;
    const QVector<CanvasObjectId> ids = doc.candidates(QRectF(p.x() - tol, p.y() - tol, 2 * tol, 2 * tol));
    if (stats) {
        stats->candidates += ids.size();
    }
    for (CanvasObjectId id : ids) {
        const CanvasObject *o = doc.object(id);
        if (!o || !isRenderable(*o) || (!policy.includeHidden && !doc.effectiveVisible(*o))) {
            continue;
        }
        if (policy.locked == CanvasLockPolicy::Skip && doc.effectiveLocked(*o)) {
            continue;
        }
        if (stats) {
            ++stats->tested;
        }
        if (auto hit = probe(doc, *o, p, tol)) {
            hits.append(*hit);
        }
    }
    std::sort(hits.begin(), hits.end(), orderedBefore);
    if (policy.locked == CanvasLockPolicy::Block) {
        const auto firstLocked = std::find_if(hits.begin(), hits.end(),
                                              [](const CanvasHit &h) { return h.locked; });
        if (firstLocked != hits.end()) {
            hits.erase(firstLocked + 1, hits.end());
        }
    }
    if (policy.maxResults > 0 && hits.size() > policy.maxResults) {
        hits.resize(policy.maxResults);
    }
    return hits;
}

QVector<CanvasObjectId> HitTestService::hitRegion(const CanvasDocumentAdapter &doc,
                                                  const QPolygonF &region, CanvasRegionMode mode,
                                                  const CanvasHitPolicy &policy,
                                                  CanvasHitStats *stats)
{
    QVector<CanvasHit> hits;
    if (region.size() < 3) {
        return {};
    }
    const QVector<CanvasObjectId> ids = doc.candidates(region.boundingRect());
    if (stats) {
        stats->candidates += ids.size();
    }
    for (CanvasObjectId id : ids) {
        const CanvasObject *o = doc.object(id);
        if (!o || !isRenderable(*o) || (!policy.includeHidden && !doc.effectiveVisible(*o))) {
            continue;
        }
        const bool locked = doc.effectiveLocked(*o);
        // A region has no single topmost object, so Block degrades to Skip.
        if (locked && policy.locked != CanvasLockPolicy::Report) {
            continue;
        }
        if (stats) {
            ++stats->tested;
        }
        const QPolygonF outline = documentOutline(*o);
        bool match = mode == CanvasRegionMode::Contain
            ? outlineInsideRegion(outline, region)
            : outlineTouchesRegion(outline, isClosed(*o), region);
        if (o->shape == CanvasShape::Path && !o->extraContours.isEmpty()) {
            const auto polygons = CanvasPath::document(*o).toSubpathPolygons();
            for (int c = 1; c < polygons.size() && c <= o->extraContours.size(); ++c) {
                const bool current = mode == CanvasRegionMode::Contain
                    ? outlineInsideRegion(polygons[c], region)
                    : outlineTouchesRegion(polygons[c], o->extraContours[c - 1].closed, region);
                if (mode == CanvasRegionMode::Contain) match = match && current;
                else match = match || current;
            }
        }
        if (match) {
            CanvasHit h;
            h.object = o->id;
            h.paintKey = doc.paintKey(*o);
            hits.append(h);
        }
    }
    std::sort(hits.begin(), hits.end(), orderedBefore);
    if (policy.maxResults > 0 && hits.size() > policy.maxResults) {
        hits.resize(policy.maxResults);
    }
    QVector<CanvasObjectId> out;
    out.reserve(hits.size());
    for (const CanvasHit &h : hits) {
        out.append(h.object);
    }
    return out;
}

QString HitTestService::partName(CanvasPart part)
{
    switch (part) {
    case CanvasPart::Fill: return QStringLiteral("fill");
    case CanvasPart::Stroke: return QStringLiteral("stroke");
    case CanvasPart::Vertex: return QStringLiteral("vertex");
    case CanvasPart::Segment: return QStringLiteral("segment");
    case CanvasPart::ImageTile: return QStringLiteral("imageTile");
    }
    return {};
}

CanvasLockPolicy HitTestService::lockPolicy(const QString &name)
{
    if (name == QLatin1String("report")) {
        return CanvasLockPolicy::Report;
    }
    if (name == QLatin1String("block")) {
        return CanvasLockPolicy::Block;
    }
    return CanvasLockPolicy::Skip;
}

QVariantMap HitTestService::toVariant(const CanvasHit &hit)
{
    return {{QStringLiteral("objectId"), qulonglong(hit.object)},
            {QStringLiteral("layerId"), qulonglong(hit.layer)},
            {QStringLiteral("part"), partName(hit.part)},
            {QStringLiteral("index"), hit.index},
            {QStringLiteral("distance"), hit.distance},
            {QStringLiteral("locked"), hit.locked}};
}

QVariantList HitTestService::pointAt(qreal x, qreal y, qreal zoom, const QString &lock,
                                     int maxResults) const
{
    QVariantList out;
    if (!m_document) {
        return out;
    }
    CanvasHitPolicy policy;
    policy.locked = lockPolicy(lock);
    policy.maxResults = maxResults;
    for (const CanvasHit &h : hitPoint(*m_document, QPointF(x, y),
                                       documentTolerance(m_tolerance, zoom), policy)) {
        out << toVariant(h);
    }
    return out;
}

QVariantList HitTestService::regionIds(const QRectF &rect, bool contain) const
{
    QVariantList out;
    if (!m_document) {
        return out;
    }
    const auto mode = contain ? CanvasRegionMode::Contain : CanvasRegionMode::Intersect;
    for (CanvasObjectId id : hitRegion(*m_document, QPolygonF(rect), mode)) {
        out << qulonglong(id);
    }
    return out;
}

} // namespace QindaTK
