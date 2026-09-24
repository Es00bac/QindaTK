// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_ring.h"

#include <QtGlobal>
#include <atomic>
#include <cstddef>

namespace QindaTK {

// One 10 ms level block of every metered mixer channel, produced by one
// AudioRenderGraph generation (D-113). Plain data, copied by the SPSC ring.
struct AudioChannelMeterFrame {
    static constexpr int kMaxSlots = 128;
    quint64 generation = 0; // the graph whose meterChannels() name the slots
    int frames = 0;
    int slotCount = 0; // never `slots`: a Qt keyword macro
    float peak[kMaxSlots][2] = {};     // post-fader sample peak per lane
    double squares[kMaxSlots][2] = {}; // post-fader sum of squares per lane
};

// AGENT-CONTRACT (WP-062, D-113): the per-channel measurement point of one
// device stream. AudioRenderGraph::render adds each channel's post-fader
// lanes on the device thread; nothing here allocates, locks or waits after
// construction. A block closes after blockFrames() frames, or early when the
// graph generation changes, so a frame never mixes two slot layouts. A full
// ring drops the frame and counts it. AudioEngine drains frames on the GUI
// thread into one AudioMeterModel per channel (AudioEngine::channelMeter).
class AudioChannelMeterSink {
public:
    static constexpr std::size_t kRingFrames = 256; // about 2.5 s of 10 ms blocks
    explicit AudioChannelMeterSink(int sampleRate);
    AudioChannelMeterSink(const AudioChannelMeterSink &) = delete;
    AudioChannelMeterSink &operator=(const AudioChannelMeterSink &) = delete;

    int sampleRate() const { return m_rate; }
    int blockFrames() const { return m_blockFrames; }

    // ---- producer: the device thread ----
    // Starts (or continues) a block for `generation` with `slotCount` channels.
    void begin(quint64 generation, int slotCount) noexcept;
    // Frames left in the open block (at least 1): render splits its chunks here.
    int remaining() const noexcept { return m_blockFrames - m_frame.frames; }
    // Adds samples[i] * gain[i] (gain may be null: unity) to `slot`, `lane`.
    void add(int slot, int lane, const double *samples, const double *gain, int frames) noexcept;
    // Counts `frames` (<= remaining()) into the block; closes it when full.
    void advance(int frames) noexcept;
    // A stopped transport: silent blocks, so the meters fall on audio time.
    void silence(quint64 generation, int slotCount, int frames) noexcept;

    // ---- consumer: one non-realtime thread ----
    bool pop(AudioChannelMeterFrame &frame) noexcept { return m_ring.pop(frame); }
    quint64 droppedFrames() const noexcept { return m_dropped.load(std::memory_order_acquire); }

private:
    void close() noexcept;

    const int m_rate;
    const int m_blockFrames;
    AudioChannelMeterFrame m_frame;
    AudioSpscRing<AudioChannelMeterFrame> m_ring{kRingFrames};
    std::atomic<quint64> m_dropped{0};
};

} // namespace QindaTK
