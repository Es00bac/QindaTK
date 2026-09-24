// SPDX-License-Identifier: LGPL-3.0-or-later
#include "voxel_volume.h"

#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace QindaTK {
namespace {
constexpr double Missing = std::numeric_limits<double>::quiet_NaN();

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

// Counts must be whole numbers: QVariant::toInt() would silently round 33.5.
bool wholeNumber(const QVariant &value, int minimum, int maximum, int *out)
{
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number) || number != std::floor(number)
        || number < minimum || number > maximum) return false;
    *out = int(number);
    return true;
}
} // namespace

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
qsizetype VoxelVolume::chunkBytesFor(Kind kind)
{
    return qsizetype(Chunk) * Chunk * Chunk * (kind == Kind::UInt8 ? 1 : kind == Kind::UInt16 ? 2 : 4);
}
qsizetype VoxelVolume::voxelOffset(int x, int y, int z) const
{
    const qsizetype bytesPerVoxel = chunkBytes() / (qsizetype(Chunk) * Chunk * Chunk);
    return (qsizetype(z % Chunk) * Chunk * Chunk + (y % Chunk) * Chunk + x % Chunk) * bytesPerVoxel;
}
void VoxelVolume::clearStorage()
{
    m_blocks.clear(); m_cache.clear(); m_cacheOrder.clear(); m_residentBytes = 0;
}
void VoxelVolume::dropCached(quint64 address) const
{
    const auto cached = m_cache.find(address);
    if (cached == m_cache.end()) return;
    m_cacheOrder.erase(cached->order);
    m_cache.erase(cached);
    m_residentBytes -= chunkBytes();
}
bool VoxelVolume::makeRoom() const
{
    // AGENT-GUARD: owned chunks are never evicted, only provider cache entries,
    // least recently used first. Callers add chunkBytes() only after this
    // returns true, so residentBytes never exceeds maximumResidentBytes.
    while (m_residentBytes + chunkBytes() > m_maxResidentBytes && !m_cacheOrder.empty())
        dropCached(m_cacheOrder.front());
    return m_residentBytes + chunkBytes() <= m_maxResidentBytes;
}
void VoxelVolume::setMaximumResidentBytes(qint64 bytes)
{
    if (bytes == m_maxResidentBytes) return;
    if (bytes < chunkBytes()) {
        fail(QStringLiteral("Resident limit must hold at least one chunk")); return;
    }
    if (bytes < qint64(m_blocks.size()) * chunkBytes()) {
        fail(QStringLiteral("Resident limit is below stored voxel data")); return;
    }
    m_maxResidentBytes = bytes;
    while (m_residentBytes > m_maxResidentBytes && !m_cacheOrder.empty())
        dropCached(m_cacheOrder.front());
    fail({}); emit changed();
}

bool VoxelVolume::configure(const QVariantMap &description)
{
    int shape[3] = {0, 0, 0};
    const char *names[] = {"sizeX", "sizeY", "sizeZ"};
    for (int axis = 0; axis < 3; ++axis)
        if (!wholeNumber(description.value(QString::fromLatin1(names[axis])), 1, 8192, &shape[axis])) {
            fail(QStringLiteral("Volume dimensions must be whole numbers from 1 to 8192")); return false;
        }
    int channels = 1, frames = 1;
    if (!wholeNumber(description.value(QStringLiteral("channels"), 1), 1, 64, &channels)
        || !wholeNumber(description.value(QStringLiteral("frames"), 1), 1, 4096, &frames)) {
        fail(QStringLiteral("Channels must be 1–64 and frames 1–4096")); return false;
    }
    const QString type = description.value(QStringLiteral("voxelType"), QStringLiteral("uint8")).toString();
    Kind kind = Kind::UInt8;
    if (type == QStringLiteral("uint16")) kind = Kind::UInt16;
    else if (type == QStringLiteral("float32")) kind = Kind::Float32;
    else if (type != QStringLiteral("uint8")) {
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
    if (m_maxResidentBytes < chunkBytesFor(kind)) {
        fail(QStringLiteral("Resident limit is smaller than one %1 chunk").arg(type)); return false;
    }
    std::copy(shape, shape + 3, m_shape);
    std::copy(spacing, spacing + 3, m_spacing);
    std::copy(origin, origin + 3, m_origin);
    std::copy(orientation, orientation + 9, m_orientation);
    m_channels = channels; m_frames = frames; m_kind = kind; m_type = type;
    m_unit = unit; m_valueUnit = valueUnit;
    clearStorage(); m_source.reset();
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
    const quint64 nx = quint64(chunkCount(0)), ny = quint64(chunkCount(1)), nz = quint64(chunkCount(2));
    return (((quint64(frame) * quint64(m_channels) + quint64(channel)) * nz + quint64(cz)) * ny
            + quint64(cy)) * nx + quint64(cx);
}
const QByteArray *VoxelVolume::chunkForRead(int cx, int cy, int cz, int channel, int frame) const
{
    const quint64 address = key(cx, cy, cz, channel, frame);
    if (const auto owned = m_blocks.constFind(address); owned != m_blocks.cend()) return &owned.value();
    if (!m_source) return nullptr;
    if (const auto cached = m_cache.find(address); cached != m_cache.end()) {
        m_cacheOrder.splice(m_cacheOrder.end(), m_cacheOrder, cached->order);
        return &cached->bytes;
    }
    QByteArray bytes = m_source->readChunk(cx, cy, cz, channel, frame);
    if (bytes.size() != chunkBytes() || !makeRoom()) return nullptr;
    m_cacheOrder.push_back(address);
    const auto inserted = m_cache.emplace(address, CachedChunk{std::move(bytes), std::prev(m_cacheOrder.end())});
    m_residentBytes += chunkBytes();
    return &inserted->bytes;
}
double VoxelVolume::valueAt(int x, int y, int z, int channel, int frame) const
{
    if (!validIndex(x, y, z, channel, frame)) return Missing;
    const QByteArray *bytes = chunkForRead(x / Chunk, y / Chunk, z / Chunk, channel, frame);
    // AGENT-NOTE: an unwritten owned chunk reads as zero. With a provider, a
    // failed read (or a chunk the resident limit cannot hold) is missing.
    if (!bytes) return m_source ? Missing : 0.0;
    const auto *data = reinterpret_cast<const uchar *>(bytes->constData()) + voxelOffset(x, y, z);
    switch (m_kind) {
    case Kind::UInt8: return data[0];
    case Kind::UInt16: return qFromLittleEndian<quint16>(data);
    case Kind::Float32: break;
    }
    const quint32 bits = qFromLittleEndian<quint32>(data);
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
QString VoxelVolume::writeVoxel(int x, int y, int z, double value, int channel, int frame)
{
    if (!validIndex(x, y, z, channel, frame)) return QStringLiteral("Voxel index is outside the volume");
    if (!std::isfinite(value)) return QStringLiteral("Voxel value must be finite");
    // Integer types round half away from zero and must then fit the type;
    // float32 must stay finite after narrowing.
    const double stored = m_kind == Kind::Float32 ? value : std::round(value);
    if ((m_kind == Kind::UInt8 && (stored < 0 || stored > 255))
        || (m_kind == Kind::UInt16 && (stored < 0 || stored > 65535))
        || (m_kind == Kind::Float32 && std::abs(stored) > double(std::numeric_limits<float>::max())))
        return QStringLiteral("Value %1 does not fit %2").arg(value).arg(m_type);
    const int cx = x / Chunk, cy = y / Chunk, cz = z / Chunk;
    const quint64 address = key(cx, cy, cz, channel, frame);
    auto owned = m_blocks.find(address);
    if (owned == m_blocks.end()) {
        QByteArray base;
        if (m_source) {
            // AGENT-GUARD: copy-on-write. A write into a provider chunk starts
            // from the provider's bytes; a zero block would erase the other
            // 32767 voxels of the chunk. The cached copy becomes the owned one.
            const QByteArray *provided = chunkForRead(cx, cy, cz, channel, frame);
            if (!provided) return QStringLiteral("Provider chunk is unavailable within the resident limit");
            base = *provided;
            dropCached(address);
        } else {
            base = QByteArray(chunkBytes(), '\0');
        }
        if (!makeRoom()) return QStringLiteral("Resident limit exceeded");
        owned = m_blocks.emplace(address, std::move(base));
        m_residentBytes += chunkBytes();
    }
    uchar *data = reinterpret_cast<uchar *>(owned.value().data()) + voxelOffset(x, y, z);
    if (m_kind == Kind::UInt8) {
        data[0] = uchar(stored);
    } else if (m_kind == Kind::UInt16) {
        qToLittleEndian<quint16>(quint16(stored), data);
    } else {
        const float narrowed = float(stored);
        quint32 bits = 0;
        std::memcpy(&bits, &narrowed, sizeof(bits));
        qToLittleEndian<quint32>(bits, data);
    }
    return {};
}
bool VoxelVolume::setVoxel(int x, int y, int z, double value, int channel, int frame)
{
    const QString error = writeVoxel(x, y, z, value, channel, frame);
    if (!error.isEmpty()) { fail(error); return false; }
    ++m_revision; fail({}); emit changed(); return true;
}
bool VoxelVolume::putChunk(int cx, int cy, int cz, int channel, int frame, const QByteArray &bytes)
{
    if (cx < 0 || cy < 0 || cz < 0 || cx >= chunkCount(0) || cy >= chunkCount(1)
        || cz >= chunkCount(2) || channel < 0 || channel >= m_channels || frame < 0
        || frame >= m_frames || bytes.size() != chunkBytes()) {
        fail(QStringLiteral("Invalid chunk coordinates or byte count")); return false;
    }
    const quint64 address = key(cx, cy, cz, channel, frame);
    if (!m_blocks.contains(address)) {
        // An owned chunk supersedes a cached provider copy of the same block.
        dropCached(address);
        if (!makeRoom()) { fail(QStringLiteral("Resident limit exceeded")); return false; }
        m_residentBytes += chunkBytes();
    }
    m_blocks.insert(address, bytes);
    ++m_revision; fail({}); emit changed(); return true;
}
void VoxelVolume::setSource(std::shared_ptr<VoxelChunkSource> source)
{
    m_source = std::move(source);
    clearStorage();
    ++m_revision; fail({}); emit changed();
}
bool VoxelVolume::fillSpherePhantom(double radiusFraction)
{
    if (sizeX() < 2 || sizeY() < 2 || sizeZ() < 2 || m_channels != 1 || m_frames != 1) {
        fail(QStringLiteral("A phantom needs one channel, one frame and at least 2 voxels per axis"));
        return false;
    }
    if (!std::isfinite(radiusFraction) || radiusFraction <= 0 || radiusFraction > 0.5) {
        fail(QStringLiteral("Phantom radius fraction must be greater than 0 and at most 0.5"));
        return false;
    }
    clearStorage(); m_source.reset();
    // AGENT-NOTE: known answer for tests and examples. The centre is index
    // (size−1)/2; r = fraction × the smallest physical extent (size × spacing).
    // Within 0.55 r (spatial units) voxels hold the peak, up to r half of it,
    // elsewhere zero. Peak: 220 uint8, 55000 uint16, 1 float32.
    const double centre[3] = {(sizeX() - 1) / 2.0, (sizeY() - 1) / 2.0, (sizeZ() - 1) / 2.0};
    const double radius = std::min({sizeX() * m_spacing[0], sizeY() * m_spacing[1],
                                    sizeZ() * m_spacing[2]}) * radiusFraction;
    const double peak = m_kind == Kind::UInt16 ? 55000 : m_kind == Kind::UInt8 ? 220 : 1;
    int low[3], high[3];
    for (int axis = 0; axis < 3; ++axis) {
        low[axis] = std::max(0, int(std::floor(centre[axis] - radius / m_spacing[axis])));
        high[axis] = std::min(m_shape[axis] - 1, int(std::ceil(centre[axis] + radius / m_spacing[axis])));
    }
    QString error;
    for (int z = low[2]; z <= high[2] && error.isEmpty(); ++z)
        for (int y = low[1]; y <= high[1] && error.isEmpty(); ++y)
            for (int x = low[0]; x <= high[0] && error.isEmpty(); ++x) {
                const double dx = (x - centre[0]) * m_spacing[0], dy = (y - centre[1]) * m_spacing[1];
                const double dz = (z - centre[2]) * m_spacing[2];
                const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (distance <= radius)
                    error = writeVoxel(x, y, z, distance <= radius * 0.55 ? peak : peak * 0.5, 0, 0);
            }
    ++m_revision;
    fail(error.isEmpty() ? QString() : QStringLiteral("Phantom incomplete: %1").arg(error));
    emit changed();
    return error.isEmpty();
}
double VoxelVolume::sampleValue(double x, double y, double z, int channel, int frame,
                                Interpolation interpolation) const
{
    // AGENT-CONTRACT (docs/volume-imaging.md, D-086): an index is a cell
    // centre, so each axis samples −0.5 … size−0.5. Nearest picks the
    // containing cell; Linear clamps to the outer centres in the edge half cell.
    const double point[3] = {x, y, z};
    if (m_shape[0] < 1) return Missing;
    int lower[3], upper[3];
    double fraction[3];
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(point[axis]) || point[axis] < -0.5 || point[axis] > m_shape[axis] - 0.5)
            return Missing;
        const double last = m_shape[axis] - 1;
        if (interpolation == Nearest) {
            lower[axis] = upper[axis] = int(std::clamp(std::round(point[axis]), 0.0, last));
            fraction[axis] = 0;
            continue;
        }
        const double clamped = std::clamp(point[axis], 0.0, last);
        lower[axis] = int(std::floor(clamped));
        upper[axis] = std::min(lower[axis] + 1, m_shape[axis] - 1);
        fraction[axis] = clamped - lower[axis];
    }
    double total = 0;
    for (int corner = 0; corner < 8; ++corner) {
        double weight = 1;
        int index[3];
        for (int axis = 0; axis < 3; ++axis) {
            const bool high = (corner >> axis) & 1;
            weight *= high ? fraction[axis] : 1 - fraction[axis];
            index[axis] = high ? upper[axis] : lower[axis];
        }
        if (weight == 0) continue;
        const double value = valueAt(index[0], index[1], index[2], channel, frame);
        if (!std::isfinite(value)) return Missing;
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
    // Orthonormal orientation: its inverse is the transpose.
    const double delta[3] = {x - m_origin[0], y - m_origin[1], z - m_origin[2]};
    return {(m_orientation[0] * delta[0] + m_orientation[3] * delta[1] + m_orientation[6] * delta[2]) / m_spacing[0],
            (m_orientation[1] * delta[0] + m_orientation[4] * delta[1] + m_orientation[7] * delta[2]) / m_spacing[1],
            (m_orientation[2] * delta[0] + m_orientation[5] * delta[1] + m_orientation[8] * delta[2]) / m_spacing[2]};
}

} // namespace QindaTK
