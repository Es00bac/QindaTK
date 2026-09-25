// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"

#include <QtGlobal>
#include <complex>
#include <optional>
#include <vector>

namespace QindaTK {

// AGENT-CONTRACT (D-136, docs/audio-editing.md): the spectral conventions
// shared by SpectrogramSource analysis and spectral processing.
// - Frame f of an analysis at hop h is centred on sample c = f * h and reads
//   samples [c - N/2, c + N/2); samples outside the source are zero.
// - The window is the periodic Hann w[j] = 0.5 - 0.5 cos(2πj/N); w[N/2] = 1
//   sits on the centre sample. Σw = N/2.
// - Bin k (0..N/2) is the frequency k * sampleRate / N.
// - Magnitudes are amplitudes: 2|X[k]|/Σw for 0 < k < N/2 and |X[k]|/Σw at
//   DC and Nyquist, so a full-scale sine centred on a bin reads 1.0 (0 dBFS).
namespace AudioDsp {

inline constexpr int kMinFftSize = 64;
inline constexpr int kMaxFftSize = 16384;
inline constexpr double kPi = 3.14159265358979323846;
// A spectral gain at or below this many dB is exact zero (removal).
inline constexpr double kSilenceDb = -150.0;

bool isPowerOfTwo(qint64 value);
bool validFftSize(int size);
double gainFromDecibels(double decibels);
float amplitudeToDecibels(double amplitude); // floor -240 dB
double binFrequency(int bin, int fftSize, int sampleRate);

// Iterative radix-2 complex FFT with precomputed twiddles. forward() uses
// e^{-2πikn/N}; inverse() uses e^{+2πikn/N} and scales by 1/N, so
// inverse(forward(x)) == x to rounding.
class Fft {
public:
    explicit Fft(int size);
    int size() const { return m_size; }
    void forward(std::complex<double> *data) const noexcept { transform(data, false); }
    void inverse(std::complex<double> *data) const noexcept;
private:
    void transform(std::complex<double> *data, bool inverse) const noexcept;
    int m_size = 0;
    std::vector<std::complex<double>> m_twiddles;
    std::vector<int> m_reverse;
};

std::vector<double> hannWindow(int size); // periodic

// Analysis state reused across frames by one thread.
class FrameAnalyzer {
public:
    explicit FrameAnalyzer(int fftSize);
    int fftSize() const { return m_fft.size(); }
    int bins() const { return m_fft.size() / 2 + 1; }
    const std::vector<double> &window() const { return m_window; }
    // Amplitude magnitudes of the frame centred on `centre` into `out` (bins()).
    // `samples` is one channel of `frames` samples.
    void magnitudes(const float *samples, qint64 frames, qint64 centre, float *out) noexcept;
    // Same, for the mean of `channels` planar channels.
    void magnitudesMixed(const float *planar, int channels, qint64 frames, qint64 centre, float *out) noexcept;
private:
    void finish(float *out) noexcept;
    Fft m_fft;
    std::vector<double> m_window;
    std::vector<std::complex<double>> m_work;
    double m_windowSum = 0.0;
};

// ---- spectral processing (D-137) ----
struct SpectralGainSpec {
    qint64 start = 0;      // frames whose centre lies in [start, end) are processed
    qint64 end = 0;
    double lowHz = 0.0;    // bins with lowHz <= k * rate / N <= highHz
    double highHz = 0.0;
    double gain = 0.0;     // linear factor applied to those bins
    int fftSize = 2048;    // hop is fftSize / 4
    int sampleRate = 48000;
};
struct SpectralPatch {
    qint64 start = 0;           // first replaced sample
    std::vector<float> samples; // replacement for [start, start + size)
};
// First and last+1 sample a spectral edit can change; empty when no frame
// centre falls in the range.
std::optional<std::pair<qint64, qint64>> spectralPatchRange(const SpectralGainSpec &spec, qint64 frames);
// Short-time Fourier processing with Hann analysis and synthesis windows at
// hop N/4 (Σw² = 1.5). Only the processed frames' differences are added, so
// every sample outside the returned patch is untouched and unprocessed bins
// reconstruct to rounding. Returns nullopt when cancelled or when no frame
// centre lies in the range.
std::optional<SpectralPatch> spectralGain(const float *samples, qint64 frames, const SpectralGainSpec &spec,
                                          const CancellationToken &cancel = {});

} // namespace AudioDsp
} // namespace QindaTK
