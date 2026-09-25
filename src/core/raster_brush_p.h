// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QRgb>
#include <QtGlobal>

namespace QindaTK::RasterBrush {

// AGENT-CONTRACT: the one brush kernel shared by TiledRasterDocument (2D
// canvas strokes, WP-045) and MeshRigDocument (texture paint on a 3D surface,
// WP-077). Both call these functions, so a dab of the same colour, opacity and
// hardness writes the same premultiplied sRGB bytes in either editor. Changing
// either formula changes both documents' saved pixels; update the exact-pixel
// tests in tst_tiled_raster_document and tst_mesh_rig_paint together.

// Coverage of a round dab at `distance` (0 = centre, 1 = rim; callers skip
// distance >= 1). Hardness 1 is a flat disk; lower values ramp the rim.
inline qreal edgeCoverage(qreal distance, qreal hardness)
{
    return distance <= hardness || hardness >= 1
        ? 1.0 : (1.0 - distance) / qMax<qreal>(0.0001, 1.0 - hardness);
}

// Source-over of a straight-alpha colour at coverage `alpha` onto one
// premultiplied ARGB32 pixel.
inline QRgb blendPremultiplied(QRgb previous, const QColor &source, qreal alpha)
{
    const qreal inverse = 1.0 - alpha;
    const int a = qRound(alpha * 255 + qAlpha(previous) * inverse);
    const int red = qRound(source.redF() * alpha * 255 + qRed(previous) * inverse);
    const int green = qRound(source.greenF() * alpha * 255 + qGreen(previous) * inverse);
    const int blue = qRound(source.blueF() * alpha * 255 + qBlue(previous) * inverse);
    return qRgba(qMin(red, a), qMin(green, a), qMin(blue, a), a);
}

} // namespace QindaTK::RasterBrush
