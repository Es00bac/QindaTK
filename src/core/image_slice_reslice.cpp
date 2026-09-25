// SPDX-License-Identifier: LGPL-3.0-or-later
// Oblique (world-space) reslicing and slice navigation for ImageSliceItem.
#include "image_slice_item.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
bool listToTriple(const QVariantList &list, double out[3])
{
    if (list.size() != 3) return false;
    for (int i = 0; i < 3; ++i) out[i] = list[i].toDouble();
    return true;
}
double dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
} // namespace

QVariantList ImageSliceItem::resliceOrientation() const
{
    QVariantList result;
    for (double value : m_reslice) result.append(value);
    return result;
}
bool ImageSliceItem::parseOrientation(const QVariantList &matrix, double m[9])
{
    bool ok = matrix.size() == 9;
    for (int i = 0; ok && i < 9; ++i) {
        m[i] = matrix[i].toDouble(&ok);
        ok = ok && std::isfinite(m[i]);
    }
    // Same tolerance as VoxelVolume orientations: columns orthonormal.
    for (int a = 0; ok && a < 3; ++a)
        for (int b = a; ok && b < 3; ++b) {
            double product = 0;
            for (int row = 0; row < 3; ++row) product += m[row * 3 + a] * m[row * 3 + b];
            ok = std::abs(product - (a == b ? 1.0 : 0.0)) <= 1e-5;
        }
    return ok;
}
void ImageSliceItem::setResliceOrientation(const QVariantList &matrix)
{
    double m[9];
    if (!parseOrientation(matrix, m)) {
        m_error = QStringLiteral("Reslice orientation must be an orthonormal 3×3 matrix");
        emit changed();
        return;
    }
    const bool same = std::equal(m, m + 9, m_reslice);
    if (same && m_error.isEmpty()) return;
    std::copy(m, m + 9, m_reslice);
    m_error.clear();
    invalidate(); emit changed(); emit sliceChanged();
}

ImageSliceItem::ObliqueFrame ImageSliceItem::obliqueFrame() const
{
    ObliqueFrame f;
    if (!m_volume || m_volume->sizeX() < 1) return f;
    const int shape[3] = {m_volume->sizeX(), m_volume->sizeY(), m_volume->sizeZ()};
    double diagonal = 0;
    f.step = std::numeric_limits<double>::max();
    for (int axis = 0; axis < 3; ++axis) {
        const double extent = shape[axis] * m_volume->spacingAt(axis);
        diagonal += extent * extent;
        f.step = std::min(f.step, m_volume->spacingAt(axis));
        f.u[axis] = m_reslice[axis * 3 + 0];
        f.v[axis] = m_reslice[axis * 3 + 1];
        f.w[axis] = m_reslice[axis * 3 + 2];
    }
    f.side = std::sqrt(diagonal);
    double volumeCentre[3], cursorWorld[3];
    listToTriple(m_volume->worldFromVoxel((shape[0] - 1) / 2.0, (shape[1] - 1) / 2.0,
                                          (shape[2] - 1) / 2.0), volumeCentre);
    if (m_cursor && m_cursorLinked && listToTriple(m_cursor->worldPoint(), cursorWorld)) {
        const double delta[3] = {cursorWorld[0] - volumeCentre[0], cursorWorld[1] - volumeCentre[1],
                                 cursorWorld[2] - volumeCentre[2]};
        f.offset = dot(delta, f.w);
    }
    // AGENT-NOTE: the plane contains the cursor; its image centre is the
    // volume centre moved along the normal, so in-plane cursor moves never
    // pan the image and a slice depends on `offset` alone (paint caches on it).
    for (int axis = 0; axis < 3; ++axis) f.centre[axis] = volumeCentre[axis] + f.offset * f.w[axis];
    double base[3], alongU[3], alongV[3];
    listToTriple(m_volume->voxelFromWorld(f.centre[0], f.centre[1], f.centre[2]), base);
    listToTriple(m_volume->voxelFromWorld(f.centre[0] + f.u[0], f.centre[1] + f.u[1], f.centre[2] + f.u[2]), alongU);
    listToTriple(m_volume->voxelFromWorld(f.centre[0] + f.v[0], f.centre[1] + f.v[1], f.centre[2] + f.v[2]), alongV);
    for (int axis = 0; axis < 3; ++axis) {
        f.voxelCentre[axis] = base[axis];
        f.voxelU[axis] = alongU[axis] - base[axis];
        f.voxelV[axis] = alongV[axis] - base[axis];
    }
    f.valid = f.side > 0;
    return f;
}
QVariantList ImageSliceItem::obliqueVoxelAt(const QRectF &rect, double px, double py) const
{
    const ObliqueFrame f = obliqueFrame();
    if (!f.valid) return {};
    const double h = ((px - rect.left()) / rect.width() - 0.5) * f.side;
    const double v = ((py - rect.top()) / rect.height() - 0.5) * f.side;
    return {f.voxelCentre[0] + h * f.voxelU[0] + v * f.voxelV[0],
            f.voxelCentre[1] + h * f.voxelU[1] + v * f.voxelV[1],
            f.voxelCentre[2] + h * f.voxelU[2] + v * f.voxelV[2]};
}
QVariantList ImageSliceItem::obliquePosition(const QRectF &rect, double x, double y, double z) const
{
    const ObliqueFrame f = obliqueFrame();
    double world[3];
    if (!f.valid || !listToTriple(m_volume->worldFromVoxel(x, y, z), world)) return {};
    const double delta[3] = {world[0] - f.centre[0], world[1] - f.centre[1], world[2] - f.centre[2]};
    return {rect.left() + (dot(delta, f.u) / f.side + 0.5) * rect.width(),
            rect.top() + (dot(delta, f.v) / f.side + 0.5) * rect.height()};
}
QImage ImageSliceItem::obliqueImage(int columns, int rows) const
{
    const ObliqueFrame f = obliqueFrame();
    if (!f.valid) return {};
    columns = std::clamp(columns, 1, MaximumImageSide);
    rows = std::clamp(rows, 1, MaximumImageSide);
    QImage image(columns, rows, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    for (int row = 0; row < rows; ++row) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(row));
        const double v = ((row + 0.5) / rows - 0.5) * f.side;
        for (int column = 0; column < columns; ++column) {
            const double h = ((column + 0.5) / columns - 0.5) * f.side;
            line[column] = qPremultiply(shadeAt(f.voxelCentre[0] + h * f.voxelU[0] + v * f.voxelV[0],
                                                f.voxelCentre[1] + h * f.voxelU[1] + v * f.voxelV[1],
                                                f.voxelCentre[2] + h * f.voxelU[2] + v * f.voxelV[2]));
        }
    }
    return image;
}

int ImageSliceItem::sliceCount() const
{
    if (!m_volume || m_volume->sizeX() < 1) return 0;
    if (m_plane == Oblique) {
        const ObliqueFrame f = obliqueFrame();
        return int(std::floor(f.side / f.step)) + 1;
    }
    return m_plane == Axial ? m_volume->sizeZ() : m_plane == Coronal ? m_volume->sizeY() : m_volume->sizeX();
}
int ImageSliceItem::sliceIndex() const
{
    const int count = sliceCount();
    if (count < 1) return 0;
    if (m_plane == Oblique) {
        const ObliqueFrame f = obliqueFrame();
        return std::clamp(int(std::lround((f.offset + f.side / 2) / f.step)), 0, count - 1);
    }
    return std::clamp(int(std::lround(currentSlice())), 0, count - 1);
}
bool ImageSliceItem::moveCursorWorld(const double delta[3])
{
    double world[3];
    if (!m_cursor || !m_cursorLinked || !listToTriple(m_cursor->worldPoint(), world)) return false;
    const double before[3] = {m_cursor->x(), m_cursor->y(), m_cursor->z()};
    if (!m_cursor->setWorldPoint(world[0] + delta[0], world[1] + delta[1], world[2] + delta[2]))
        return false;
    return m_cursor->x() != before[0] || m_cursor->y() != before[1] || m_cursor->z() != before[2];
}
bool ImageSliceItem::stepSlice(int steps)
{
    if (!m_cursor || !m_cursorLinked || !m_volume || steps == 0) return false;
    if (m_plane == Oblique) {
        const ObliqueFrame f = obliqueFrame();
        if (!f.valid) return false;
        const double delta[3] = {steps * f.step * f.w[0], steps * f.step * f.w[1], steps * f.step * f.w[2]};
        return moveCursorWorld(delta);
    }
    double point[3] = {m_cursor->x(), m_cursor->y(), m_cursor->z()};
    const int axis = m_plane == Axial ? 2 : m_plane == Coronal ? 1 : 0;
    const double before = point[axis];
    point[axis] = std::round(point[axis]) + steps;
    if (!m_cursor->setVoxelPoint(point[0], point[1], point[2])) return false;
    const double after = axis == 0 ? m_cursor->x() : axis == 1 ? m_cursor->y() : m_cursor->z();
    return after != before;
}
bool ImageSliceItem::moveCrosshair(int dh, int dv)
{
    if (!m_cursor || !m_cursorLinked || !m_volume || (dh == 0 && dv == 0)) return false;
    if (m_plane == Oblique) {
        const ObliqueFrame f = obliqueFrame();
        if (!f.valid) return false;
        double delta[3];
        for (int axis = 0; axis < 3; ++axis) delta[axis] = (dh * f.u[axis] + dv * f.v[axis]) * f.step;
        return moveCursorWorld(delta);
    }
    const double x = std::round(m_cursor->x()), y = std::round(m_cursor->y()), z = std::round(m_cursor->z());
    if (m_plane == Axial) return m_cursor->setVoxelPoint(x + dh, y + dv, z);
    if (m_plane == Coronal) return m_cursor->setVoxelPoint(x + dh, y, z + dv);
    return m_cursor->setVoxelPoint(x, y + dh, z + dv);
}
} // namespace QindaTK
