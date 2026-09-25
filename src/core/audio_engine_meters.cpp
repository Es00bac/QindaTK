// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioEngine: the GUI-thread side of the meter taps (docs/audio-measurement.md,
// docs/audio-mixer.md). Everything here runs in poll() or a reconfiguration.
#include "audio_engine.h"

#include "audio_engine_p.h"

#include <QSet>
#include <algorithm>

namespace QindaTK {

void AudioEngine::drainMeters(Run &run)
{
    drainChannelMeters(run);
    AudioMeterTap *tap = run.processor ? run.processor->outputTap() : nullptr;
    if (!tap) return;
    AudioMeterBlock block;
    bool measured = false;
    // Bounded: one ring's worth per drain, so a producer can never keep the
    // GUI thread here.
    for (std::size_t n = 0; n < AudioMeterTap::kRingBlocks && tap->pop(block); ++n) {
        m_outputMeter->consume(block);
        m_outputLoudness->consume(block);
        measured = true;
    }
    const quint64 dropped = tap->droppedBlocks();
    const quint64 newlyDropped = dropped > m_meterDropped ? dropped - m_meterDropped : 0;
    m_meterDropped = dropped;
    if (!measured && newlyDropped == 0) return;
    m_outputMeter->publish(newlyDropped);
    m_outputLoudness->publish(newlyDropped);
}

AudioMeterModel *AudioEngine::channelMeter(const QString &channelId)
{
    if (channelId.isEmpty()) return nullptr;
    AudioMeterModel *&model = m_channelMeters[channelId];
    if (!model) model = new AudioMeterModel(this); // parented: QML never deletes it
    return model;
}

void AudioEngine::retainChannelMeters(const QSet<QString> &channels)
{
    for (auto it = m_channelMeters.begin(); it != m_channelMeters.end();) {
        if (channels.contains(it.key())) {
            ++it;
            continue;
        }
        // AGENT-GUARD (WP-062): deleteLater, never delete: a strip may still
        // hold the model until its row goes; QML then reads null.
        it.value()->deleteLater();
        it = m_channelMeters.erase(it);
    }
}

void AudioEngine::registerMeterMap(const AudioRenderGraph &graph)
{
    for (const MeterMap &map : std::as_const(m_meterMaps))
        if (map.generation == graph.generation()) return;
    m_meterMaps.push_back({graph.generation(), graph.meterChannels(), graph.meterLanes()});
    // Bounded: a drain always reaches the newest generations first in time.
    while (m_meterMaps.size() > 64) m_meterMaps.removeFirst();
}

void AudioEngine::switchMeterGeneration(quint64 generation)
{
    m_meterGeneration = generation;
    // AGENT-NOTE (D-113): the ring is FIFO and the device thread adopts graphs
    // in offer order, so no frame of an older generation can still arrive.
    m_meterMaps.removeIf([generation](const MeterMap &map) { return map.generation < generation; });
    const auto current = std::find_if(m_meterMaps.cbegin(), m_meterMaps.cend(),
                                      [generation](const MeterMap &map) { return map.generation == generation; });
    for (auto it = m_channelMeters.cbegin(); it != m_channelMeters.cend(); ++it)
        if (current == m_meterMaps.cend() || !current->channels.contains(it.key()))
            it.value()->configure(0, 0, {}); // the channel left the playing graph
}

void AudioEngine::drainChannelMeters(Run &run)
{
    AudioChannelMeterSink *sink = run.processor ? run.processor->channelMeters() : nullptr;
    if (!sink) return;
    QSet<AudioMeterModel *> touched;
    AudioChannelMeterFrame frame;
    for (std::size_t n = 0; n < AudioChannelMeterSink::kRingFrames && sink->pop(frame); ++n) {
        if (frame.generation != m_meterGeneration) switchMeterGeneration(frame.generation);
        const auto map = std::find_if(m_meterMaps.cbegin(), m_meterMaps.cend(),
                                      [&frame](const MeterMap &entry) { return entry.generation == frame.generation; });
        if (map == m_meterMaps.cend()) continue;
        const int count = std::min(frame.slotCount, int(map->channels.size()));
        for (int slot = 0; slot < count; ++slot) {
            AudioMeterModel *model = m_channelMeters.value(map->channels.at(slot));
            if (!model) continue; // nobody asked for this channel's meter
            const int lanes = std::clamp(map->lanes.value(slot, 2), 1, AudioChannelMeterFrame::kMaxLanes);
            const auto layout = AudioMixer::layoutForLanes(lanes);
            model->configure(sink->sampleRate(), lanes, AudioMixer::layoutName(layout.value_or(AudioLayout::Stereo)),
                             sink->blockFrames());
            AudioMeterBlock block;
            block.frames = frame.frames;
            block.channels = lanes;
            for (int lane = 0; lane < lanes; ++lane) {
                block.peak[lane] = frame.peak[slot][lane];
                block.squares[lane] = frame.squares[slot][lane];
            }
            model->consume(block);
            touched.insert(model);
        }
    }
    const quint64 dropped = sink->droppedFrames();
    const quint64 newlyDropped = dropped > m_channelMetersDropped ? dropped - m_channelMetersDropped : 0;
    m_channelMetersDropped = dropped;
    if (newlyDropped > 0)
        for (AudioMeterModel *model : std::as_const(m_channelMeters)) touched.insert(model);
    for (AudioMeterModel *model : std::as_const(touched)) model->publish(newlyDropped);
}

void AudioEngine::clearChannelMeters()
{
    for (AudioMeterModel *model : std::as_const(m_channelMeters)) model->configure(0, 0, {});
    m_meterMaps.clear();
    m_meterGeneration = 0;
}

} // namespace QindaTK
