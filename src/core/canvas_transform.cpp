// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_transform.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace QindaTK::CanvasTransform {

bool isFinite(const QTransform &m)
{
    const qreal values[] = {m.m11(), m.m12(), m.m13(), m.m21(), m.m22(),
                            m.m23(), m.m31(), m.m32(), m.m33()};
    return std::all_of(std::begin(values), std::end(values),
                       [](qreal v) { return std::isfinite(v); });
}

bool isInvertible(const QTransform &m)
{
    if (!isFinite(m)) {
        return false;
    }
    // Scale the threshold by the magnitude of the linear part so a matrix of
    // tiny-but-valid scales is not mistaken for a singular one.
    const qreal scale = std::max({std::abs(m.m11()), std::abs(m.m12()),
                                  std::abs(m.m21()), std::abs(m.m22()), 1e-300});
    const qreal det = m.determinant();
    return std::isfinite(det) && std::abs(det) > kSingularEpsilon * scale * scale;
}

std::optional<QTransform> invert(const QTransform &m)
{
    if (!isInvertible(m)) {
        return std::nullopt;
    }
    bool ok = false;
    const QTransform inverse = m.inverted(&ok);
    if (!ok || !isFinite(inverse)) {
        return std::nullopt;
    }
    return inverse;
}

qreal normalizeRotation(qreal degrees)
{
    if (!std::isfinite(degrees)) {
        return 0.0;
    }
    qreal r = std::fmod(degrees, 360.0);
    if (r <= -180.0) {
        r += 360.0;
    } else if (r > 180.0) {
        r -= 360.0;
    }
    return r;
}

qreal clampZoom(qreal zoom, const CanvasZoomLimits &limits)
{
    const qreal lo = std::max(limits.minimum, 1e-6);
    const qreal hi = std::max(limits.maximum, lo);
    if (!std::isfinite(zoom) || zoom <= 0.0) {
        return std::clamp(1.0, lo, hi);
    }
    return std::clamp(zoom, lo, hi);
}

CanvasViewPose sanitized(CanvasViewPose pose, const CanvasZoomLimits &limits)
{
    pose.zoom = clampZoom(pose.zoom, limits);
    pose.rotation = normalizeRotation(pose.rotation);
    if (!std::isfinite(pose.center.x()) || !std::isfinite(pose.center.y())) {
        pose.center = {};
    }
    return pose;
}

QTransform documentToView(const CanvasViewPose &pose, const QSizeF &viewSize)
{
    QTransform t;
    t.translate(viewSize.width() / 2.0, viewSize.height() / 2.0);
    t.rotate(pose.rotation);
    t.scale(pose.zoom, pose.zoom);
    t.translate(-pose.center.x(), -pose.center.y());
    return t;
}

QTransform viewToDocument(const CanvasViewPose &pose, const QSizeF &viewSize)
{
    // Built from the inverse factors rather than by inverting a matrix, so
    // the round trip error stays at one rounding per factor.
    QTransform t;
    t.translate(pose.center.x(), pose.center.y());
    t.scale(1.0 / pose.zoom, 1.0 / pose.zoom);
    t.rotate(-pose.rotation);
    t.translate(-viewSize.width() / 2.0, -viewSize.height() / 2.0);
    return t;
}

QTransform viewToDevice(qreal devicePixelRatio)
{
    const qreal dpr = std::isfinite(devicePixelRatio) && devicePixelRatio > 0 ? devicePixelRatio : 1.0;
    return QTransform::fromScale(dpr, dpr);
}

QPointF mapToView(const CanvasViewPose &pose, const QSizeF &viewSize, QPointF doc)
{
    return documentToView(pose, viewSize).map(doc);
}

QPointF mapToDocument(const CanvasViewPose &pose, const QSizeF &viewSize, QPointF view)
{
    return viewToDocument(pose, viewSize).map(view);
}

QPolygonF visibleDocumentPolygon(const CanvasViewPose &pose, const QSizeF &viewSize)
{
    const QTransform inverse = viewToDocument(pose, viewSize);
    QPolygonF quad;
    quad << inverse.map(QPointF(0, 0)) << inverse.map(QPointF(viewSize.width(), 0))
         << inverse.map(QPointF(viewSize.width(), viewSize.height()))
         << inverse.map(QPointF(0, viewSize.height()));
    return quad;
}

QRectF visibleDocumentBounds(const CanvasViewPose &pose, const QSizeF &viewSize)
{
    return visibleDocumentPolygon(pose, viewSize).boundingRect();
}

CanvasViewPose zoomAbout(const CanvasViewPose &pose, const QSizeF &viewSize,
                         QPointF viewAnchor, qreal factor, const CanvasZoomLimits &limits)
{
    if (!std::isfinite(factor) || factor <= 0.0) {
        return pose;
    }
    const QPointF anchorDoc = mapToDocument(pose, viewSize, viewAnchor);
    CanvasViewPose next = pose;
    next.zoom = clampZoom(pose.zoom * factor, limits);
    // Solve for the centre that puts anchorDoc back under viewAnchor.
    const QPointF misplaced = mapToDocument(next, viewSize, viewAnchor);
    next.center += anchorDoc - misplaced;
    return next;
}

CanvasViewPose rotateAbout(const CanvasViewPose &pose, const QSizeF &viewSize,
                           QPointF viewAnchor, qreal degrees)
{
    if (!std::isfinite(degrees)) {
        return pose;
    }
    const QPointF anchorDoc = mapToDocument(pose, viewSize, viewAnchor);
    CanvasViewPose next = pose;
    next.rotation = normalizeRotation(pose.rotation + degrees);
    const QPointF misplaced = mapToDocument(next, viewSize, viewAnchor);
    next.center += anchorDoc - misplaced;
    return next;
}

CanvasViewPose panBy(const CanvasViewPose &pose, QPointF viewDelta)
{
    if (!std::isfinite(viewDelta.x()) || !std::isfinite(viewDelta.y())) {
        return pose;
    }
    // Content moves with the pointer, so the centre moves the other way,
    // expressed in document axes (undo rotation, undo zoom).
    QTransform linear;
    linear.scale(1.0 / pose.zoom, 1.0 / pose.zoom);
    linear.rotate(-pose.rotation);
    CanvasViewPose next = pose;
    next.center -= linear.map(viewDelta);
    return next;
}

CanvasViewPose fit(const CanvasViewPose &pose, const QSizeF &viewSize,
                   const QRectF &documentRect, qreal padding, const CanvasZoomLimits &limits)
{
    if (!documentRect.isValid() || viewSize.isEmpty()) {
        return pose;
    }
    QTransform turn;
    turn.rotate(pose.rotation);
    const QRectF turned = turn.mapRect(documentRect);
    const qreal availableW = std::max(1.0, viewSize.width() - 2.0 * padding);
    const qreal availableH = std::max(1.0, viewSize.height() - 2.0 * padding);
    CanvasViewPose next = pose;
    next.zoom = clampZoom(std::min(availableW / std::max(turned.width(), 1e-9),
                                   availableH / std::max(turned.height(), 1e-9)),
                          limits);
    next.center = documentRect.center();
    return next;
}

} // namespace QindaTK::CanvasTransform
