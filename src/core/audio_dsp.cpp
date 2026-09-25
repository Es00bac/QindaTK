// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_dsp.h"

#include <algorithm>
#include <cmath>

namespace QindaTK::AudioDsp {
namespace {

qint64 ceilDiv(qint64 value, qint64 divisor)
{
    return value >= 0 ? (value + divisor - 1) / divisor : -((-value) / divisor);
}

} // namespace

bool isPowerOfTwo(qint64 value)
{
    return value > 0 && (value & (value - 1)) == 0;
}

bool validFftSize(int size)
{
    return size >= kMinFftSize && size <= kMaxFftSize && isPowerOfTwo(size);
}

double gainFromDecibels(double decibels)
{
    if (!(decibels > kSilenceDb)) return 0.0; // also NaN
    return std::pow(10.0, decibels / 20.0);
}

float amplitudeToDecibels(double amplitude)
{
    constexpr double kFloor = 1e-12; // -240 dB
    return float(20.0 * std::log10(std::max(kFloor, std::fabs(amplitude))));
}

double binFrequency(int bin, int fftSize, int sampleRate)
{
    return fftSize > 0 ? double(bin) * double(sampleRate) / double(fftSize) : 0.0;
}

Fft::Fft(int size)
    : m_size(isPowerOfTwo(size) && size >= 2 ? size : 2)
{
    m_twiddles.resize(size_t(m_size / 2));
    for (int k = 0; k < m_size / 2; ++k)
        m_twiddles[size_t(k)] = std::polar(1.0, -2.0 * kPi * double(k) / double(m_size));
    int bits = 0;
    while ((1 << bits) < m_size) ++bits;
    m_reverse.resize(size_t(m_size));
    for (int i = 0; i < m_size; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        m_reverse[size_t(i)] = r;
    }
}

void Fft::transform(std::complex<double> *data, bool inverse) const noexcept
{
    const int n = m_size;
    for (int i = 0; i < n; ++i) {
        const int j = m_reverse[size_t(i)];
        if (i < j) std::swap(data[i], data[j]);
    }
    for (int length = 2; length <= n; length <<= 1) {
        const int half = length / 2;
        const int step = n / length;
        for (int i = 0; i < n; i += length)
            for (int j = 0; j < half; ++j) {
                std::complex<double> w = m_twiddles[size_t(j * step)];
                if (inverse) w = std::conj(w);
                const std::complex<double> u = data[i + j];
                const std::complex<double> v = data[i + j + half] * w;
                data[i + j] = u + v;
                data[i + j + half] = u - v;
            }
    }
}

void Fft::inverse(std::complex<double> *data) const noexcept
{
    transform(data, true);
    const double scale = 1.0 / double(m_size);
    for (int i = 0; i < m_size; ++i) data[i] *= scale;
}

std::vector<double> hannWindow(int size)
{
    std::vector<double> window(size_t(std::max(0, size)));
    for (int j = 0; j < size; ++j)
        window[size_t(j)] = 0.5 - 0.5 * std::cos(2.0 * kPi * double(j) / double(size));
    return window;
}

FrameAnalyzer::FrameAnalyzer(int fftSize)
    : m_fft(fftSize), m_window(hannWindow(m_fft.size())), m_work(size_t(m_fft.size()))
{
    for (double w : m_window) m_windowSum += w;
}

void FrameAnalyzer::magnitudes(const float *samples, qint64 frames, qint64 centre, float *out) noexcept
{
    const int n = m_fft.size();
    const qint64 first = centre - n / 2;
    for (int j = 0; j < n; ++j) {
        const qint64 at = first + j;
        const double x = at >= 0 && at < frames ? double(samples[at]) : 0.0;
        m_work[size_t(j)] = {x * m_window[size_t(j)], 0.0};
    }
    finish(out);
}

void FrameAnalyzer::magnitudesMixed(const float *planar, int channels, qint64 frames, qint64 centre,
                                    float *out) noexcept
{
    const int n = m_fft.size();
    const qint64 first = centre - n / 2;
    const double scale = channels > 0 ? 1.0 / double(channels) : 0.0;
    for (int j = 0; j < n; ++j) {
        const qint64 at = first + j;
        double x = 0.0;
        if (at >= 0 && at < frames)
            for (int c = 0; c < channels; ++c) x += double(planar[qsizetype(c) * frames + at]);
        m_work[size_t(j)] = {x * scale * m_window[size_t(j)], 0.0};
    }
    finish(out);
}

void FrameAnalyzer::finish(float *out) noexcept
{
    m_fft.forward(m_work.data());
    const int n = m_fft.size();
    const double edge = 1.0 / m_windowSum;
    const double inner = 2.0 / m_windowSum;
    for (int k = 0; k <= n / 2; ++k)
        out[k] = float(std::abs(m_work[size_t(k)]) * (k == 0 || k == n / 2 ? edge : inner));
}

std::optional<std::pair<qint64, qint64>> spectralPatchRange(const SpectralGainSpec &spec, qint64 frames)
{
    if (!validFftSize(spec.fftSize) || spec.start < 0 || spec.end <= spec.start || frames <= 0) return std::nullopt;
    const qint64 hop = spec.fftSize / 4;
    const qint64 f0 = ceilDiv(spec.start, hop);
    const qint64 f1 = ceilDiv(spec.end, hop);
    if (f0 >= f1) return std::nullopt;
    const qint64 first = std::max<qint64>(0, f0 * hop - spec.fftSize / 2);
    const qint64 last = std::min<qint64>(frames, (f1 - 1) * hop + spec.fftSize / 2);
    if (first >= last) return std::nullopt;
    return std::make_pair(first, last);
}

std::vector<std::pair<int, int>> maskBinRanges(const std::vector<SpectralMaskPolygon> &mask, double centre,
                                               int fftSize, int sampleRate)
{
    std::vector<std::pair<int, int>> ranges;
    if (sampleRate <= 0 || fftSize <= 0) return ranges;
    const double binsPerHz = double(fftSize) / double(sampleRate);
    std::vector<double> crossings;
    for (const SpectralMaskPolygon &polygon : mask) {
        crossings.clear();
        const size_t count = polygon.size();
        for (size_t i = 0; i < count && count >= 3; ++i) {
            const SpectralMaskPoint &a = polygon[i];
            const SpectralMaskPoint &b = polygon[(i + 1) % count];
            // Half-open rule: an edge counts when the line x = centre passes
            // between its ends, so a shared vertex is crossed exactly once.
            if ((a.sample <= centre) == (b.sample <= centre)) continue;
            crossings.push_back(a.hz + (centre - a.sample) * (b.hz - a.hz) / (b.sample - a.sample));
        }
        std::sort(crossings.begin(), crossings.end());
        for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
            const int first = int(std::clamp(std::ceil(crossings[i] * binsPerHz), 0.0, double(fftSize / 2) + 1.0));
            const int last = int(std::clamp(std::floor(crossings[i + 1] * binsPerHz), -1.0, double(fftSize / 2)));
            if (first <= last) ranges.emplace_back(first, last);
        }
    }
    return ranges;
}

std::optional<SpectralPatch> spectralGain(const float *samples, qint64 frames, const SpectralGainSpec &spec,
                                          const CancellationToken &cancel)
{
    const auto range = spectralPatchRange(spec, frames);
    if (!range || spec.sampleRate <= 0) return std::nullopt;
    const int n = spec.fftSize;
    const qint64 hop = n / 4;
    const Fft fft(n);
    const std::vector<double> window = hannWindow(n);
    std::vector<std::complex<double>> work(static_cast<size_t>(n));
    std::vector<double> difference(size_t(range->second - range->first), 0.0);
    const double binsPerHz = double(n) / double(spec.sampleRate);
    const int low = int(std::clamp(std::ceil(spec.lowHz * binsPerHz), 0.0, double(n / 2)));
    const int high = int(std::clamp(std::floor(spec.highHz * binsPerHz), -1.0, double(n / 2)));
    const qint64 f0 = ceilDiv(spec.start, hop);
    const qint64 f1 = ceilDiv(spec.end, hop);
    const auto sampleAt = [&](qint64 at) { return at >= 0 && at < frames ? double(samples[at]) : 0.0; };
    const bool masked = !spec.mask.empty();
    std::vector<char> selected(size_t(n / 2 + 1), 0);
    for (qint64 f = f0; f < f1 && (masked || low <= high); ++f) {
        if (cancel.isCancelled()) return std::nullopt;
        const qint64 first = f * hop - n / 2;
        if (masked) {
            // A frame the mask misses adds no difference; skip its FFTs.
            std::fill(selected.begin(), selected.end(), char(0));
            bool any = false;
            for (const auto &[a, b] : maskBinRanges(spec.mask, double(f * hop), n, spec.sampleRate))
                for (int k = a; k <= b; ++k) selected[size_t(k)] = any = true;
            if (!any) continue;
        }
        for (int j = 0; j < n; ++j) work[size_t(j)] = {sampleAt(first + j) * window[size_t(j)], 0.0};
        fft.forward(work.data());
        for (int k = masked ? 0 : low; k <= (masked ? n / 2 : high); ++k) {
            if (masked && !selected[size_t(k)]) continue;
            work[size_t(k)] *= spec.gain;
            if (k != 0 && k != n / 2) work[size_t(n - k)] *= spec.gain;
        }
        fft.inverse(work.data());
        for (int j = 0; j < n; ++j) {
            const qint64 at = first + j;
            if (at < range->first || at >= range->second) continue;
            const double w = window[size_t(j)];
            difference[size_t(at - range->first)] += w * (work[size_t(j)].real() - w * sampleAt(at));
        }
    }
    if (cancel.isCancelled()) return std::nullopt;
    // Σw² over the hop-N/4 frames covering any sample is 1.5 (periodic Hann).
    SpectralPatch patch;
    patch.start = range->first;
    patch.samples.resize(difference.size());
    for (size_t i = 0; i < difference.size(); ++i)
        patch.samples[i] = float(sampleAt(range->first + qint64(i)) + difference[i] / 1.5);
    return patch;
}

} // namespace QindaTK::AudioDsp
