// SPDX-License-Identifier: LGPL-3.0-or-later
#include "voxel_volume.h"

#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace QindaTK {
namespace {
bool triples(const QVariant &value, double *out)
{
    const auto list = value.toList();
    if (list.size() != 3) return false;
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        out[i] = list[i].toDouble(&ok);
        if (!ok || !std::isfinite(out[i])) return false;
    }
    return true;
}
}

VoxelVolume::VoxelVolume(QObject *parent) : QObject(parent) {}
QVariantList VoxelVolume::spacing() const { return {m_spacing[0], m_spacing[1], m_spacing[2]}; }
QVariantList VoxelVolume::origin() const { return {m_origin[0], m_origin[1], m_origin[2]}; }
QVariantList VoxelVolume::orientation() const
{
    QVariantList result;
    for (double value : m_orientation) result.append(value);
    return result;
}
void VoxelVolume::fail(const QString &message)
{
    if (m_error == message) return;
    m_error = message;
    emit errorChanged();
}
int VoxelVolume::bytesPerVoxel() const
{
    return m_type == QStringLiteral("uint8") ? 1 : m_type == QStringLiteral("uint16") ? 2 : 4;
}
qsizetype VoxelVolume::chunkBytes() const { return qsizetype(Chunk) * Chunk * Chunk * bytesPerVoxel(); }
void VoxelVolume::setMaximumResidentBytes(qint64 bytes)
{
    if (bytes < chunkBytes() || bytes == m_maxResidentBytes) return;
    if (bytes < qsizetype(m_blocks.size()) * chunkBytes()) {
        fail(QStringLiteral("Resident limit is below stored voxel data"));
        return;
    }
    m_maxResidentBytes = bytes;
    while (m_residentBytes > m_maxResidentBytes && !m_cacheOrder.isEmpty()) {
        m_cache.remove(m_cacheOrder.takeFirst());
        m_residentBytes -= chunkBytes();
    }
    emit changed();
}

bool VoxelVolume::configure(const QVariantMap &description)
{
    int shape[3];
    for (int axis = 0; axis < 3; ++axis) {
        bool ok = false;
        const char *names[] = {"sizeX", "sizeY", "sizeZ"};
        shape[axis] = description.value(QString::fromLatin1(names[axis])).toInt(&ok);
        if (!ok || shape[axis] < 1 || shape[axis] > 8192) {
            fail(QStringLiteral("Volume dimensions must be 1–8192")); return false;
        }
    }
    bool channelsOk = false, framesOk = false;
    const int channels = description.value(QStringLiteral("channels"), 1).toInt(&channelsOk);
    const int frames = description.value(QStringLiteral("frames"), 1).toInt(&framesOk);
    if (!channelsOk || !framesOk || channels < 1 || channels > 64 || frames < 1 || frames > 4096) {
        fail(QStringLiteral("Invalid channel or frame count")); return false;
    }
    const QString type = description.value(QStringLiteral("voxelType"), QStringLiteral("uint8")).toString();
    if (type != QStringLiteral("uint8") && type != QStringLiteral("uint16")
        && type != QStringLiteral("float32")) {
        fail(QStringLiteral("Voxel type must be uint8, uint16 or float32")); return false;
    }
    double spacing[3], origin[3], orientation[9];
    if (!triples(description.value(QStringLiteral("spacing"), QVariantList{1, 1, 1}), spacing)
        || !triples(description.value(QStringLiteral("origin"), QVariantList{0, 0, 0}), origin)) {
        fail(QStringLiteral("Spacing and origin must have three finite values")); return false;
    }
    for (double value : spacing)
        if (value <= 0) { fail(QStringLiteral("Spacing must be positive")); return false; }
    const QVariantList matrix = description.value(QStringLiteral("orientation"),
        QVariantList{1, 0, 0, 0, 1, 0, 0, 0, 1}).toList();
    if (matrix.size() != 9) { fail(QStringLiteral("Orientation must be a 3×3 matrix")); return false; }
    for (int i = 0; i < 9; ++i) {
        bool ok = false;
        orientation[i] = matrix[i].toDouble(&ok);
        if (!ok || !std::isfinite(orientation[i])) {
            fail(QStringLiteral("Orientation must be finite")); return false;
        }
    }
    for (int a = 0; a < 3; ++a)
        for (int b = a; b < 3; ++b) {
            double dot = 0;
            for (int row = 0; row < 3; ++row) dot += orientation[row * 3 + a] * orientation[row * 3 + b];
            if (std::abs(dot - (a == b ? 1.0 : 0.0)) > 1e-5) {
                fail(QStringLiteral("Orientation axes must be orthonormal")); return false;
            }
        }
    const QString unit = description.value(QStringLiteral("unit"), QStringLiteral("mm")).toString();
    const QString valueUnit = description.value(QStringLiteral("valueUnit")).toString();
    if (unit.isEmpty()) { fail(QStringLiteral("Spatial unit is required")); return false; }
    std::copy(shape, shape + 3, m_shape);
    std::copy(spacing, spacing + 3, m_spacing);
    std::copy(origin, origin + 3, m_origin);
    std::copy(orientation, orientation + 9, m_orientation);
    m_channels = channels; m_frames = frames; m_type = type; m_unit = unit; m_valueUnit = valueUnit;
    m_blocks.clear(); m_cache.clear(); m_cacheOrder.clear(); m_source.reset(); m_residentBytes = 0;
    ++m_revision; fail({}); emit changed();
    return true;
}

bool VoxelVolume::validIndex(int x, int y, int z, int channel, int frame) const
{
    return x >= 0 && x < sizeX() && y >= 0 && y < sizeY() && z >= 0 && z < sizeZ()
        && channel >= 0 && channel < m_channels && frame >= 0 && frame < m_frames;
}
quint64 VoxelVolume::key(int cx, int cy, int cz, int channel, int frame) const
{
    const quint64 nx = (sizeX() + Chunk - 1) / Chunk, ny = (sizeY() + Chunk - 1) / Chunk;
    const quint64 nz = (sizeZ() + Chunk - 1) / Chunk;
    return (((quint64(frame) * m_channels + channel) * nz + cz) * ny + cy) * nx + cx;
}
const QByteArray *VoxelVolume::chunkForRead(int cx, int cy, int cz, int channel, int frame) const
{
    const quint64 address = key(cx, cy, cz, channel, frame);
    if (const auto found = m_blocks.constFind(address); found != m_blocks.cend()) return &found.value();
    if (!m_source) return nullptr;
    if (const auto found = m_cache.constFind(address); found != m_cache.cend()) {
        m_cacheOrder.removeAll(address); m_cacheOrder.append(address);
        return &found.value();
    }
    QByteArray bytes = m_source->readChunk(cx, cy, cz, channel, frame);
    if (bytes.size() != chunkBytes()) return nullptr;
    while (m_residentBytes + chunkBytes() > m_maxResidentBytes && !m_cacheOrder.isEmpty()) {
        m_cache.remove(m_cacheOrder.takeFirst()); m_residentBytes -= chunkBytes();
    }
    if (m_residentBytes + chunkBytes() > m_maxResidentBytes) return nullptr;
    m_cache.insert(address, std::move(bytes)); m_cacheOrder.append(address);
    m_residentBytes += chunkBytes();
    return &m_cache[address];
}
double VoxelVolume::valueAt(int x, int y, int z, int channel, int frame) const
{
    if (!validIndex(x, y, z, channel, frame)) return std::numeric_limits<double>::quiet_NaN();
    const QByteArray *bytes = chunkForRead(x / Chunk, y / Chunk, z / Chunk, channel, frame);
    if (!bytes) return m_source ? std::numeric_limits<double>::quiet_NaN() : 0.0;
    const qsizetype offset = (qsizetype(z % Chunk) * Chunk * Chunk
                             + (y % Chunk) * Chunk + x % Chunk) * bytesPerVoxel();
    const auto *data = reinterpret_cast<const uchar *>(bytes->constData() + offset);
    if (m_type == QStringLiteral("uint8")) return data[0];
    if (m_type == QStringLiteral("uint16")) return qFromLittleEndian<quint16>(data);
    const quint32 bits = qFromLittleEndian<quint32>(data);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
bool VoxelVolume::writeVoxel(int x, int y, int z, double value, int channel, int frame)
{
    if (!validIndex(x, y, z, channel, frame) || !std::isfinite(value)) return false;
    if ((m_type == QStringLiteral("uint8") && (value < 0 || value > 255))
        || (m_type == QStringLiteral("uint16") && (value < 0 || value > 65535))) return false;
    const quint64 address = key(x / Chunk, y / Chunk, z / Chunk, channel, frame);
    auto found = m_blocks.find(address);
    if (found == m_blocks.end()) {
        while (m_residentBytes + chunkBytes() > m_maxResidentBytes && !m_cacheOrder.isEmpty()) {
            m_cache.remove(m_cacheOrder.takeFirst()); m_residentBytes -= chunkBytes();
        }
        if (m_residentBytes + chunkBytes() > m_maxResidentBytes) return false;
        found = m_blocks.insert(address, QByteArray(chunkBytes(), '\0'));
        m_residentBytes += chunkBytes();
    }
    const qsizetype offset = (qsizetype(z % Chunk) * Chunk * Chunk
                             + (y % Chunk) * Chunk + x % Chunk) * bytesPerVoxel();
    char *data = found.value().data() + offset;
    if (m_type == QStringLiteral("uint8")) data[0] = char(std::lround(value));
    else if (m_type == QStringLiteral("uint16")) qToLittleEndian<quint16>(quint16(std::lround(value)), reinterpret_cast<uchar *>(data));
    else {
        const float narrowed = float(value);
        quint32 bits; std::memcpy(&bits, &narrowed, sizeof(bits));
        qToLittleEndian<quint32>(bits, reinterpret_cast<uchar *>(data));
    }
    return true;
}
bool VoxelVolume::setVoxel(int x, int y, int z, double value, int channel, int frame)
{
    if (!writeVoxel(x, y, z, value, channel, frame)) {
        fail(QStringLiteral("Invalid voxel or resident limit exceeded")); return false;
    }
    ++m_revision; fail({}); emit changed(); return true;
}
bool VoxelVolume::putChunk(int cx, int cy, int cz, int channel, int frame, const QByteArray &bytes)
{
    if (bytes.size() != chunkBytes() || !validIndex(cx * Chunk, cy * Chunk, cz * Chunk, channel, frame)) {
        fail(QStringLiteral("Invalid chunk coordinates or byte count")); return false;
    }
    const quint64 address = key(cx, cy, cz, channel, frame);
    if (!m_blocks.contains(address)) {
        while (m_residentBytes + chunkBytes() > m_maxResidentBytes && !m_cacheOrder.isEmpty()) {
            m_cache.remove(m_cacheOrder.takeFirst()); m_residentBytes -= chunkBytes();
        }
        if (m_residentBytes + chunkBytes() > m_maxResidentBytes) {
            fail(QStringLiteral("Resident limit exceeded")); return false;
        }
        m_residentBytes += chunkBytes();
    }
    m_blocks.insert(address, bytes);
    ++m_revision; fail({}); emit changed(); return true;
}
void VoxelVolume::setSource(std::shared_ptr<VoxelChunkSource> source)
{
    m_source = std::move(source);
    m_blocks.clear(); m_cache.clear(); m_cacheOrder.clear(); m_residentBytes = 0;
    ++m_revision; emit changed();
}
bool VoxelVolume::fillSpherePhantom(double radiusFraction)
{
    if (sizeX() < 2 || sizeY() < 2 || sizeZ() < 2 || !std::isfinite(radiusFraction)
        || radiusFraction <= 0 || radiusFraction > 0.5 || m_channels != 1 || m_frames != 1) return false;
    m_blocks.clear(); m_cache.clear(); m_cacheOrder.clear(); m_source.reset(); m_residentBytes = 0;
    const double cx = (sizeX() - 1) / 2.0, cy = (sizeY() - 1) / 2.0, cz = (sizeZ() - 1) / 2.0;
    const double radius = std::min({sizeX() * m_spacing[0], sizeY() * m_spacing[1],
                                    sizeZ() * m_spacing[2]}) * radiusFraction;
    const double peak = m_type == QStringLiteral("uint16") ? 55000 : m_type == QStringLiteral("uint8") ? 220 : 1;
    for (int z = 0; z < sizeZ(); ++z)
        for (int y = 0; y < sizeY(); ++y)
            for (int x = 0; x < sizeX(); ++x) {
                const double dx = (x - cx) * m_spacing[0], dy = (y - cy) * m_spacing[1];
                const double dz = (z - cz) * m_spacing[2];
                const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (distance <= radius && !writeVoxel(x, y, z,
                        distance <= radius * 0.55 ? peak : peak * 0.5, 0, 0)) {
                    fail(QStringLiteral("Phantom exceeds resident limit")); return false;
                }
            }
    ++m_revision; fail({}); emit changed(); return true;
}
double VoxelVolume::sampleValue(double x, double y, double z, int channel, int frame,
                                Interpolation interpolation) const
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
        || x < 0 || y < 0 || z < 0 || x > sizeX() - 1 || y > sizeY() - 1 || z > sizeZ() - 1)
        return std::numeric_limits<double>::quiet_NaN();
    if (interpolation == Nearest)
        return valueAt(int(std::lround(x)), int(std::lround(y)), int(std::lround(z)), channel, frame);
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y)), z0 = int(std::floor(z));
    const int x1 = std::min(x0 + 1, sizeX() - 1), y1 = std::min(y0 + 1, sizeY() - 1);
    const int z1 = std::min(z0 + 1, sizeZ() - 1);
    double total = 0;
    for (int iz = 0; iz < 2; ++iz)
        for (int iy = 0; iy < 2; ++iy)
            for (int ix = 0; ix < 2; ++ix) {
                const double weight = (ix ? x - x0 : 1 - (x - x0))
                    * (iy ? y - y0 : 1 - (y - y0)) * (iz ? z - z0 : 1 - (z - z0));
                if (weight == 0) continue;
                const double value = valueAt(ix ? x1 : x0, iy ? y1 : y0, iz ? z1 : z0,
                                             channel, frame);
                if (!std::isfinite(value)) return std::numeric_limits<double>::quiet_NaN();
                total += weight * value;
            }
    return total;
}
QVariantMap VoxelVolume::sample(double x, double y, double z, int channel, int frame,
                                Interpolation interpolation) const
{
    const double value = sampleValue(x, y, z, channel, frame, interpolation);
    return std::isfinite(value) ? QVariantMap{{QStringLiteral("valid"), true},
                                               {QStringLiteral("value"), value},
                                               {QStringLiteral("unit"), m_valueUnit}}
                                : QVariantMap{{QStringLiteral("valid"), false}};
}
QVariantList VoxelVolume::worldFromVoxel(double x, double y, double z) const
{
    const double scaled[3] = {x * m_spacing[0], y * m_spacing[1], z * m_spacing[2]};
    return {m_origin[0] + m_orientation[0] * scaled[0] + m_orientation[1] * scaled[1] + m_orientation[2] * scaled[2],
            m_origin[1] + m_orientation[3] * scaled[0] + m_orientation[4] * scaled[1] + m_orientation[5] * scaled[2],
            m_origin[2] + m_orientation[6] * scaled[0] + m_orientation[7] * scaled[1] + m_orientation[8] * scaled[2]};
}
QVariantList VoxelVolume::voxelFromWorld(double x, double y, double z) const
{
    const double delta[3] = {x - m_origin[0], y - m_origin[1], z - m_origin[2]};
    return {(m_orientation[0] * delta[0] + m_orientation[3] * delta[1] + m_orientation[6] * delta[2]) / m_spacing[0],
            (m_orientation[1] * delta[0] + m_orientation[4] * delta[1] + m_orientation[7] * delta[2]) / m_spacing[1],
            (m_orientation[2] * delta[0] + m_orientation[5] * delta[1] + m_orientation[8] * delta[2]) / m_spacing[2]};
}

} // namespace QindaTK
