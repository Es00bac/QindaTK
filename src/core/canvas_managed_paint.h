// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "gradient_editor_model.h"
#include <QBrush>
#include <QByteArray>
#include <QRectF>

namespace QindaTK::CanvasManagedPaint {
// The payload is an owned, bounded native object value. ICC bytes travel with
// each typed fill so a reopened document never depends on a process registry.
QByteArray solid(const ColorValue &value, const ColorProfile &profile, OperationError *error = nullptr);
QByteArray gradient(const GradientDefinition &value, OperationError *error = nullptr);
OperationError validate(const QByteArray &payload);
QBrush brush(const QByteArray &payload, const QRectF &bounds, ColorEngine &engine,
             OperationError *error = nullptr);
}
