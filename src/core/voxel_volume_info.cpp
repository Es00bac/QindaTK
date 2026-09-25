// SPDX-License-Identifier: LGPL-3.0-or-later
// VoxelVolume identity, geometry compatibility and histograms (D-260, EXT-07,
// EXT-09). Storage and sampling live in voxel_volume.cpp.
#include "voxel_volume.h"

#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace QindaTK {
namespace {
// Relative tolerance for spacing/origin/orientation equality: calibration
// written as decimal text round-trips within it.
constexpr double Tolerance = 1e-6;
bool near(double a, double b) { return std::abs(a - b) <= Tolerance * std::max({1.0, std::abs(a), std::abs(b)}); }
} // namespace

QString VoxelVolume::geometryMismatch(const VoxelVolume *other) const
{
    // AGENT-CONTRACT (docs/volume-imaging.md#linked-views-and-overlays, D-260):
    // ImageSliceItem overlays and linked cursors call this; an empty string is
    // the only "compatible" answer, and every sentence names the difference.
    if (!other) return QStringLiteral("No volume to compare");
    if (other == this) return {};
    if (sizeX() != other->sizeX() || sizeY() != other->sizeY() || sizeZ() != other->sizeZ())
        return QStringLiteral("Voxel grids differ (%1×%2×%3 vs %4×%5×%6)")
            .arg(sizeX()).arg(sizeY()).arg(sizeZ())
            .arg(other->sizeX()).arg(other->sizeY()).arg(other->sizeZ());
    if (m_unit != other->m_unit)
        return QStringLiteral("Spatial units differ (%1 vs %2)").arg(m_unit, other->m_unit);
    if (!m_frame.isEmpty() && !other->m_frame.isEmpty() && m_frame != other->m_frame)
        return QStringLiteral("Frames of reference differ (%1 vs %2)").arg(m_frame, other->m_frame);
    if (m_axisIds != other->m_axisIds)
        return QStringLiteral("Axis IDs differ (%1 vs %2)")
            .arg(m_axisIds.join(QLatin1Char(',')), other->m_axisIds.join(QLatin1Char(',')));
    for (int axis = 0; axis < 3; ++axis)
        if (!near(m_spacing[axis], other->m_spacing[axis]))
            return QStringLiteral("Voxel spacing differs on axis %1").arg(m_axisIds.value(axis));
    for (int axis = 0; axis < 3; ++axis)
        if (!near(m_origin[axis], other->m_origin[axis]))
            return QStringLiteral("Origins differ on axis %1").arg(m_axisIds.value(axis));
    for (int i = 0; i < 9; ++i)
        if (!near(m_orientation[i], other->m_orientation[i]))
            return QStringLiteral("Orientations differ");
    return {};
}

QVariantMap VoxelVolume::identity() const
{
    return {{QStringLiteral("datasetId"), m_datasetId},
            {QStringLiteral("frameOfReference"), m_frame},
            {QStringLiteral("axisIds"), m_axisIds},
            {QStringLiteral("unit"), m_unit},
            {QStringLiteral("valueUnit"), m_valueUnit},
            {QStringLiteral("voxelType"), m_type},
            {QStringLiteral("shape"), QVariantList{sizeX(), sizeY(), sizeZ()}},
            {QStringLiteral("channels"), m_channels},
            {QStringLiteral("frames"), m_frames},
            {QStringLiteral("revision"), QString::number(m_revision)}};
}

QString VoxelVolume::identityMismatch(const QVariantMap &identity) const
{
    // AGENT-NOTE: settings made for a dataset stay valid when it is reloaded
    // (a new revision, even a new resolution: views work in fractions and
    // world units), so only the identity and the addressed channel/frame
    // are checked. `revision` is recorded for provenance, never compared.
    if (sizeX() < 1) return QStringLiteral("No volume is loaded");
    const QString dataset = identity.value(QStringLiteral("datasetId")).toString();
    if (dataset != m_datasetId)
        return QStringLiteral("The settings belong to dataset “%1”, not “%2”")
            .arg(dataset, m_datasetId);
    const QString frame = identity.value(QStringLiteral("frameOfReference")).toString();
    if (frame != m_frame)
        return QStringLiteral("The settings use frame of reference “%1”, not “%2”")
            .arg(frame, m_frame);
    const QString unit = identity.value(QStringLiteral("unit"), m_unit).toString();
    if (unit != m_unit)
        return QStringLiteral("The settings use spatial unit %1, not %2").arg(unit, m_unit);
    return {};
}

QVariantMap VoxelVolume::histogram(int bins, double minimum, double maximum, int channel, int frame) const
{
    if (bins < 1 || bins > 4096 || !std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum
        || channel < 0 || channel >= m_channels || frame < 0 || frame >= m_frames || sizeX() < 1)
        return {};
    QVector<qint64> counts(bins, 0);
    qint64 below = 0, above = 0, missing = 0;
    const double scale = bins / (maximum - minimum);
    const auto count = [&](double value) {
        if (!std::isfinite(value)) ++missing;
        else if (value < minimum) ++below;
        else if (value > maximum) ++above;
        else ++counts[std::min(bins - 1, int((value - minimum) * scale))];
    };
    QMutexLocker lock(&m_lock);
    const int bytesPerValue = bytesPerVoxel();
    // AGENT-GUARD: chunk by chunk (like packTexture) so a provider volume
    // larger than maximumResidentBytes streams through the LRU cache once.
    for (int cz = 0; cz < chunkCount(2); ++cz)
        for (int cy = 0; cy < chunkCount(1); ++cy)
            for (int cx = 0; cx < chunkCount(0); ++cx) {
                const QByteArray *bytes = chunkForRead(cx, cy, cz, channel, frame);
                const int nz = std::min(Chunk, m_shape[2] - cz * Chunk);
                const int ny = std::min(Chunk, m_shape[1] - cy * Chunk);
                const int nx = std::min(Chunk, m_shape[0] - cx * Chunk);
                if (!bytes) {
                    const qint64 cells = qint64(nx) * ny * nz;
                    // A failed provider read is missing; an unwritten owned
                    // chunk reads as zero (valueAt's rule).
                    if (m_source) {
                        missing += cells;
                    } else if (0.0 < minimum) {
                        below += cells;
                    } else if (0.0 > maximum) {
                        above += cells;
                    } else {
                        counts[std::min(bins - 1, int((0.0 - minimum) * scale))] += cells;
                    }
                    continue;
                }
                const auto *data = reinterpret_cast<const uchar *>(bytes->constData());
                for (int z = 0; z < nz; ++z)
                    for (int y = 0; y < ny; ++y)
                        for (int x = 0; x < nx; ++x) {
                            const uchar *at = data + (qsizetype(z) * Chunk * Chunk + y * Chunk + x) * bytesPerValue;
                            if (m_kind == Kind::UInt8) { count(at[0]); continue; }
                            if (m_kind == Kind::UInt16) { count(qFromLittleEndian<quint16>(at)); continue; }
                            const quint32 bits = qFromLittleEndian<quint32>(at);
                            float value = 0;
                            std::memcpy(&value, &bits, sizeof(value));
                            count(value);
                        }
            }
    lock.unlock();
    QVariantList list;
    qint64 largest = 0;
    for (qint64 c : counts) { list.append(c); largest = std::max(largest, c); }
    return {{QStringLiteral("bins"), list}, {QStringLiteral("minimum"), minimum},
            {QStringLiteral("maximum"), maximum}, {QStringLiteral("below"), below},
            {QStringLiteral("above"), above}, {QStringLiteral("missing"), missing},
            {QStringLiteral("largest"), largest}, {QStringLiteral("channel"), channel},
            {QStringLiteral("frame"), frame},
            {QStringLiteral("total"), qint64(sizeX()) * sizeY() * sizeZ()}};
}
} // namespace QindaTK
