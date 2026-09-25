// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "voxel_volume.h"
#include <QByteArray>
#include <QImage>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <array>
#include <functional>

namespace QindaTK {
class TransferFunctionModel;

// AGENT-CONTRACT (docs/volume-imaging.md#volume-rendering, D-146): the
// reference raycaster in volume_raycast.cpp and the GPU fragment shader
// src/shaders/volume_raycast.frag implement ONE algorithm: the same ray
// geometry, transfer table, sample positions, opacity correction, early
// termination and maximum-intensity rule. Change both, or the GPU view stops
// drawing what the tests verify.

// 256 straight RGBA8 entries over [domainMin, domainMax]. Both render paths
// look values up in this table with linear interpolation between entries
// (the GPU samples it as a 256×1 linear texture); a value below or above the
// domain takes `below`/`above` and a NaN takes `missing` (D-263), which the
// GPU receives as three uniforms.
struct VolumeTransferTable {
    static constexpr int Size = 256;
    double domainMin = 0, domainMax = 255;
    std::array<quint8, Size * 4> rgba{};
    std::array<quint8, 4> below{}, above{}, missing{};
    // Entry k holds colorAt/opacityAt at domainMin + k/255 × span.
    static VolumeTransferTable fromModel(const TransferFunctionModel &model);
    // No transfer model: a gray ramp over center ± width/2 whose opacity
    // equals its gray level.
    static VolumeTransferTable fromWindow(double center, double width);
    // Straight RGBA in 0…1: below/above/missing entries outside the domain.
    void lookup(double value, double out[4]) const;
    QImage image() const; // 256×1 RGBA8888, for the GPU upload
};

enum class VolumeRenderMode { Composite = 0, MaximumIntensity = 1 };

struct VolumeRayParams {
    double yawDegrees = 30, pitchDegrees = 20, zoom = 1;
    double samplingRate = 2; // samples per smallest voxel spacing
    double clipLow[3] = {0, 0, 0}, clipHigh[3] = {1, 1, 1};
    VolumeRenderMode mode = VolumeRenderMode::Composite;
    VoxelVolume::Interpolation interpolation = VoxelVolume::Linear;
    int channel = 0, frame = 0;
};

// Orthographic ray geometry for one view size, in continuous voxel indices.
// The ray through view pixel centre (px, py) starts at
//   origin = centre + (px − width/2)·right + (py − height/2)·down
// and advances by `direction` per world unit; samples sit at
//   t_k = tNear + (k + ½)·step, k = 0 … while t_k < tFar and k < maximumSteps.
struct VolumeRayGeometry {
    bool valid = false;
    double centre[3] = {0, 0, 0};
    double right[3] = {0, 0, 0};
    double down[3] = {0, 0, 0};
    double direction[3] = {0, 0, 0};
    double boxLow[3] = {0, 0, 0}, boxHigh[3] = {0, 0, 0};
    double sizeInverse[3] = {1, 1, 1};
    double step = 1;       // world units between samples
    double exponent = 0.5; // opacity correction: 1 − (1 − α)^exponent
    double width = 0, height = 0;
    double pixelsPerUnit = 0;
    int maximumSteps = 0;
    static VolumeRayGeometry compute(const VoxelVolume &volume, const VolumeRayParams &params,
                                     const QSizeF &viewSize);
    // False when the ray misses the clipped volume box.
    bool span(double px, double py, double origin[3], double *tNear, double *tFar) const;
};

// Why the GPU path cannot draw a volume; see volumeGpuUnavailableReason().
struct VolumeGpuProbe {
    bool rhi = false;        // an RHI-based scene graph with a live QRhi
    bool shaders = false;    // the compiled .qsb shaders are in the resources
    bool threeDimensionalTextures = false;
    bool formatSupported = false;
    int maximumTextureSide = 0;
};

class VolumeRaycaster {
public:
    static constexpr double TerminationOpacity = 0.99;
    static constexpr int MaximumSteps = 4096;
    // Conservative 3D texture side: QRhi reports only the 2D limit and
    // GL_MAX_3D_TEXTURE_SIZE is 2048 on current desktop drivers.
    static constexpr int MaximumTextureSide = 2048;
    // Premultiplied ARGB32, one ray per pixel centre. Missing samples
    // (unavailable chunks, NaN) composite the table's `missing` entry, which
    // is transparent unless the transfer function sets it; maximum intensity
    // ignores them. `canceled`, when given, is polled before every ray: a
    // true answer abandons the frame and returns a null image (D-261).
    static QImage render(const VoxelVolume &volume, const VolumeTransferTable &table,
                         const VolumeRayParams &params, const QSize &size,
                         const std::function<bool()> &canceled = {});
    // The same with geometry computed earlier (a worker must not read the
    // volume's calibration while the GUI thread may reconfigure it; D-261).
    static QImage render(const VoxelVolume &volume, const VolumeTransferTable &table,
                         const VolumeRayParams &params, const VolumeRayGeometry &geometry,
                         const QSize &size, const std::function<bool()> &canceled = {});
    // Premultiplied RGBA (0…1) of the ray through (px, py).
    static void shade(const VoxelVolume &volume, const VolumeTransferTable &table,
                      const VolumeRayParams &params, const VolumeRayGeometry &geometry,
                      double px, double py, double out[4]);
    // Continuous voxel index where the ray through `point` first reaches 0.5
    // accumulated opacity (Composite) or its maximum value (MaximumIntensity).
    static bool pick(const VoxelVolume &volume, const VolumeTransferTable &table,
                     const VolumeRayParams &params, const QSizeF &viewSize,
                     const QPointF &point, double voxel[3]);
    // Empty when the GPU path can draw `volume`; otherwise a sentence for
    // VolumeView.fallbackReason. `textureBytes` is the packed texture size.
    static QString gpuUnavailableReason(const VoxelVolume *volume, const VolumeGpuProbe &probe,
                                        qint64 budgetBytes, bool referenceRequested);
    static qint64 textureBytes(const VoxelVolume &volume);
    // One channel and frame, tightly packed X-fastest (z slices of y rows) in
    // the volume's voxel type, host byte order, for a 3D texture upload.
    // Reads chunk by chunk through the volume's bounded cache. False (and
    // `out` cleared) when a chunk is missing.
    static bool packTexture(const VoxelVolume &volume, int channel, int frame, QByteArray *out,
                            const std::function<bool()> &canceled = {});
};

} // namespace QindaTK
