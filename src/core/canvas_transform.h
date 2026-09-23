// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>
#include <optional>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#coordinate-spaces): three spaces, one
// direction of truth.
//   document  -- the model's coordinates; never changed by navigation.
//   view      -- logical item pixels (what Qt input events report).
//   device    -- physical pixels = view * devicePixelRatio.
// A view is fully described by CanvasViewPose {center, zoom, rotation}: the
// document point `center` sits at the middle of the view, one document unit
// is `zoom` view pixels, and the document is turned `rotation` degrees
// clockwise on screen. Two views that share a pose show the same document
// region regardless of their own sizes, which is what synchronization means.
struct CanvasViewPose {
    QPointF center;
    qreal zoom = 1.0;
    qreal rotation = 0.0; // degrees, normalized to (-180, 180]

    friend bool operator==(const CanvasViewPose &, const CanvasViewPose &) = default;
};

struct CanvasZoomLimits {
    qreal minimum = 0.01;
    qreal maximum = 256.0;
};

namespace CanvasTransform {

// AGENT-GUARD: a matrix is usable only when every element is finite and
// |det| exceeds kSingularEpsilon relative to its scale. Callers must use
// invert() rather than QTransform::inverted(), which silently returns the
// identity for a singular matrix and would map every pointer to the origin.
inline constexpr qreal kSingularEpsilon = 1e-12;

[[nodiscard]] bool isFinite(const QTransform &matrix);
[[nodiscard]] bool isInvertible(const QTransform &matrix);
[[nodiscard]] std::optional<QTransform> invert(const QTransform &matrix);

[[nodiscard]] qreal normalizeRotation(qreal degrees);
[[nodiscard]] qreal clampZoom(qreal zoom, const CanvasZoomLimits &limits);
[[nodiscard]] CanvasViewPose sanitized(CanvasViewPose pose, const CanvasZoomLimits &limits);

// document -> view:  T(viewSize/2) * R(rotation) * S(zoom) * T(-center)
[[nodiscard]] QTransform documentToView(const CanvasViewPose &pose, const QSizeF &viewSize);
// view -> document: the exact algebraic inverse (never singular for a
// sanitized pose because zoom is clamped strictly above zero).
[[nodiscard]] QTransform viewToDocument(const CanvasViewPose &pose, const QSizeF &viewSize);
[[nodiscard]] QTransform viewToDevice(qreal devicePixelRatio);

[[nodiscard]] QPointF mapToView(const CanvasViewPose &pose, const QSizeF &viewSize, QPointF doc);
[[nodiscard]] QPointF mapToDocument(const CanvasViewPose &pose, const QSizeF &viewSize, QPointF view);

// The view rectangle expressed in document space: a rotated quad in general.
[[nodiscard]] QPolygonF visibleDocumentPolygon(const CanvasViewPose &pose, const QSizeF &viewSize);
[[nodiscard]] QRectF visibleDocumentBounds(const CanvasViewPose &pose, const QSizeF &viewSize);

// Navigation. Each returns a new pose; the document is never touched.
// zoomAbout keeps the document point under `viewAnchor` fixed on screen.
[[nodiscard]] CanvasViewPose zoomAbout(const CanvasViewPose &pose, const QSizeF &viewSize,
                                       QPointF viewAnchor, qreal factor,
                                       const CanvasZoomLimits &limits);
// rotateAbout keeps the document point under `viewAnchor` fixed on screen.
[[nodiscard]] CanvasViewPose rotateAbout(const CanvasViewPose &pose, const QSizeF &viewSize,
                                         QPointF viewAnchor, qreal degrees);
// panBy moves the content by `viewDelta` pixels on screen.
[[nodiscard]] CanvasViewPose panBy(const CanvasViewPose &pose, QPointF viewDelta);
// Fits `documentRect` (as rotated by the pose's rotation) inside the view,
// inset by `padding` view pixels on every side.
[[nodiscard]] CanvasViewPose fit(const CanvasViewPose &pose, const QSizeF &viewSize,
                                 const QRectF &documentRect, qreal padding,
                                 const CanvasZoomLimits &limits);

} // namespace CanvasTransform
} // namespace QindaTK
