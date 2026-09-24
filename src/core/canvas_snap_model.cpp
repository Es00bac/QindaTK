// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_snap_model.h"
#include "canvas_geometry.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace QindaTK {
namespace {

struct AxisCandidate {
    qreal position = 0;
    qreal distancePixels = std::numeric_limits<qreal>::infinity();
    int priority = -1;
    QString source;
    QString targetId;
    QString feature;
};

bool finite(qreal value) { return std::isfinite(value); }

QPointF nearestPointOnSegment(QPointF point, QPointF start, QPointF end)
{
    const QPointF edge = end - start;
    const qreal lengthSquared = QPointF::dotProduct(edge, edge);
    if (!finite(lengthSquared) || lengthSquared <= 0) return start;
    const qreal t = std::clamp(QPointF::dotProduct(point - start, edge) / lengthSquared,
                               qreal(0), qreal(1));
    return start + edge * t;
}

void offer(AxisCandidate &best, qreal input, qreal position, qreal zoom,
           qreal tolerancePixels, int priority, const QString &source,
           const QString &targetId, const QString &feature)
{
    if (!finite(position)) return;
    const qreal distance = std::abs(position - input) * zoom;
    if (!finite(distance) || distance > tolerancePixels) return;
    if (priority < best.priority) return;
    if (priority == best.priority) {
        if (distance > best.distancePixels + 1e-9) return;
        if (std::abs(distance - best.distancePixels) <= 1e-9
            && std::tie(source, targetId, feature)
                   >= std::tie(best.source, best.targetId, best.feature)) return;
    }
    best = {position, distance, priority, source, targetId, feature};
}

QVariantMap provenance(const AxisCandidate &candidate)
{
    if (candidate.priority < 0) return {};
    return {{QStringLiteral("source"), candidate.source},
            {QStringLiteral("targetId"), candidate.targetId},
            {QStringLiteral("feature"), candidate.feature},
            {QStringLiteral("position"), candidate.position},
            {QStringLiteral("distancePixels"), candidate.distancePixels}};
}

void offerPoint(AxisCandidate &x, AxisCandidate &y, QPointF input,
                QPointF target, qreal zoom, qreal tolerance, const QString &id,
                const QString &feature)
{
    offer(x, input.x(), target.x(), zoom, tolerance, 200,
          QStringLiteral("object"), id, feature);
    offer(y, input.y(), target.y(), zoom, tolerance, 200,
          QStringLiteral("object"), id, feature);
}

void offerEdgeProjection(AxisCandidate &x, AxisCandidate &y, QPointF input,
                         QPointF target, qreal zoom, qreal tolerance,
                         const QString &id, const QString &feature)
{
    // A projection onto a perpendicular edge can leave one coordinate
    // unchanged. Such a no-op must not beat a useful snap from another edge.
    if (std::abs(target.x() - input.x()) * zoom > 1e-9)
        offer(x, input.x(), target.x(), zoom, tolerance, 200,
              QStringLiteral("object"), id, feature);
    if (std::abs(target.y() - input.y()) * zoom > 1e-9)
        offer(y, input.y(), target.y(), zoom, tolerance, 200,
              QStringLiteral("object"), id, feature);
}

} // namespace

void CanvasSnapModel::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document == document) return;
    m_document = document;
    emit changed();
}

void CanvasSnapModel::setViewState(CanvasViewState *state)
{
    if (m_viewState == state) return;
    m_viewState = state;
    emit changed();
}

void CanvasSnapModel::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    emit changed();
}

void CanvasSnapModel::setTolerancePixels(qreal value)
{
    if (!finite(value) || value < 0 || m_tolerancePixels == value) return;
    m_tolerancePixels = value;
    emit changed();
}

void CanvasSnapModel::setGridSpacing(qreal value)
{
    if (!finite(value) || value < 0 || m_gridSpacing == value) return;
    m_gridSpacing = value;
    emit changed();
}

void CanvasSnapModel::setGridOrigin(QPointF origin)
{
    if (!finite(origin.x()) || !finite(origin.y()) || m_gridOrigin == origin) return;
    m_gridOrigin = origin;
    emit changed();
}

void CanvasSnapModel::setGuides(const QVariantList &guides)
{
    if (m_guides == guides) return;
    m_guides = guides;
    emit changed();
}

QVariantMap CanvasSnapModel::snapPoint(QPointF point,
                                      const QVariantList &excludedObjectIds) const
{
    const auto untouched = [&]() {
        return QVariantMap{{QStringLiteral("point"), point},
            {QStringLiteral("delta"), QPointF()}, {QStringLiteral("snapped"), false},
            {QStringLiteral("x"), QVariantMap{}}, {QStringLiteral("y"), QVariantMap{}}};
    };
    if (!m_enabled || !finite(point.x()) || !finite(point.y())
        || !m_viewState || !finite(m_viewState->zoom()) || m_viewState->zoom() <= 0
        || m_tolerancePixels <= 0) return untouched();

    const qreal zoom = m_viewState->zoom();
    const qreal tolerance = m_tolerancePixels;
    AxisCandidate x, y;

    // AGENT-GUARD: priority is explicit and stable: visible guide (300),
    // visible object geometry (200), grid (100). Distance and identity break
    // ties inside one source class. Re-evaluate from the raw drag point on
    // every query; retaining a previous snapped point creates a jump when a
    // user disables snapping mid-gesture.
    for (const QVariant &entry : m_guides) {
        const QVariantMap guide = entry.toMap();
        if (guide.value(QStringLiteral("visible"), true).toBool() == false) continue;
        const QString id = guide.value(QStringLiteral("id")).toString();
        const QString orientation = guide.value(QStringLiteral("orientation")).toString();
        bool valid = false;
        const qreal position = guide.value(QStringLiteral("position")).toDouble(&valid);
        if (id.isEmpty() || !valid || !finite(position)) continue;
        if (orientation == QStringLiteral("vertical"))
            offer(x, point.x(), position, zoom, tolerance, 300,
                  QStringLiteral("guide"), id, orientation);
        else if (orientation == QStringLiteral("horizontal"))
            offer(y, point.y(), position, zoom, tolerance, 300,
                  QStringLiteral("guide"), id, orientation);
    }

    if (m_document) {
        QSet<CanvasObjectId> excluded;
        for (const QVariant &entry : excludedObjectIds) {
            bool valid = false;
            const quint64 id = entry.toString().toULongLong(&valid);
            if (valid) excluded.insert(id);
        }
        const qreal documentTolerance = tolerance / zoom;
        if (finite(documentTolerance) && documentTolerance > 0) {
            const QRectF region(point.x() - documentTolerance,
                                point.y() - documentTolerance,
                                2 * documentTolerance, 2 * documentTolerance);
            for (CanvasObjectId id : m_document->candidates(region)) {
                if (excluded.contains(id)) continue;
                const CanvasObject *object = m_document->object(id);
                if (!object || !m_document->effectiveVisible(*object)
                    || !CanvasGeometry::isRenderable(*object)) continue;
                const QString targetId = QString::number(id);
                const QPolygonF outline = CanvasGeometry::documentOutline(*object);
                for (qsizetype i = 0; i < outline.size(); ++i) {
                    offerPoint(x, y, point, outline.at(i), zoom, tolerance,
                               targetId, QStringLiteral("vertex:%1").arg(i));
                    if (i > 0)
                        offerEdgeProjection(
                            x, y, point,
                            nearestPointOnSegment(point, outline.at(i - 1), outline.at(i)),
                            zoom, tolerance, targetId,
                            QStringLiteral("edge:%1").arg(i-1));
                }
                if (CanvasGeometry::isClosed(*object) && outline.size() > 1)
                    offerEdgeProjection(
                        x, y, point,
                        nearestPointOnSegment(point, outline.last(), outline.first()),
                        zoom, tolerance, targetId, QStringLiteral("edge:closing"));
                offerPoint(x, y, point, CanvasGeometry::documentBounds(*object).center(),
                           zoom, tolerance, targetId, QStringLiteral("center"));
            }
        }
    }

    if (m_gridSpacing > 0 && finite(m_gridSpacing)) {
        const auto offerGrid = [&](AxisCandidate &best, qreal input, qreal origin) {
            const qreal multiple = std::round((input - origin) / m_gridSpacing);
            const qreal position = origin + multiple * m_gridSpacing;
            offer(best, input, position, zoom, tolerance, 100,
                  QStringLiteral("grid"), QString{}, QStringLiteral("line"));
        };
        offerGrid(x, point.x(), m_gridOrigin.x());
        offerGrid(y, point.y(), m_gridOrigin.y());
    }

    const QPointF snapped(x.priority >= 0 ? x.position : point.x(),
                          y.priority >= 0 ? y.position : point.y());
    return {{QStringLiteral("point"), snapped},
            {QStringLiteral("delta"), snapped - point},
            {QStringLiteral("snapped"), x.priority >= 0 || y.priority >= 0},
            {QStringLiteral("x"), provenance(x)},
            {QStringLiteral("y"), provenance(y)}};
}

} // namespace QindaTK
