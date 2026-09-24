// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "canvas_object.h"

#include <QPainterPath>
#include <optional>

namespace QindaTK::CanvasPathBoolean {

// AGENT-CONTRACT (docs/vector-authoring.md#path-booleans, D-108): union,
// intersect, subtract (backmost minus all others) and exclude run Qt's path
// clipper on operands flattened to straight segments within a tolerance of
// 1e-4 of their combined extent, honouring each operand's fill rule and
// resolving self-intersections; results are straight-segment contours.
// Combine merges every contour unchanged (curves kept) into one compound path.
enum class Operation : quint8 { Union, Intersect, Subtract, Exclude, Combine };
[[nodiscard]] std::optional<Operation> operation(const QString &name);

// Rectangles, ellipses and paths whose every contour is closed.
[[nodiscard]] bool eligible(const CanvasObject &object);
// Document-space fill area of an eligible object, with its fill rule.
[[nodiscard]] QPainterPath area(const CanvasObject &object);
// Straight-segment copy of `path`: each cubic is halved until both of its
// controls lie within `tolerance` of the chord, so the curve does too.
[[nodiscard]] QPainterPath flattened(const QPainterPath &path, qreal tolerance);

struct Result {
    QVector<CanvasPathContour> contours; // in the local space of `target`
    Qt::FillRule fillRule = Qt::OddEvenFill;
};
// Combines `operands` (paint order, backmost first) and expresses the result
// in the local space of `target`, the transform the result object keeps.
[[nodiscard]] std::optional<Result> combine(const QVector<const CanvasObject *> &operands,
                                            Operation operation, const QTransform &target,
                                            OperationError *error = nullptr);

} // namespace QindaTK::CanvasPathBoolean
