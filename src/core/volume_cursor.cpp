// SPDX-License-Identifier: LGPL-3.0-or-later
#include "volume_cursor.h"
#include "voxel_volume.h"
#include <algorithm>
#include <cmath>

namespace QindaTK {
VolumeCursor::VolumeCursor(QObject *parent) : QObject(parent) {}
void VolumeCursor::setVolume(VoxelVolume *volume)
{
    if (m_volume == volume) return;
    if (m_volume) disconnect(m_volume, nullptr, this, nullptr);
    m_volume = volume;
    if (volume) {
        m_point[0] = (volume->sizeX() - 1) / 2.0;
        m_point[1] = (volume->sizeY() - 1) / 2.0;
        m_point[2] = (volume->sizeZ() - 1) / 2.0;
        connect(volume, &VoxelVolume::changed, this, [this] {
            if (!m_volume) return;
            setVoxelPoint(m_point[0], m_point[1], m_point[2]);
            emit changed();
        });
    }
    emit changed();
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
    if (m_point[0] == next[0] && m_point[1] == next[1] && m_point[2] == next[2]) return true;
    std::copy(next, next + 3, m_point);
    emit changed();
    return true;
}
bool VolumeCursor::setWorldPoint(double x, double y, double z)
{
    if (!m_volume) return false;
    const auto point = m_volume->voxelFromWorld(x, y, z);
    return setVoxelPoint(point[0].toDouble(), point[1].toDouble(), point[2].toDouble());
}
} // namespace QindaTK
