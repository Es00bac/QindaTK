// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_spectrogram.h"

#include "audio_dsp.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AudioSpectrogram", text);
}

qint64 ceilDiv(qint64 value, qint64 divisor)
{
    return value >= 0 ? (value + divisor - 1) / divisor : -((-value) / divisor);
}

qint64 framesIn(qint64 start, qint64 end, qint64 hop)
{
    return ceilDiv(end, hop) - ceilDiv(start, hop);
}

} // namespace

double AudioSpectrogramImage::intensity(int column, int row, double floorDb) const
{
    const double value = at(column, row);
    return std::clamp((value - floorDb) / -floorDb, 0.0, 1.0);
}

QString AudioSpectrogram::normalize(AudioSpectrogramRequest &request, const AudioBuffer &buffer, int *stride)
{
    if (!AudioDsp::validFftSize(request.fftSize)) return tr("The FFT size must be a power of two from 64 to 16384");
    if (request.hop < 1 || request.hop > request.fftSize) return tr("The hop must be from 1 sample to the FFT size");
    if (request.channel < -1 || request.channel >= buffer.channels) return tr("The source has no such channel");
    if (buffer.sampleRate <= 0 || buffer.frames <= 0) return tr("The source has no samples");
    request.start = std::clamp<qint64>(request.start, 0, buffer.frames);
    request.end = std::clamp<qint64>(request.end, request.start, buffer.frames);
    if (request.end <= request.start) return tr("The analysis range is empty");
    request.maximumHz = std::min(request.maximumHz, buffer.sampleRate / 2.0);
    if (!(request.minimumHz >= 0.0) || !(request.maximumHz > request.minimumHz))
        return tr("The frequency range must rise from zero or above to at most half the sample rate");
    if (request.logarithmic && request.minimumHz <= 0.0) return tr("A logarithmic axis needs a lowest frequency above zero");
    request.rows = std::clamp(request.rows, 1, kMaxRows);
    request.columns = int(std::clamp<qint64>(request.columns, 1, std::min<qint64>(kMaxCells, request.end - request.start)));
    if (qint64(request.columns) * request.rows > kMaxCells) request.columns = std::max(1, kMaxCells / request.rows);
    request.floorDb = std::clamp(request.floorDb, -240.0, -1.0);
    request.maxFrames = std::clamp(request.maxFrames, 16, 8192);
    // AGENT-NOTE (D-136): a wide range is analysed at a power-of-two multiple
    // of the hop, so a request never computes more than maxFrames frames and
    // the strided frames are still frames of the unstrided analysis.
    int s = 1;
    while (framesIn(request.start, request.end, qint64(request.hop) * s) > request.maxFrames && s < (1 << 20)) s *= 2;
    request.hop *= s;
    if (stride) *stride = s;
    return {};
}

AudioSpectrumTilePtr AudioSpectrogram::computeTile(const AudioBuffer &buffer, int channel, int fftSize, qint64 hop,
                                                   qint64 index, const CancellationToken &cancel)
{
    if (!AudioDsp::validFftSize(fftSize) || hop < 1 || index < 0 || channel >= buffer.channels) return nullptr;
    auto tile = std::make_shared<AudioSpectrumTile>();
    tile->channel = channel;
    tile->fftSize = fftSize;
    tile->hop = hop;
    tile->index = index;
    tile->frames = kTileFrames;
    tile->bins = fftSize / 2 + 1;
    tile->magnitudes.resize(size_t(tile->frames) * size_t(tile->bins));
    AudioDsp::FrameAnalyzer analyzer(fftSize);
    for (int i = 0; i < kTileFrames; ++i) {
        if (cancel.isCancelled()) return nullptr;
        const qint64 centre = (index * kTileFrames + i) * hop;
        float *out = tile->magnitudes.data() + size_t(i) * size_t(tile->bins);
        if (channel < 0) analyzer.magnitudesMixed(buffer.samples.data(), buffer.channels, buffer.frames, centre, out);
        else analyzer.magnitudes(buffer.channel(channel), buffer.frames, centre, out);
    }
    return tile;
}

std::optional<AudioSpectrogramImage> AudioSpectrogram::render(
    const AudioBuffer &buffer, const AudioSpectrogramRequest &request, int stride,
    const std::function<AudioSpectrumTilePtr(qint64 index)> &tile, const CancellationToken &cancel)
{
    AudioSpectrogramImage image;
    image.columns = request.columns;
    image.rows = request.rows;
    image.effectiveHop = request.hop;
    image.stride = stride;
    image.fftSize = request.fftSize;
    image.sampleRate = buffer.sampleRate;
    const qint64 length = request.end - request.start;
    image.boundaries.resize(size_t(image.columns) + 1);
    for (int i = 0; i <= image.columns; ++i)
        image.boundaries[size_t(i)] = request.start + qint64(i) * length / image.columns;
    image.bandEdges.resize(size_t(image.rows) + 1);
    for (int r = 0; r <= image.rows; ++r) {
        const double fraction = double(r) / double(image.rows);
        image.bandEdges[size_t(r)] = r == 0 ? request.minimumHz
            : r == image.rows ? request.maximumHz
            : request.logarithmic ? request.minimumHz * std::pow(request.maximumHz / request.minimumHz, fraction)
            : request.minimumHz + (request.maximumHz - request.minimumHz) * fraction;
    }
    // Bins per band.
    const int lastBin = request.fftSize / 2;
    const double binHz = double(buffer.sampleRate) / double(request.fftSize);
    constexpr double kEpsilon = 1e-9;
    std::vector<int> from(size_t(image.rows)), to(size_t(image.rows));
    for (int r = 0; r < image.rows; ++r) {
        const double lo = image.bandEdges[size_t(r)] / binHz;
        const double hi = image.bandEdges[size_t(r) + 1] / binHz;
        int a = int(std::ceil(lo - kEpsilon));
        int b = r + 1 == image.rows ? int(std::floor(hi + kEpsilon)) : int(std::ceil(hi - kEpsilon)) - 1;
        a = std::clamp(a, 0, lastBin);
        b = std::clamp(b, -1, lastBin);
        if (a > b) {
            const double centre = request.logarithmic ? std::sqrt(lo * hi) : (lo + hi) / 2.0;
            a = b = std::clamp(int(std::lround(centre)), 0, lastBin);
        }
        from[size_t(r)] = a;
        to[size_t(r)] = b;
    }
    const qint64 hop = request.hop;
    std::vector<float> peak(size_t(image.rows));
    image.decibels.resize(size_t(image.rows) * size_t(image.columns));
    AudioSpectrumTilePtr current;
    for (int i = 0; i < image.columns; ++i) {
        if (cancel.isCancelled()) return std::nullopt;
        const qint64 a = image.boundaries[size_t(i)];
        const qint64 b = image.boundaries[size_t(i) + 1];
        qint64 first = ceilDiv(a, hop);
        qint64 last = ceilDiv(b, hop);
        if (first >= last) {
            first = (a + b + hop) / (2 * hop); // the frame nearest the column centre
            last = first + 1;
        }
        std::fill(peak.begin(), peak.end(), 0.0f);
        for (qint64 f = first; f < last; ++f) {
            const qint64 index = f / kTileFrames;
            if (!current || current->index != index) current = tile(index);
            if (!current) return std::nullopt; // cancelled while computing
            const float *magnitudes = current->frame(int(f % kTileFrames));
            for (int r = 0; r < image.rows; ++r)
                for (int k = from[size_t(r)]; k <= to[size_t(r)]; ++k) peak[size_t(r)] = std::max(peak[size_t(r)], magnitudes[k]);
        }
        for (int r = 0; r < image.rows; ++r)
            image.decibels[size_t(r) * size_t(image.columns) + size_t(i)] = AudioDsp::amplitudeToDecibels(peak[size_t(r)]);
    }
    return image;
}

} // namespace QindaTK
