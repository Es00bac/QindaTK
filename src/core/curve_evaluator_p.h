// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "curve_evaluator.h"

namespace QindaTK::CurveDetail {

OperationError error(ErrorCode code, const QString &field, const QString &message);
QPointF outgoing(const CurveKnot &left, const CurveKnot &right);
QPointF incoming(const CurveKnot &left, const CurveKnot &right);
std::optional<double> edgeSlope(const CurveKnot &left, const CurveKnot &right, bool atStart);

} // namespace QindaTK::CurveDetail
