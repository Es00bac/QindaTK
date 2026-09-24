// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "canvas_object.h"
#include <QPainterPath>
#include <QPen>

namespace QindaTK::CanvasPath {
[[nodiscard]] QPainterPath local(const CanvasObject &object);
[[nodiscard]] QPainterPath document(const CanvasObject &object);
[[nodiscard]] QPen pen(const CanvasObject &object, qreal minimumWidth = 0);
// Local-space contours of any vector object: a path's own contours, a
// rectangle's four corners (clockwise from top-left), an ellipse's four cubic
// quarter arcs (from its rightmost point, like documentOutline) or an open
// polyline. Image objects have none.
[[nodiscard]] QVector<CanvasPathContour> contours(const CanvasObject &object);
} // namespace QindaTK::CanvasPath
