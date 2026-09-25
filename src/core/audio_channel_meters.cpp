// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_channel_meters.h"

#include "audio_meter_tap.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {

AudioChannelMeterSink::AudioChannelMeterSink(int sampleRate)
    : m_rate(std::max(1, sampleRate)), m_blockFrames(AudioMeterTap::defaultBlockFrames(sampleRate))
{
}

void AudioChannelMeterSink::begin(quint64 generation, int slotCount) noexcept
{
    // AGENT-GUARD (D-113): slots are numbered per graph; a block that holds
    // frames of another generation is closed first, never continued.
    if (m_frame.frames > 0 && m_frame.generation != generation) close();
    m_frame.generation = generation;
    m_frame.slotCount = std::clamp(slotCount, 0, AudioChannelMeterFrame::kMaxSlots);
}

void AudioChannelMeterSink::add(int slot, int lane, const double *samples, const double *gain, int frames) noexcept
{
    if (slot < 0 || slot >= m_frame.slotCount || lane < 0 || lane >= AudioChannelMeterFrame::kMaxLanes || !samples
        || frames <= 0)
        return;
    float peak = m_frame.peak[slot][lane];
    double squares = m_frame.squares[slot][lane];
    for (int i = 0; i < frames; ++i) {
        const double value = gain ? samples[i] * gain[i] : samples[i];
        if (!std::isfinite(value)) continue; // measured as silence, like AudioMeterTap
        peak = std::max(peak, float(std::fabs(value)));
        squares += value * value;
    }
    m_frame.peak[slot][lane] = peak;
    m_frame.squares[slot][lane] = squares;
}

void AudioChannelMeterSink::advance(int frames) noexcept
{
    if (frames <= 0) return;
    m_frame.frames += frames;
    if (m_frame.frames >= m_blockFrames) close();
}

void AudioChannelMeterSink::silence(quint64 generation, int slotCount, int frames) noexcept
{
    begin(generation, slotCount);
    while (frames > 0) {
        const int count = std::min(frames, remaining());
        advance(count);
        frames -= count;
    }
}

void AudioChannelMeterSink::close() noexcept
{
    if (!m_ring.push(m_frame)) m_dropped.fetch_add(1, std::memory_order_acq_rel);
    const quint64 generation = m_frame.generation;
    const int count = m_frame.slotCount;
    for (int slot = 0; slot < count; ++slot)
        for (int lane = 0; lane < AudioChannelMeterFrame::kMaxLanes; ++lane) {
            m_frame.peak[slot][lane] = 0.0f;
            m_frame.squares[slot][lane] = 0.0;
        }
    m_frame.frames = 0;
    m_frame.generation = generation;
    m_frame.slotCount = count;
}

} // namespace QindaTK
