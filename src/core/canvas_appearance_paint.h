// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_object.h"

#include <QBrush>
#include <QPainter>
#include <functional>

namespace QindaTK::CanvasAppearancePaint {

// Fill colour, pattern ink and stroke (any alignment) of a vector object.
// `painter` maps document units to device pixels at `scale` px per unit.
void paintVector(QPainter *painter, const CanvasObject &object, const QBrush &fillBrush,
                 qreal scale);

// Paints the object's content into an offscreen layer that covers
// `documentRect` plus the effect reach, applies the enabled effects in stack
// order, then composites the layer with the object's opacity and blend mode
// (D-107). `content` receives the layer painter, the document rectangle the
// layer covers and the layer's pixels per document unit.
using LayerContent = std::function<void(QPainter *, const QRectF &, qreal)>;
void paintLayered(QPainter *painter, const CanvasObject &object, const QRectF &documentRect,
                  qreal scale, const LayerContent &content);

} // namespace QindaTK::CanvasAppearancePaint
