// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "color_context.h"

#include <QImage>
#include <QPointF>

namespace QindaTK {

// ColorWheels x/y live in the normalized, host-declared opponent wheel space
// documented in docs/color.md. No hidden QColor transfer or luma linkage.
struct ColorGradeParameters {
    double exposureEv = 0.0;
    QPointF lift;
    QPointF gamma;
    QPointF gain;
    bool isValid() const;
    static std::optional<ColorGradeParameters> fromVariantMap(const QVariantMap &map,
                                                               OperationError *error = nullptr);
};

struct ColorGradeResult {
    QImage image;
    OperationError error;
    quint64 generation = 0;
    explicit operator bool() const { return !image.isNull() && !error; }
};

// CPU RGB image transform. Input must have a valid ICC tag. Math runs in
// linear sRGB; output is converted and tagged in ColorContextSnapshot::output.
class ColorGradeProcessor {
public:
    ColorGradeResult process(const QImage &source, const ColorGradeParameters &parameters,
                             const ColorContextSnapshot &context) const;
};

} // namespace QindaTK
