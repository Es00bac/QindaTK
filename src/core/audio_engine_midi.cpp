// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioEngine: MIDI audition and launcher commands on the GUI thread
// (WP-064, docs/audio-midi.md). The device-thread side is
// audio_engine_midi_rt.cpp.
#include "audio_engine.h"

#include "audio_engine_p.h"

namespace QindaTK {

bool AudioEngine::auditionNoteOn(int pitch, double velocity)
{
    if (!m_run || m_job || pitch < 0 || pitch > 127) return false;
    MidiCommand command;
    command.kind = MidiCommand::NoteOn;
    command.pitch = pitch;
    command.velocity = Midi::velocityFromUnit(velocity);
    return m_run->processor->postMidi(command);
}

bool AudioEngine::auditionNoteOn16(int pitch, quint16 velocity)
{
    if (!m_run || m_job || pitch < 0 || pitch > 127 || velocity == 0) return false;
    MidiCommand command;
    command.kind = MidiCommand::NoteOn;
    command.pitch = pitch;
    command.velocity = velocity;
    return m_run->processor->postMidi(command);
}

bool AudioEngine::releaseMidi()
{
    if (!m_run) return true; // closeAfterStop already cleared everything
    MidiCommand command;
    command.kind = MidiCommand::Reset;
    return m_run->processor->postMidi(command);
}

bool AudioEngine::auditionNoteOff(int pitch)
{
    if (!m_run || pitch < 0 || pitch > 127) return false;
    MidiCommand command;
    command.kind = MidiCommand::NoteOff;
    command.pitch = pitch;
    return m_run->processor->postMidi(command);
}

bool AudioEngine::allNotesOff()
{
    // Without a stream nothing sounds: the processor cleared every voice
    // when it closed (AudioEngineProcessor::closeAfterStop).
    if (!m_run) return true;
    MidiCommand command;
    command.kind = MidiCommand::AllNotesOff;
    return m_run->processor->postMidi(command);
}

int AudioEngine::auditionVoices() const
{
    return m_run ? m_run->processor->auditionVoices() : 0;
}

int AudioEngine::launchedClips() const
{
    return m_run ? m_run->processor->launchedClips() : 0;
}

quint64 AudioEngine::postMidiCommand(int kind, quint64 lane, quint64 slot, MidiQuantize quantize, QString *error)
{
    const auto refuse = [error](const QString &message) -> quint64 {
        if (error) *error = message;
        return 0;
    };
    if (m_job) return refuse(tr("The audio device is being reconfigured"));
    if (!m_run) return refuse(tr("No audio device is running"));
    // AGENT-GUARD: bounds the device thread's answers (and the Canceled
    // events a request can cause) well inside its 4096-event ring.
    if (m_midiRequests.size() >= kMidiRequestLimit) return refuse(tr("Too many launcher requests are waiting"));
    MidiCommand command;
    command.serial = ++m_midiSerial;
    command.kind = kind;
    command.lane = lane;
    command.slot = slot;
    command.quantize = int(quantize);
    if (!m_run->processor->postMidi(command)) return refuse(tr("The MIDI command queue is full"));
    m_midiRequests.insert(command.serial);
    return command.serial;
}

quint64 AudioEngine::postLaunch(quint64 lane, quint64 slot, MidiQuantize quantize, QString *error)
{
    if (!lane || !slot) {
        if (error) *error = tr("The slot is not on a MIDI track");
        return 0;
    }
    return postMidiCommand(MidiCommand::Launch, lane, slot, quantize, error);
}

quint64 AudioEngine::postLaunchStop(quint64 lane, MidiQuantize quantize, QString *error)
{
    return postMidiCommand(MidiCommand::Stop, lane, 0, quantize, error);
}

quint64 AudioEngine::postRecord(quint64 lane, quint64 slot, MidiQuantize quantize, QString *error)
{
    if (!lane || !slot) {
        if (error) *error = tr("The slot is not on a MIDI track");
        return 0;
    }
    return postMidiCommand(MidiCommand::RecordStart, lane, slot, quantize, error);
}

quint64 AudioEngine::postRecordStop(MidiQuantize quantize, QString *error)
{
    return postMidiCommand(MidiCommand::RecordStop, 0, 0, quantize, error);
}

void AudioEngine::drainMidi(Run &run)
{
    MidiEngineEvent event;
    while (run.processor->takeMidiEvent(event)) {
        if (event.kind == MidiEngineEvent::Queued || event.kind == MidiEngineEvent::StopQueued
            || event.kind == MidiEngineEvent::Rejected || event.kind == MidiEngineEvent::RecordQueued
            || event.kind == MidiEngineEvent::RecordStopped
            || (event.kind == MidiEngineEvent::Stopped && event.slot == 0))
            m_midiRequests.remove(event.serial); // the one answer of that command
        if (event.kind >= MidiEngineEvent::RecordQueued)
            emit midiRecordEvent(event.kind, event.serial, event.slot, event.sample, event.tick, event.pitch,
                                 event.velocity, event.code);
        else
            emit midiLaunchEvent(event.kind, event.serial, event.lane, event.slot, event.sample, event.code);
    }
    const int voices = run.processor->auditionVoices();
    const int launched = run.processor->launchedClips();
    if (voices != m_lastAuditionVoices || launched != m_lastLaunchedClips) {
        m_lastAuditionVoices = voices;
        m_lastLaunchedClips = launched;
        emit midiStateChanged();
    }
}

} // namespace QindaTK
