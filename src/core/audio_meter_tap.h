// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_ring.h"

#include <QtGlobal>
#include <array>
#include <atomic>
#include <cstddef>

namespace QindaTK {

// One level block of one audio stream, measured on the audio thread
// (D-102, docs/audio-measurement.md). Plain data, so the SPSC ring copies it
// without allocating. Amplitudes are linear full scale: 1.0 is 0 dBFS.
struct AudioMeterBlock {
    static constexpr int kMaxChannels = 8;
    qint64 endFrame = 0;                // stream frames measured when the block closed
    int frames = 0;                     // frames in this block (the tap's blockFrames)
    int channels = 0;                   // measured channels
    float peak[kMaxChannels] = {};      // sample peak |x|
    double squares[kMaxChannels] = {};  // sum of x^2
    double weighted[kMaxChannels] = {}; // sum of y^2 after ITU-R BS.1770-4 K-weighting
    int invalid[kMaxChannels] = {};     // non-finite samples, measured as silence
};

// One biquad section, y = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2).
struct AudioBiquad {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

// ITU-R BS.1770-4 K-weighting at `sampleRate`: [0] the pre-filter shelf,
// [1] the RLB high-pass. Derived from the standard's analog prototypes by the
// bilinear transform, so every stream rate gets its own exact filter; at
// 48 kHz the result equals the published Tables 1 and 2.
std::array<AudioBiquad, 2> kWeightingFilters(int sampleRate);

// AGENT-CONTRACT: the engine's realtime measurement point (WP-061). process()
// runs on the device thread after the graph rendered a period: it never
// allocates, frees, locks, waits, does I/O or calls Qt; every buffer exists
// from construction. Closed blocks cross to ONE consumer thread through the
// bounded SPSC ring; a full ring drops the block and counts it, it never
// blocks the producer. AudioMeterModel and AudioLoudnessModel consume blocks.
class AudioMeterTap {
public:
    static constexpr std::size_t kRingBlocks = 1024; // about 10 s of 10 ms blocks
    // `channels` is the interleaved stream width; the first kMaxChannels are
    // measured. blockFrames <= 0 selects defaultBlockFrames(sampleRate).
    AudioMeterTap(int sampleRate, int channels, int blockFrames = 0);
    AudioMeterTap(const AudioMeterTap &) = delete;
    AudioMeterTap &operator=(const AudioMeterTap &) = delete;

    // round(sampleRate / 100): exactly 10 ms at every rate divisible by 100 Hz.
    static int defaultBlockFrames(int sampleRate);
    int sampleRate() const { return m_rate; }
    int channels() const { return m_channels; }
    int blockFrames() const { return m_blockFrames; }

    // ---- producer: the device thread ----
    void process(const float *interleaved, int frames) noexcept;

    // ---- consumer: one non-realtime thread ----
    bool pop(AudioMeterBlock &block) noexcept { return m_ring.pop(block); }
    quint64 droppedBlocks() const noexcept { return m_dropped.load(std::memory_order_acquire); }

private:
    void close() noexcept;

    const int m_rate;
    const int m_stride;
    const int m_channels;
    const int m_blockFrames;
    const std::array<AudioBiquad, 2> m_filters;
    double m_state[AudioMeterBlock::kMaxChannels][4] = {}; // two transposed direct-form II sections
    AudioMeterBlock m_block;
    int m_filled = 0;
    qint64 m_frames = 0;
    AudioSpscRing<AudioMeterBlock> m_ring{kRingBlocks};
    std::atomic<quint64> m_dropped{0};
};

} // namespace QindaTK
