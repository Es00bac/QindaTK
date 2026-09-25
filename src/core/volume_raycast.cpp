// SPDX-License-Identifier: LGPL-3.0-or-later
#include "volume_raycast.h"
#include "transfer_function_model.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace QindaTK {
namespace {
constexpr double Pi = 3.14159265358979323846;
constexpr double Parallel = 1e-12;

// World direction → continuous voxel-index delta: Rᵀ·v, divided by spacing.
void toIndex(const VoxelVolume &volume, const double world[3], double out[3])
{
    const QVariantList r = volume.orientation();
    double m[9];
    for (int i = 0; i < 9; ++i) m[i] = r[i].toDouble();
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = (m[axis] * world[0] + m[3 + axis] * world[1] + m[6 + axis] * world[2])
                    / volume.spacingAt(axis);
}
quint8 channelByte(double value)
{
    return quint8(std::clamp<long>(std::lround(std::clamp(value, 0.0, 1.0) * 255), 0, 255));
}
} // namespace

VolumeTransferTable VolumeTransferTable::fromModel(const TransferFunctionModel &model)
{
    VolumeTransferTable table;
    table.domainMin = model.domainMin();
    table.domainMax = model.domainMax();
    for (int k = 0; k < Size; ++k) {
        const double value = table.domainMin + (table.domainMax - table.domainMin) * k / (Size - 1);
        const QColor color = model.colorAt(value);
        table.rgba[k * 4 + 0] = quint8(color.red());
        table.rgba[k * 4 + 1] = quint8(color.green());
        table.rgba[k * 4 + 2] = quint8(color.blue());
        table.rgba[k * 4 + 3] = channelByte(model.opacityAt(value));
    }
    // Probe the model's own classification: one span outside each end, NaN.
    const double span = table.domainMax - table.domainMin;
    const auto entry = [&model](double value, std::array<quint8, 4> *out) {
        const QColor color = model.colorAt(value);
        *out = {quint8(color.red()), quint8(color.green()), quint8(color.blue()),
                channelByte(model.opacityAt(value))};
    };
    entry(table.domainMin - span, &table.below);
    entry(table.domainMax + span, &table.above);
    entry(std::numeric_limits<double>::quiet_NaN(), &table.missing);
    return table;
}
VolumeTransferTable VolumeTransferTable::fromWindow(double center, double width)
{
    VolumeTransferTable table;
    if (!std::isfinite(center) || !std::isfinite(width) || width <= 0) { center = 127.5; width = 255; }
    table.domainMin = center - width / 2;
    table.domainMax = center + width / 2;
    for (int k = 0; k < Size; ++k)
        for (int c = 0; c < 4; ++c) table.rgba[k * 4 + c] = quint8(k);
    table.below = {0, 0, 0, 0};
    table.above = {255, 255, 255, 255};
    return table;
}
void VolumeTransferTable::lookup(double value, double out[4]) const
{
    const auto special = [out](const std::array<quint8, 4> &entry) {
        for (int c = 0; c < 4; ++c) out[c] = entry[c] / 255.0;
    };
    if (!std::isfinite(value)) return special(missing);
    if (value < domainMin) return special(below);
    if (value > domainMax) return special(above);
    const double t = std::clamp((value - domainMin) / (domainMax - domainMin), 0.0, 1.0);
    const double position = t * (Size - 1);
    const int low = std::min(int(std::floor(position)), Size - 1);
    const int high = std::min(low + 1, Size - 1);
    const double fraction = position - low;
    for (int c = 0; c < 4; ++c)
        out[c] = (rgba[low * 4 + c] * (1 - fraction) + rgba[high * 4 + c] * fraction) / 255.0;
}
QImage VolumeTransferTable::image() const
{
    QImage result(Size, 1, QImage::Format_RGBA8888);
    std::memcpy(result.scanLine(0), rgba.data(), rgba.size());
    return result;
}

VolumeRayGeometry VolumeRayGeometry::compute(const VoxelVolume &volume, const VolumeRayParams &params,
                                             const QSizeF &viewSize)
{
    VolumeRayGeometry g;
    const int shape[3] = {volume.sizeX(), volume.sizeY(), volume.sizeZ()};
    if (shape[0] < 1 || shape[1] < 1 || shape[2] < 1 || viewSize.width() <= 0
        || viewSize.height() <= 0 || !std::isfinite(params.zoom) || params.zoom <= 0
        || !std::isfinite(params.samplingRate) || params.samplingRate <= 0) return g;
    double diagonal = 0, minimumSpacing = std::numeric_limits<double>::max();
    for (int axis = 0; axis < 3; ++axis) {
        const double extent = shape[axis] * volume.spacingAt(axis);
        diagonal += extent * extent;
        minimumSpacing = std::min(minimumSpacing, volume.spacingAt(axis));
        g.centre[axis] = (shape[axis] - 1) / 2.0;
        g.boxLow[axis] = -0.5 + std::clamp(params.clipLow[axis], 0.0, 1.0) * shape[axis];
        g.boxHigh[axis] = -0.5 + std::clamp(params.clipHigh[axis], 0.0, 1.0) * shape[axis];
        g.sizeInverse[axis] = 1.0 / shape[axis];
    }
    const double radius = 0.5 * std::sqrt(diagonal);
    // Camera basis M = Ry(yaw)·Rx(−pitch): right = M·x, up = M·y and the ray
    // runs along −M·z. Yaw 0, pitch 0 looks down world −Z with +X right and
    // +Y up; positive yaw moves the eye towards +X, positive pitch towards +Y.
    const double yaw = params.yawDegrees * Pi / 180, pitch = params.pitchDegrees * Pi / 180;
    const double right[3] = {std::cos(yaw), 0, -std::sin(yaw)};
    const double down[3] = {std::sin(yaw) * std::sin(pitch), -std::cos(pitch), std::cos(yaw) * std::sin(pitch)};
    const double forward[3] = {-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    g.width = viewSize.width();
    g.height = viewSize.height();
    g.pixelsPerUnit = params.zoom * std::min(g.width, g.height) / (2 * radius);
    toIndex(volume, right, g.right);
    toIndex(volume, down, g.down);
    toIndex(volume, forward, g.direction);
    for (int axis = 0; axis < 3; ++axis) {
        g.right[axis] /= g.pixelsPerUnit;
        g.down[axis] /= g.pixelsPerUnit;
    }
    g.step = minimumSpacing / params.samplingRate;
    g.exponent = 1.0 / params.samplingRate;
    g.maximumSteps = int(std::min<double>(VolumeRaycaster::MaximumSteps,
                                          std::ceil(2 * radius / g.step) + 1));
    g.valid = true;
    for (int axis = 0; axis < 3; ++axis)
        if (!(g.boxLow[axis] < g.boxHigh[axis])) g.valid = false;
    return g;
}
bool VolumeRayGeometry::span(double px, double py, double origin[3], double *tNear, double *tFar) const
{
    double nearest = -std::numeric_limits<double>::infinity();
    double farthest = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        origin[axis] = centre[axis] + (px - width / 2) * right[axis] + (py - height / 2) * down[axis];
        if (std::abs(direction[axis]) < Parallel) {
            if (origin[axis] < boxLow[axis] || origin[axis] > boxHigh[axis]) return false;
            continue;
        }
        const double t1 = (boxLow[axis] - origin[axis]) / direction[axis];
        const double t2 = (boxHigh[axis] - origin[axis]) / direction[axis];
        nearest = std::max(nearest, std::min(t1, t2));
        farthest = std::min(farthest, std::max(t1, t2));
    }
    *tNear = nearest;
    *tFar = farthest;
    return farthest > nearest;
}

void VolumeRaycaster::shade(const VoxelVolume &volume, const VolumeTransferTable &table,
                            const VolumeRayParams &params, const VolumeRayGeometry &g,
                            double px, double py, double out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    double origin[3], tNear = 0, tFar = 0;
    if (!g.valid || !g.span(px, py, origin, &tNear, &tFar)) return;
    const bool maximum = params.mode == VolumeRenderMode::MaximumIntensity;
    double best = 0, rgba[4];
    bool found = false;
    for (int k = 0; k < g.maximumSteps; ++k) {
        const double t = tNear + (k + 0.5) * g.step;
        if (t >= tFar) break;
        const double value = volume.sampleValue(origin[0] + t * g.direction[0],
                                                origin[1] + t * g.direction[1],
                                                origin[2] + t * g.direction[2],
                                                params.channel, params.frame, params.interpolation);
        if (maximum) {
            if (std::isfinite(value) && (!found || value > best)) { best = value; found = true; }
            continue;
        }
        table.lookup(value, rgba); // a missing sample takes the `missing` entry
        if (rgba[3] <= 0) continue;
        const double alpha = 1 - std::pow(1 - rgba[3], g.exponent);
        const double weight = (1 - out[3]) * alpha;
        for (int c = 0; c < 3; ++c) out[c] += weight * rgba[c];
        out[3] += weight;
        if (out[3] >= TerminationOpacity) break;
    }
    if (maximum && found) {
        table.lookup(best, rgba);
        for (int c = 0; c < 3; ++c) out[c] = rgba[c] * rgba[3];
        out[3] = rgba[3];
    }
}
QImage VolumeRaycaster::render(const VoxelVolume &volume, const VolumeTransferTable &table,
                               const VolumeRayParams &params, const QSize &size,
                               const std::function<bool()> &canceled)
{
    if (size.isEmpty()) return {};
    return render(volume, table, params, VolumeRayGeometry::compute(volume, params, QSizeF(size)),
                  size, canceled);
}
QImage VolumeRaycaster::render(const VoxelVolume &volume, const VolumeTransferTable &table,
                               const VolumeRayParams &params, const VolumeRayGeometry &g,
                               const QSize &size, const std::function<bool()> &canceled)
{
    if (size.isEmpty()) return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(Qt::transparent);
    if (!g.valid) return image;
    double rgba[4];
    for (int row = 0; row < size.height(); ++row) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(row));
        for (int column = 0; column < size.width(); ++column) {
            if (canceled && canceled()) return {};
            shade(volume, table, params, g, column + 0.5, row + 0.5, rgba);
            line[column] = qRgba(channelByte(rgba[0]), channelByte(rgba[1]),
                                 channelByte(rgba[2]), channelByte(rgba[3]));
        }
    }
    return image;
}
bool VolumeRaycaster::pick(const VoxelVolume &volume, const VolumeTransferTable &table,
                           const VolumeRayParams &params, const QSizeF &viewSize,
                           const QPointF &point, double voxel[3])
{
    const VolumeRayGeometry g = VolumeRayGeometry::compute(volume, params, viewSize);
    double origin[3], tNear = 0, tFar = 0, rgba[4];
    if (!g.valid || !g.span(point.x(), point.y(), origin, &tNear, &tFar)) return false;
    const bool maximum = params.mode == VolumeRenderMode::MaximumIntensity;
    double accumulated = 0, best = 0;
    bool found = false;
    for (int k = 0; k < g.maximumSteps; ++k) {
        const double t = tNear + (k + 0.5) * g.step;
        if (t >= tFar) break;
        const double q[3] = {origin[0] + t * g.direction[0], origin[1] + t * g.direction[1],
                             origin[2] + t * g.direction[2]};
        const double value = volume.sampleValue(q[0], q[1], q[2], params.channel, params.frame,
                                                params.interpolation);
        if (maximum) {
            if (std::isfinite(value) && (!found || value > best)) {
                best = value; found = true;
                std::copy(q, q + 3, voxel);
            }
            continue;
        }
        table.lookup(value, rgba);
        if (rgba[3] <= 0) continue;
        accumulated += (1 - accumulated) * (1 - std::pow(1 - rgba[3], g.exponent));
        if (accumulated >= 0.5) { std::copy(q, q + 3, voxel); return true; }
    }
    return found;
}
qint64 VolumeRaycaster::textureBytes(const VoxelVolume &volume)
{
    return qint64(volume.sizeX()) * volume.sizeY() * volume.sizeZ() * volume.bytesPerVoxel();
}
QString VolumeRaycaster::gpuUnavailableReason(const VoxelVolume *volume, const VolumeGpuProbe &probe,
                                              qint64 budgetBytes, bool referenceRequested)
{
    // AGENT-NOTE: the order is the documented fallback order (D-147); tests
    // compare these sentences exactly.
    if (referenceRequested) return QStringLiteral("Reference renderer requested");
    if (!volume || volume->sizeX() < 1) return QStringLiteral("No volume");
    if (!probe.rhi) return QStringLiteral("The scene graph has no GPU (software renderer)");
    if (!probe.shaders) return QStringLiteral("GPU volume shaders are not built");
    if (!probe.threeDimensionalTextures) return QStringLiteral("The GPU has no 3D textures");
    if (!probe.formatSupported)
        return QStringLiteral("The GPU cannot sample %1 3D textures").arg(volume->voxelType());
    const int side = std::min(probe.maximumTextureSide, MaximumTextureSide);
    if (volume->sizeX() > side || volume->sizeY() > side || volume->sizeZ() > side)
        return QStringLiteral("The volume exceeds the %1-voxel GPU texture side").arg(side);
    if (textureBytes(*volume) > budgetBytes)
        return QStringLiteral("The volume exceeds the GPU texture budget");
    return {};
}
bool VolumeRaycaster::packTexture(const VoxelVolume &volume, int channel, int frame, QByteArray *out,
                                  const std::function<bool()> &canceled)
{
    out->clear();
    const int sx = volume.sizeX(), sy = volume.sizeY(), sz = volume.sizeZ();
    const int bpv = volume.bytesPerVoxel(), side = VoxelVolume::ChunkSide;
    if (sx < 1 || channel < 0 || channel >= volume.channels() || frame < 0 || frame >= volume.frames())
        return false;
    QByteArray packed(qsizetype(textureBytes(volume)), Qt::Uninitialized);
    QByteArray chunk;
    // AGENT-GUARD: iterate chunk by chunk, not voxel by voxel, so a provider
    // volume larger than maximumResidentBytes streams through the LRU cache
    // once instead of thrashing it; `chunk` is the only extra copy held.
    for (int cz = 0; cz * side < sz; ++cz)
        for (int cy = 0; cy * side < sy; ++cy)
            for (int cx = 0; cx * side < sx; ++cx) {
                if (canceled && canceled()) { out->clear(); return false; }
                // A reconfigure during a background pack changes the chunk
                // size; the result is obsolete anyway, so stop safely.
                if (!volume.copyChunk(cx, cy, cz, channel, frame, &chunk)
                    || chunk.size() != qsizetype(side) * side * side * bpv) { out->clear(); return false; }
                const int columns = std::min(side, sx - cx * side);
                for (int z = 0; z < side && cz * side + z < sz; ++z)
                    for (int y = 0; y < side && cy * side + y < sy; ++y) {
                        const qsizetype from = (qsizetype(z) * side * side + qsizetype(y) * side) * bpv;
                        const qsizetype to = ((qsizetype(cz * side + z) * sy + (cy * side + y)) * sx
                                              + qsizetype(cx) * side) * bpv;
                        std::memcpy(packed.data() + to, chunk.constData() + from, size_t(columns) * bpv);
                    }
            }
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    // Chunks are little-endian; a texture upload expects host order.
    auto *bytes = reinterpret_cast<uchar *>(packed.data());
    for (qsizetype i = 0; bpv > 1 && i + bpv <= packed.size(); i += bpv) std::reverse(bytes + i, bytes + i + bpv);
#endif
    *out = std::move(packed);
    return true;
}

} // namespace QindaTK
