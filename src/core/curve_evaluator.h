// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include <QPointF>

namespace QindaTK {

enum class CurveInterpolation { Step, Linear, CubicBezier };
enum class CurveExtrapolation { Hold, Linear, Repeat };

struct CurveAxisDomain {
    UnitDimension dimension = UnitDimension::Scalar;
    QString unitId = QStringLiteral("normalized");
    double minimum = 0.0;
    double maximum = 1.0;
};

// AGENT-CONTRACT: CurveEditor uses the same stable string IDs and absolute
// normalized x/y handles (docs/curve.md). No coordinate is a tangent offset.
struct CurveKnot {
    QString id;
    double x = 0.0;
    double y = 0.0;
    std::optional<QPointF> inHandle;
    std::optional<QPointF> outHandle;
    CurveInterpolation interpolation = CurveInterpolation::Linear; // outgoing segment
};

struct CurveDefinition {
    QString id;
    CurveAxisDomain xDomain;
    CurveAxisDomain yDomain;
    QVector<CurveKnot> knots;
    CurveExtrapolation before = CurveExtrapolation::Hold;
    CurveExtrapolation after = CurveExtrapolation::Hold;
};

struct CurveSample {
    double normalizedX = 0.0; // original query, before repeat wrapping
    double normalizedY = 0.0; // may leave [0,1] under linear extrapolation
    UnitQuantity value;
};
struct CurveEvaluation {
    std::optional<CurveSample> sample;
    OperationError error;
};

// Stateless, thread-safe for immutable inputs, double-precision arithmetic.
// Every operation validates the whole definition; no host edits or repairs.
class CurveEvaluator {
public:
    static constexpr qsizetype MaxKnots = 65536;
    static constexpr double MinimumXSpacing = 1e-12;
    static OperationError validate(const CurveDefinition &curve);
    static CurveEvaluation evaluate(const CurveDefinition &curve, const UnitQuantity &x);
    static CurveEvaluation evaluateNormalized(const CurveDefinition &curve, double x);
};

} // namespace QindaTK
