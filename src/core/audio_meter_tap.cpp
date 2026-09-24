// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_meter_tap.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace QindaTK {
namespace {

// Filter state this small is inaudible (below -400 dBFS) and would decay into
// denormals during silence, which are slow on x86; flushed at block ends.
constexpr double kStateFloor = 1e-20;

} // namespace

std::array<AudioBiquad, 2> kWeightingFilters(int sampleRate)
{
    // AGENT-NOTE: analog prototypes behind ITU-R BS.1770-4 Tables 1 and 2
    // (shelf centre, gain and Q; high-pass corner and Q), mapped with the
    // bilinear transform. tst_audio_meter_tap compares 48 kHz with the table.
    const double rate = double(std::max(1, sampleRate));
    AudioBiquad shelf;
    {
        const double f0 = 1681.974450955533;
        const double gainDb = 3.999843853973347;
        const double q = 0.7071752369554196;
        const double k = std::tan(std::numbers::pi * f0 / rate);
        const double vh = std::pow(10.0, gainDb / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        shelf.b0 = (vh + vb * k / q + k * k) / a0;
        shelf.b1 = 2.0 * (k * k - vh) / a0;
        shelf.b2 = (vh - vb * k / q + k * k) / a0;
        shelf.a1 = 2.0 * (k * k - 1.0) / a0;
        shelf.a2 = (1.0 - k / q + k * k) / a0;
    }
    AudioBiquad highPass;
    {
        const double f0 = 38.13547087602444;
        const double q = 0.5003270373238773;
        const double k = std::tan(std::numbers::pi * f0 / rate);
        const double a0 = 1.0 + k / q + k * k;
        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (k * k - 1.0) / a0;
        highPass.a2 = (1.0 - k / q + k * k) / a0;
    }
    return {shelf, highPass};
}

int AudioMeterTap::defaultBlockFrames(int sampleRate)
{
    return std::max(1, int(std::lround(double(sampleRate) / 100.0)));
}

AudioMeterTap::AudioMeterTap(int sampleRate, int channels, int blockFrames)
    : m_rate(std::max(1, sampleRate)),
      m_stride(std::max(0, channels)),
      m_channels(std::clamp(channels, 0, AudioMeterBlock::kMaxChannels)),
      m_blockFrames(blockFrames > 0 ? blockFrames : defaultBlockFrames(sampleRate)),
      m_filters(kWeightingFilters(sampleRate))
{
}

void AudioMeterTap::process(const float *interleaved, int frames) noexcept
{
    if (!interleaved || frames <= 0 || m_channels <= 0) return;
    const AudioBiquad &shelf = m_filters[0];
    const AudioBiquad &pass = m_filters[1];
    for (int frame = 0; frame < frames; ++frame) {
        const float *in = interleaved + std::size_t(frame) * std::size_t(m_stride);
        for (int c = 0; c < m_channels; ++c) {
            float sample = in[c];
            if (!std::isfinite(sample)) {
                // A NaN or infinity would poison the filter state for good;
                // it is measured as silence and reported instead.
                ++m_block.invalid[c];
                sample = 0.0f;
            }
            const float magnitude = std::fabs(sample);
            if (magnitude > m_block.peak[c]) m_block.peak[c] = magnitude;
            const double x = double(sample);
            m_block.squares[c] += x * x;
            double *z = m_state[c];
            const double y = shelf.b0 * x + z[0];
            z[0] = shelf.b1 * x - shelf.a1 * y + z[1];
            z[1] = shelf.b2 * x - shelf.a2 * y;
            const double w = pass.b0 * y + z[2];
            z[2] = pass.b1 * y - pass.a1 * w + z[3];
            z[3] = pass.b2 * y - pass.a2 * w;
            m_block.weighted[c] += w * w;
        }
        if (++m_filled == m_blockFrames) close();
    }
}

void AudioMeterTap::close() noexcept
{
    m_frames += m_filled;
    m_block.endFrame = m_frames;
    m_block.frames = m_filled;
    m_block.channels = m_channels;
    if (!m_ring.push(m_block)) m_dropped.fetch_add(1, std::memory_order_acq_rel);
    for (int c = 0; c < m_channels; ++c)
        for (double &z : m_state[c])
            if (std::fabs(z) < kStateFloor) z = 0.0;
    m_block = AudioMeterBlock{};
    m_filled = 0;
}

} // namespace QindaTK
