// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "audio_media.h"

#include <QString>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace QindaTK {

// kTileFrames consecutive analysis frames of one channel (or the channel
// mean, channel -1) at one hop: frames [index·kTileFrames, …), each bins()
// amplitude magnitudes (AudioDsp conventions, D-136). A frame depends only on
// its own samples, so tiles computed separately join without seams.
struct AudioSpectrumTile {
    int channel = -1;
    int fftSize = 0;
    qint64 hop = 0;
    qint64 index = 0;
    int frames = 0;
    int bins = 0;
    std::vector<float> magnitudes; // frames × bins
    const float *frame(int i) const { return magnitudes.data() + size_t(i) * size_t(bins); }
    qint64 bytes() const { return qint64(magnitudes.size() * sizeof(float)); }
};
using AudioSpectrumTilePtr = std::shared_ptr<const AudioSpectrumTile>;

struct AudioSpectrogramRequest {
    qint64 start = 0;          // source samples, [start, end)
    qint64 end = 0;
    int columns = 256;
    int rows = 48;
    double minimumHz = 20.0;
    double maximumHz = 20000.0;
    bool logarithmic = true;
    double floorDb = -120.0;   // intensity 0; 0 dBFS is intensity 1
    int channel = -1;          // -1: the mean of every channel
    int fftSize = 2048;
    int hop = 512;
    int maxFrames = 1024;      // analysis frames per request before the hop is strided
};

// Reduced time/frequency image. Row 0 is the lowest band.
struct AudioSpectrogramImage {
    int columns = 0;
    int rows = 0;
    std::vector<float> decibels;     // rows × columns, row-major
    std::vector<qint64> boundaries;  // columns + 1 exact source samples
    std::vector<double> bandEdges;   // rows + 1 frequencies in Hz
    qint64 effectiveHop = 0;         // hop × stride
    int stride = 1;
    int fftSize = 0;
    int sampleRate = 0;
    float at(int column, int row) const { return decibels[size_t(row) * size_t(columns) + size_t(column)]; }
    double intensity(int column, int row, double floorDb) const;
};

namespace AudioSpectrogram {
inline constexpr int kTileFrames = 32;
inline constexpr int kMaxCells = 12000; // SpectrogramView's paint cap
inline constexpr int kMaxRows = 512;

// Validates and clamps a request against a buffer (range, sizes, band limits
// at Nyquist, columns × rows ≤ kMaxCells, stride); returns an error message
// or an empty string. On success `hop` is the effective (strided) hop and
// `stride` receives the stride.
QString normalize(AudioSpectrogramRequest &request, const AudioBuffer &buffer, int *stride);

AudioSpectrumTilePtr computeTile(const AudioBuffer &buffer, int channel, int fftSize, qint64 hop, qint64 index,
                                 const CancellationToken &cancel = {});

// Reduces a normalized request. Column i covers [b_i, b_{i+1}) with
// b_i = start + floor(i·L/C) and takes the per-band maximum over the frames
// centred inside it, or over the frame nearest its centre when none is.
// A band takes the maximum of the bins whose frequency lies in
// [e_r, e_{r+1}) (the top band includes its upper edge), or the bin nearest
// its centre when none does. `tile` returns (or computes) tile `index`.
std::optional<AudioSpectrogramImage> render(const AudioBuffer &buffer, const AudioSpectrogramRequest &request,
                                            int stride,
                                            const std::function<AudioSpectrumTilePtr(qint64 index)> &tile,
                                            const CancellationToken &cancel = {});
} // namespace AudioSpectrogram

} // namespace QindaTK
