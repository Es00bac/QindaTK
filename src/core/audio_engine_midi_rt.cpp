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
    case MidiCommand::NoteOn:
        if (command.pitch < 0 || command.pitch > 127 || command.velocity == 0) return;
        startVoice(command.pitch, command.velocity);
        return recordNote(command.pitch, command.velocity);
    case MidiCommand::NoteOff:
        releaseVoices(command.pitch);
        return recordNote(command.pitch, 0);
    case MidiCommand::AllNotesOff:
        releaseVoices(-1);
        return recordNote(-1, 0);
    case MidiCommand::Launch: return launch(command);
    case MidiCommand::Stop: return stopLaunches(command);
    case MidiCommand::Reset: return resetMidi();
    case MidiCommand::RecordStart: return startRecord(command);
    case MidiCommand::RecordStop:
        if (!m_midiRecord.armed)
            return recordEvent(MidiEngineEvent::RecordStopped, command.serial, 0, 0, 0, MidiEngineEvent::NotRecording);
        return stopRecord(command.serial, command.quantize, MidiEngineEvent::None);
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

void AudioEngineProcessor::recordEvent(int kind, quint64 serial, qint64 tick, int pitch, quint16 velocity,
                                       int code) noexcept
{
    MidiEngineEvent event;
    event.kind = kind;
    event.code = code;
    event.serial = serial;
    event.lane = m_midiRecord.lane;
    event.slot = m_midiRecord.slot;
    event.sample = kind == MidiEngineEvent::RecordQueued ? m_midiRecord.startSample : m_position;
    event.tick = tick;
    event.pitch = pitch;
    event.velocity = velocity;
    m_midiEvents.push(event); // a full ring drops a note; the take reports what arrived
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
    lane->queuedQuantize = command.quantize;
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
    lane.stopQuantize = quantize;
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

// AGENT-GUARD (D-153): launcher and recording state is kept in device
// samples, so every transport jump re-bases it here. A playing clip keeps its
// musical progress (its origin moves by the jump's ticks and it is heard from
// the new position; notes sounding across the jump are cut). A waiting launch
// or stop is re-quantized from the new position — "the next grid line at or
// after now" — and reported again, so a boundary at or past the loop end
// fires after the wrap instead of never. A running take keeps counting forward.
void AudioEngineProcessor::rebaseMidi(qint64 from, qint64 to) noexcept
{
    if (from == to || !m_graph) return;
    const MidiClock &clock = m_graph->midiClock();
    const qint64 shift = clock.nearestTick(to) - clock.nearestTick(from);
    const qint64 now = clock.tickAt(to, true);
    for (int i = 0; i < m_launch.count; ++i) {
        MidiLaunchLane &lane = m_launch.lanes[i];
        lane.previousSlot = 0;
        if (lane.slot) {
            lane.origin += shift;
            lane.start = to;
        }
        if (lane.queuedSlot) {
            lane.queuedOrigin = clock.nextGrid(now, MidiQuantize(lane.queuedQuantize));
            lane.queuedStart = clock.sampleAt(lane.queuedOrigin);
            midiEvent(MidiEngineEvent::Queued, lane.queuedSerial, lane.lane, lane.queuedSlot, lane.queuedStart);
        }
        if (lane.stopAt >= 0) {
            lane.stopAt = clock.sampleAt(clock.nextGrid(now, MidiQuantize(lane.stopQuantize)));
            midiEvent(MidiEngineEvent::StopQueued, lane.stopSerial, lane.lane, 0, lane.stopAt);
        }
    }
    if (m_midiRecord.armed) {
        if (from >= m_midiRecord.startSample) {
            m_midiRecord.startTick += shift;
            m_midiRecord.startSample = std::min(m_midiRecord.startSample, to);
        } else {
            m_midiRecord.startTick = clock.nextGrid(now, MidiQuantize(m_midiRecord.quantize));
            m_midiRecord.startSample = clock.sampleAt(m_midiRecord.startTick);
            recordEvent(MidiEngineEvent::RecordQueued, m_midiRecord.serial, 0);
        }
    }
}

void AudioEngineProcessor::resetMidi() noexcept
{
    endLaunches(true);
    releaseVoices(-1);
    if (m_midiRecord.armed) stopRecord(m_midiRecord.serial, int(MidiQuantize::None), MidiEngineEvent::Detached);
}

void AudioEngineProcessor::startRecord(const MidiCommand &command) noexcept
{
    const auto reject = [&](int code) {
        midiEvent(MidiEngineEvent::Rejected, command.serial, command.lane, command.slot, m_position, code);
    };
    if (!m_graph) return reject(MidiEngineEvent::NoGraph);
    if (m_graph->launchProgram(command.slot, command.lane) < 0) return reject(MidiEngineEvent::NoSlot);
    if (m_midiRecord.armed) return reject(MidiEngineEvent::RecordBusy);
    const MidiClock &clock = m_graph->midiClock();
    m_midiRecord.armed = true;
    m_midiRecord.serial = command.serial;
    m_midiRecord.lane = command.lane;
    m_midiRecord.slot = command.slot;
    m_midiRecord.quantize = command.quantize;
    m_midiRecord.startTick = clock.nextGrid(clock.tickAt(m_position, true), MidiQuantize(command.quantize));
    m_midiRecord.startSample = clock.sampleAt(m_midiRecord.startTick);
    recordEvent(MidiEngineEvent::RecordQueued, command.serial, 0);
}

// The take ends now; its length runs to the next grid line of `quantize`
// (0 when the start boundary was never reached: nothing was recorded).
void AudioEngineProcessor::stopRecord(quint64 serial, int quantize, int code) noexcept
{
    qint64 length = 0;
    if (m_graph && m_position >= m_midiRecord.startSample) {
        const MidiClock &clock = m_graph->midiClock();
        const qint64 now = std::max(clock.tickAt(m_position, true), m_midiRecord.startTick + 1);
        length = clock.nextGrid(now, MidiQuantize(quantize)) - m_midiRecord.startTick;
    }
    recordEvent(MidiEngineEvent::RecordStopped, serial, length, 0, 0, code);
    m_midiRecord = MidiRecordState{};
}

// AGENT-CONTRACT (D-153): the engine owns MIDI recording time. An audition
// note (a MIDI input device or the host) is stamped with the transport
// position of the device period that applies it, as a take-relative tick.
void AudioEngineProcessor::recordNote(int pitch, quint16 velocity) noexcept
{
    if (!m_midiRecord.armed || !m_playing || !m_graph || m_position < m_midiRecord.startSample) return;
    const qint64 tick = std::max<qint64>(0, m_graph->midiClock().nearestTick(m_position) - m_midiRecord.startTick);
    recordEvent(MidiEngineEvent::NoteRecorded, m_midiRecord.serial, tick, pitch, velocity);
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
    m_midiRecord = MidiRecordState{}; // the GUI keeps the notes it already received (MidiLauncherModel::reset)
    for (AuditionVoice &voice : m_voices) voice = AuditionVoice{};
    m_publishedVoices.store(0, std::memory_order_release);
}

} // namespace QindaTK
