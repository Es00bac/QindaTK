// SPDX-License-Identifier: LGPL-3.0-or-later
#include "volume_cursor.h"
#include "voxel_volume.h"
#include <algorithm>
#include <cmath>

namespace QindaTK {
VolumeCursor::VolumeCursor(QObject *parent) : QObject(parent) {}
void VolumeCursor::recenter()
{
    const int shape[3] = {m_volume->sizeX(), m_volume->sizeY(), m_volume->sizeZ()};
    for (int axis = 0; axis < 3; ++axis) {
        m_shape[axis] = shape[axis];
        m_point[axis] = std::max(0, shape[axis] - 1) / 2.0;
    }
}
void VolumeCursor::publish()
{
    m_world = worldPoint();
    emit changed();
}
void VolumeCursor::setVolume(VoxelVolume *volume)
{
    if (m_volume == volume) return;
    if (m_volume) disconnect(m_volume, nullptr, this, nullptr);
    m_volume = volume;
    if (volume) {
        recenter();
        connect(volume, &VoxelVolume::changed, this, &VolumeCursor::volumeChanged);
    }
    publish();
}
void VolumeCursor::volumeChanged()
{
    if (!m_volume) return;
    // AGENT-NOTE: a new shape is a new dataset (typically a volume configured
    // after this cursor was bound to it), so the cursor recentres. The same
    // shape keeps the user's point. A voxel edit leaves the point and its
    // world position alone and must not notify every linked view again.
    const int shape[3] = {m_volume->sizeX(), m_volume->sizeY(), m_volume->sizeZ()};
    const bool reshaped = !std::equal(shape, shape + 3, m_shape);
    if (reshaped) recenter();
    if (reshaped || worldPoint() != m_world) publish();
}
QVariantList VolumeCursor::worldPoint() const
{
    return m_volume ? m_volume->worldFromVoxel(m_point[0], m_point[1], m_point[2]) : QVariantList{};
}
bool VolumeCursor::setVoxelPoint(double x, double y, double z)
{
    if (!m_volume || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
        || m_volume->sizeX() < 1 || m_volume->sizeY() < 1 || m_volume->sizeZ() < 1) return false;
    const double next[3] = {std::clamp(x, 0.0, double(m_volume->sizeX() - 1)),
                            std::clamp(y, 0.0, double(m_volume->sizeY() - 1)),
                            std::clamp(z, 0.0, double(m_volume->sizeZ() - 1))};
    if (std::equal(next, next + 3, m_point)) return true;
    std::copy(next, next + 3, m_point);
    publish();
    return true;
}
bool VolumeCursor::setWorldPoint(double x, double y, double z)
{
    if (!m_volume) return false;
    const auto point = m_volume->voxelFromWorld(x, y, z);
    return setVoxelPoint(point[0].toDouble(), point[1].toDouble(), point[2].toDouble());
}
} // namespace QindaTK
