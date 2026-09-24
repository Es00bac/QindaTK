// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "color_profile.h"

namespace QindaTK {

enum class ColorOperation { Working, Display, Output, ProofPreview };
enum class ColorIntent { Perceptual, RelativeColorimetric, Saturation, AbsoluteColorimetric };
enum class ColorPrecision { Float32 };
enum class ColorGamutStatus { Unknown };

// AGENT-CONTRACT: docs/color.md defines this value snapshot. Publish a new
// copy when settings change; workers never read a live GUI color context.
struct ColorContextSnapshot {
    ColorProfile document;
    ColorProfile working;
    ColorProfile display;
    ColorProfile output;
    ColorProfile proof;
    ColorIntent intent = ColorIntent::RelativeColorimetric;
    ColorIntent proofIntent = ColorIntent::RelativeColorimetric;
    bool blackPointCompensation = false;
    quint64 generation = 1;
};

struct ColorTransformRequest {
    ColorValue value;
    ColorProfile source;
    ColorContextSnapshot context;
    ColorOperation operation = ColorOperation::Working;
};

struct ColorTransformResult {
    std::optional<ColorValue> value;
    OperationError error;
    ColorOperation operation = ColorOperation::Working;
    quint64 generation = 0;
    ColorPrecision precision = ColorPrecision::Float32;
    ColorGamutStatus gamut = ColorGamutStatus::Unknown;
    bool outsideNominalRange = false;
    // No inferred gamut/clipping guarantee for arbitrary ICC LUTs.
};

} // namespace QindaTK
