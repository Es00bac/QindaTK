// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "part_whole_model.h"
#include <QRectF>

namespace QindaTK {
struct PartGeometry {
    int node = -1;
    QRectF rect;
    double angleStart = 0; // clockwise degrees from twelve o'clock
    double angleSpan = 0;
    double innerRadius = 0; // fractions of the plot radius
    double outerRadius = 1;
    bool sector = false;
};
class PartWholeLayout {
public:
    enum Mode { Pie, Donut, Treemap, Sunburst };
    // Geometry is normalized. Missing and zero nodes remain in the model/table
    // but have no area or angle. Parent sums are validated by the model.
    static QVector<PartGeometry> build(const PartWholeModel &model, Mode mode);
};
} // namespace QindaTK
