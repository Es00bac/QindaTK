// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "color_engine.h"

namespace QindaTK {

enum class GradientInterpolation { LinearSRgb };
enum class GradientAlphaPolicy { Premultiplied };

struct GradientStop {
    QString id;
    double position = 0.0;
    ColorValue color; // straight alpha, encoding must match profile
    ColorProfile profile;
};

// AGENT-CONTRACT: docs/gradient.md defines array order as the tie-breaker
// for duplicate positions. Hosts preserve stable IDs and order on editing.
struct GradientDefinition {
    QString id;
    QVector<GradientStop> stops;
    GradientInterpolation interpolation = GradientInterpolation::LinearSRgb;
    GradientAlphaPolicy alphaPolicy = GradientAlphaPolicy::Premultiplied;
};

struct GradientEvaluation {
    std::optional<ColorValue> color;
    OperationError error;
    ColorPrecision precision = ColorPrecision::Float32;
    ColorGamutStatus gamut = ColorGamutStatus::Unknown;
    bool outsideNominalRange = false;
};

// Stateless numerical evaluator; the caller owns the ColorEngine cache.
// Fixed policy: relative colorimetric, BPC off, no proofing, padded edges.
// Only the selected stop(s) are transformed; validate() checks all metadata.
class GradientEvaluator {
public:
    static constexpr qsizetype MaxStops = 4096;
    static OperationError validate(const GradientDefinition &gradient);
    static GradientEvaluation evaluate(const GradientDefinition &gradient, double position,
                                       const ColorProfile &target, ColorEngine &engine);
};

} // namespace QindaTK
