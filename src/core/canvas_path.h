// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "canvas_object.h"
#include <QPainterPath>
#include <QPen>

namespace QindaTK::CanvasPath {
[[nodiscard]] QPainterPath local(const CanvasObject &object);
[[nodiscard]] QPainterPath document(const CanvasObject &object);
[[nodiscard]] QPen pen(const CanvasObject &object, qreal minimumWidth = 0);
} // namespace QindaTK::CanvasPath
