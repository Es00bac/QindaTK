// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioEngineProcessor: MIDI on the device thread (WP-064, docs/audio-midi.md).
// Audition voices and the clip launcher. Everything here runs inside
// process()/deviceFailed() or after the device thread was joined, and touches
// only preallocated state, the rings and atomics: no allocation, lock or I/O.
#include "audio_engine_p.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {

void AudioEngineProcessor::applyMidi(const MidiCommand &command) noexcept
{
    switch (command.kind) {
    case MidiCommand::NoteOn: return startVoice(command.pitch, command.velocity);
    case MidiCommand::NoteOff: return releaseVoices(command.pitch);
    case MidiCommand::AllNotesOff: return releaseVoices(-1);
    case MidiCommand::Launch: return launch(command);
    case MidiCommand::Stop: return stopLaunches(command);
    default: return;
    }
}

void AudioEngineProcessor::midiEvent(int kind, quint64 serial, quint64 lane, quint64 slot, qint64 sample,
                                     int code) noexcept
{
    MidiEngineEvent event;
    event.kind = kind;
    event.code = code;
    event.serial = serial;
    event.lane = lane;
    event.slot = slot;
    event.sample = sample;
    m_midiEvents.push(event); // the GUI caps launcher commands in flight (AudioEngine::kMidiRequestLimit)
}

MidiLaunchLane *AudioEngineProcessor::launchLane(quint64 lane, bool create) noexcept
{
    for (int i = 0; i < m_launch.count; ++i)
        if (m_launch.lanes[i].lane == lane) return &m_launch.lanes[i];
    if (!create) return nullptr;
    if (m_launch.count < MidiLaunchState::kLanes) {
        MidiLaunchLane &fresh = m_launch.lanes[m_launch.count++];
        fresh = MidiLaunchLane{};
        fresh.lane = lane;
        return &fresh;
    }
    // Reuse an idle lane (its release tail, if any, is cut).
    for (MidiLaunchLane &candidate : m_launch.lanes)
        if (!candidate.slot && !candidate.queuedSlot && candidate.stopAt < 0) {
            candidate = MidiLaunchLane{};
            candidate.lane = lane;
            return &candidate;
        }
    return nullptr;
}

void AudioEngineProcessor::launch(const MidiCommand &command) noexcept
{
    if (!m_graph) return midiEvent(MidiEngineEvent::Rejected, command.serial, command.lane, command.slot, m_position,
                                   MidiEngineEvent::NoGraph);
    if (m_graph->launchProgram(command.slot, command.lane) < 0)
        return midiEvent(MidiEngineEvent::Rejected, command.serial, command.lane, command.slot, m_position,
                         MidiEngineEvent::NoSlot);
    MidiLaunchLane *lane = launchLane(command.lane, true);
    if (!lane) return midiEvent(MidiEngineEvent::Rejected, command.serial, command.lane, command.slot, m_position,
                                MidiEngineEvent::NoLane);
    // AGENT-CONTRACT (D-153): the boundary comes from the engine clock when
    // the command reaches the device thread, never from the GUI: however late
    // a request arrives, the clip starts on a grid line at or after "now".
    const MidiClock &clock = m_graph->midiClock();
    const qint64 tick = clock.nextGrid(clock.tickAt(m_position, true), MidiQuantize(command.quantize));
    const qint64 start = clock.sampleAt(tick);
    if (lane->queuedSlot)
        midiEvent(MidiEngineEvent::Canceled, lane->queuedSerial, lane->lane, lane->queuedSlot, start);
    if (lane->stopAt >= 0) {
        midiEvent(MidiEngineEvent::Canceled, lane->stopSerial, lane->lane, lane->slot, start);
        lane->stopAt = -1;
    }
    lane->queuedSlot = command.slot;
    lane->queuedSerial = command.serial;
    lane->queuedOrigin = tick;
    lane->queuedStart = start;
    midiEvent(MidiEngineEvent::Queued, command.serial, lane->lane, command.slot, start);
}

void AudioEngineProcessor::stopLane(MidiLaunchLane &lane, quint64 serial, int quantize) noexcept
{
    if (lane.queuedSlot) {
        midiEvent(MidiEngineEvent::Canceled, lane.queuedSerial, lane.lane, lane.queuedSlot, m_position);
        lane.queuedSlot = 0;
    }
    if (!lane.slot || !m_graph) return;
    const MidiClock &clock = m_graph->midiClock();
    const qint64 at = clock.sampleAt(clock.nextGrid(clock.tickAt(m_position, true), MidiQuantize(quantize)));
    if (lane.stopAt >= 0 && lane.stopSerial != serial)
        midiEvent(MidiEngineEvent::Canceled, lane.stopSerial, lane.lane, lane.slot, at);
    lane.stopAt = at;
    lane.stopSerial = serial;
}

void AudioEngineProcessor::stopLaunches(const MidiCommand &command) noexcept
{
    qint64 latest = -1;
    for (int i = 0; i < m_launch.count; ++i) {
        MidiLaunchLane &lane = m_launch.lanes[i];
        if (command.lane && lane.lane != command.lane) continue;
        stopLane(lane, command.serial, command.quantize);
        latest = std::max(latest, lane.stopAt);
    }
    // The answer: when the last playing clip stops, or now when nothing plays.
    if (latest >= 0) midiEvent(MidiEngineEvent::StopQueued, command.serial, command.lane, 0, latest);
    else midiEvent(MidiEngineEvent::Stopped, command.serial, command.lane, 0, m_position);
}

void AudioEngineProcessor::launchTransitions(qint64 position) noexcept
{
    int playing = 0;
    for (int i = 0; i < m_launch.count; ++i) {
        MidiLaunchLane &lane = m_launch.lanes[i];
        if (lane.queuedSlot && lane.queuedStart <= position) {
            if (lane.slot) { // the replaced clip keeps its release tail, cut at the new start
                lane.previousSlot = lane.slot;
                lane.previousOrigin = lane.origin;
                lane.previousStart = lane.start;
                lane.previousStop = lane.queuedStart;
            }
            lane.slot = lane.queuedSlot;
            lane.playSerial = lane.queuedSerial;
            lane.origin = lane.queuedOrigin;
            lane.start = lane.queuedStart;
            lane.queuedSlot = 0;
            midiEvent(MidiEngineEvent::Started, lane.playSerial, lane.lane, lane.slot, lane.start);
        }
        if (lane.stopAt >= 0 && lane.stopAt <= position) {
            if (lane.slot) {
                lane.previousSlot = lane.slot;
                lane.previousOrigin = lane.origin;
                lane.previousStart = lane.start;
                lane.previousStop = lane.stopAt;
                midiEvent(MidiEngineEvent::Stopped, lane.stopSerial, lane.lane, lane.slot, lane.stopAt);
                lane.slot = 0;
            }
            lane.stopAt = -1;
        }
        if (lane.slot) ++playing;
    }
    m_publishedLaunched.store(playing, std::memory_order_release);
}

qint64 AudioEngineProcessor::nextLaunchTransition(qint64 position) const noexcept
{
    qint64 next = std::numeric_limits<qint64>::max();
    for (int i = 0; i < m_launch.count; ++i) {
        const MidiLaunchLane &lane = m_launch.lanes[i];
        if (lane.queuedSlot && lane.queuedStart > position) next = std::min(next, lane.queuedStart);
        if (lane.stopAt > position) next = std::min(next, lane.stopAt);
    }
    return next;
}

void AudioEngineProcessor::endLaunches(bool report) noexcept
{
    for (int i = 0; report && i < m_launch.count; ++i) {
        const MidiLaunchLane &lane = m_launch.lanes[i];
        if (lane.queuedSlot)
            midiEvent(MidiEngineEvent::Canceled, lane.queuedSerial, lane.lane, lane.queuedSlot, m_position,
                      MidiEngineEvent::TransportStopped);
        if (lane.stopAt >= 0)
            midiEvent(MidiEngineEvent::Canceled, lane.stopSerial, lane.lane, lane.slot, m_position,
                      MidiEngineEvent::TransportStopped);
        if (lane.slot)
            midiEvent(MidiEngineEvent::Stopped, lane.playSerial, lane.lane, lane.slot, m_position,
                      MidiEngineEvent::TransportStopped);
    }
    for (MidiLaunchLane &lane : m_launch.lanes) lane = MidiLaunchLane{};
    m_launch.count = 0;
    m_publishedLaunched.store(0, std::memory_order_release);
}

void AudioEngineProcessor::startVoice(int pitch, quint16 velocity) noexcept
{
    if (pitch < 0 || pitch > 127 || velocity == 0) return;
    AuditionVoice *target = nullptr;
    for (AuditionVoice &voice : m_voices)
        if (voice.active && voice.pitch == pitch && voice.releasedAt < 0) target = &voice; // retrigger a held key
    for (AuditionVoice &voice : m_voices)
        if (!target && !voice.active) target = &voice;
    if (!target) { // steal the oldest voice
        target = &m_voices[0];
        for (AuditionVoice &voice : m_voices)
            if (voice.order < target->order) target = &voice;
    }
    target->active = true;
    target->pitch = pitch;
    target->amplitude = MidiTestInstrument::kLevel * double(velocity) / 65535.0;
    target->step = m_auditionStep[pitch];
    target->age = 0;
    target->releasedAt = -1;
    target->order = ++m_voiceOrder;
}

void AudioEngineProcessor::releaseVoices(int pitch) noexcept
{
    for (AuditionVoice &voice : m_voices)
        if (voice.active && voice.releasedAt < 0 && (pitch < 0 || voice.pitch == pitch)) voice.releasedAt = voice.age;
}

// Audition voices sound whether or not the transport plays; they reach the
// first two device outputs directly (a cue path, not the mixer).
void AudioEngineProcessor::renderAudition(float *output, int frames, int channels) noexcept
{
    const int attack = MidiTestInstrument::attackFrames(m_format.sampleRate);
    const int release = MidiTestInstrument::releaseFrames(m_format.sampleRate);
    int sounding = 0;
    for (AuditionVoice &voice : m_voices) {
        if (!voice.active) continue;
        for (int i = 0; i < frames; ++i, ++voice.age) {
            const qint64 released = voice.releasedAt < 0 ? -1 : voice.age - voice.releasedAt;
            if (released >= release) {
                voice.active = false;
                break;
            }
            const double value = voice.amplitude * MidiTestInstrument::envelope(voice.age, released, attack, release)
                                 * std::cos(voice.step * double(voice.age));
            float *frame = output + std::size_t(i) * std::size_t(channels);
            frame[0] += float(value);
            if (channels > 1) frame[1] += float(value);
        }
        if (voice.active) ++sounding;
    }
    m_publishedVoices.store(sounding, std::memory_order_release);
}

void AudioEngineProcessor::clearMidi() noexcept
{
    endLaunches(false);
    for (AuditionVoice &voice : m_voices) voice = AuditionVoice{};
    m_publishedVoices.store(0, std::memory_order_release);
}

} // namespace QindaTK
