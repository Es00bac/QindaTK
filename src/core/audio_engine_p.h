// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_channel_meters.h"
#include "audio_device.h"
#include "audio_engine.h"
#include "audio_graph.h"
#include "audio_meter_tap.h"
#include "audio_ring.h"

#include <QPair>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>

namespace QindaTK {

// GUI -> realtime. Plain data so the ring copies it without allocating.
struct EngineCommand {
    enum Kind : int { Play, Stop, Seek, Record, Loop };
    quint64 serial = 0;
    int kind = Play;
    qint64 position = 0;
    qint64 rangeStart = 0;
    qint64 rangeEnd = 0;
    bool enabled = false;
};

// Realtime -> GUI.
struct EngineEvent {
    enum Kind : int { Ack, RecordBegin, RecordEnd, DeviceLost };
    enum Code : int { None, InvalidPosition, InvalidRange, NoInput, TakeBusy, Lost };
    int kind = Ack;
    int code = None;
    quint64 serial = 0;          // command serial, or take serial
    bool accepted = false;
    qint64 position = 0;         // transport sample when applied / first captured sample
    qint64 frames = 0;           // RecordEnd: frames captured into the take (code Lost: device failed)
    qint64 dropped = 0;          // RecordEnd: frames lost to a full capture ring
    const char *reason = nullptr; // DeviceLost: static text
    qint64 jumpsDropped = 0;      // RecordEnd: discontinuities the jump ring could not hold
};

// Realtime -> GUI (WP-065): the transport jumped while a take was open (a
// loop wrap or a seek). File frame `frame` of take `take` was captured at
// transport sample `position`; the frames before it continue the previous
// position. Per-pass takes are cut from these.
struct RecordJump {
    quint64 take = 0;
    qint64 frame = 0;
    qint64 position = 0;
};

// GUI -> realtime MIDI (WP-064): audition notes and the clip launcher.
struct MidiCommand {
    enum Kind : int { NoteOn, NoteOff, AllNotesOff, Launch, Stop };
    quint64 serial = 0;
    int kind = NoteOn;
    int pitch = 0;
    quint16 velocity = 0;
    quint64 lane = 0;   // Launch/Stop: Midi::key(track id); Stop with 0 stops every lane
    quint64 slot = 0;   // Launch: Midi::key(slot id)
    int quantize = int(MidiQuantize::Bar);
};

// Realtime -> GUI launcher events. Every Launch/Stop command gets exactly one
// answer (Queued, StopQueued, Stopped or Rejected) with its serial; Started
// and Stopped report transitions later, Canceled a request that was replaced.
struct MidiEngineEvent {
    enum Kind : int { Queued, Started, StopQueued, Stopped, Canceled, Rejected };
    enum Code : int { None, NoGraph, NoSlot, NoLane, TransportStopped };
    int kind = Queued;
    int code = None;
    quint64 serial = 0;
    quint64 lane = 0;
    quint64 slot = 0;
    qint64 sample = 0;
};

// One audition note of the built-in test instrument (MidiTestInstrument).
struct AuditionVoice {
    bool active = false;
    int pitch = 0;
    double amplitude = 0.0;
    double step = 0.0;
    qint64 age = 0;        // samples since note on
    qint64 releasedAt = -1; // age at note off; -1 while held
    quint64 order = 0;
};

struct EngineTransportState {
    qint64 position = 0;
    bool loopEnabled = false;
    qint64 loopStart = 0;
    qint64 loopEnd = 0;
};

// AGENT-CONTRACT: one processor per open stream. process()/deviceFailed()
// run on the device thread and touch only preallocated state, the lock-free
// rings and atomics; every other member runs on the GUI or worker thread.
// Graph lifetime: offer() publishes a raw pointer the GUI keeps alive; the
// device thread adopts it at a block boundary and reports its generation,
// after which the GUI may release every older graph (docs/audio-engine.md).
class AudioEngineProcessor final : public AudioIoCallback {
public:
    static constexpr std::size_t kCommandCapacity = 64;
    static constexpr std::size_t kEventCapacity = 256;
    static constexpr quint64 kOpenTake = std::numeric_limits<quint64>::max();
    static constexpr std::size_t kMidiCommandCapacity = 256;
    static constexpr std::size_t kMidiEventCapacity = 4096;
    static constexpr int kAuditionVoices = 16;
    static constexpr std::size_t kJumpCapacity = 1024;

    AudioEngineProcessor(const AudioStreamFormat &format, const EngineTransportState &initial,
                         double captureSeconds = 4.0);

    // ---- GUI / worker side ----
    const AudioStreamFormat &format() const { return m_format; }
    bool post(const EngineCommand &command) { return m_commands.push(command); }
    bool takeEvent(EngineEvent &event) { return m_events.pop(event); }
    bool takeJump(RecordJump &jump) { return m_jumps.pop(jump); }
    // Returns a previously offered graph that was never adopted (releasable now).
    const AudioRenderGraph *offer(const AudioRenderGraph *graph);
    quint64 adoptedGeneration() const { return m_adopted.load(std::memory_order_acquire); }
    qint64 position() const { return m_publishedPosition.load(std::memory_order_acquire); }
    bool playing() const { return m_publishedPlaying.load(std::memory_order_acquire); }
    bool recording() const { return m_publishedRecording.load(std::memory_order_acquire); }
    // MIDI (WP-064): audition and launcher commands, launcher events.
    bool postMidi(const MidiCommand &command) { return m_midiCommands.push(command); }
    bool takeMidiEvent(MidiEngineEvent &event) { return m_midiEvents.pop(event); }
    int auditionVoices() const { return m_publishedVoices.load(std::memory_order_acquire); }
    int launchedClips() const { return m_publishedLaunched.load(std::memory_order_acquire); }
    // Only after the device thread was joined: ends an open take and stops.
    void closeAfterStop();
    EngineTransportState stateAfterStop() const;

    // ---- meter consumer (GUI thread) ----
    // Output measurement after each rendered period; null without output.
    AudioMeterTap *outputTap() { return m_outputTap.get(); }
    // Post-fader levels of every mixer channel (WP-062); null without output.
    AudioChannelMeterSink *channelMeters() { return m_channelMeters.get(); }

    // ---- take writer side ----
    AudioSpscRing<float> &capture() { return m_capture; }
    quint64 takeSerial() const { return m_takeSerial.load(std::memory_order_acquire); }
    quint64 takeEnd() const { return m_takeEndTotal.load(std::memory_order_acquire); }
    quint64 pushedFrames() const { return m_pushedPublished.load(std::memory_order_acquire); }
    void markWritten(quint64 serial) { m_writtenSerial.store(serial, std::memory_order_release); }

    // ---- device thread ----
    void process(const float *input, float *output, int frames) noexcept override;
    void deviceFailed(const char *reason) noexcept override;

private:
    void apply(const EngineCommand &command) noexcept;
    void ack(const EngineCommand &command, bool accepted, int code = EngineEvent::None) noexcept;
    void beginTake() noexcept;
    void endTake(bool lost = false) noexcept;
    void captureBlock(const float *input, int frames, qint64 position) noexcept;
    void recordJump(qint64 frame, qint64 position) noexcept;
    void publish() noexcept;
    // MIDI, audio_engine_midi_rt.cpp.
    void applyMidi(const MidiCommand &command) noexcept;
    void midiEvent(int kind, quint64 serial, quint64 lane, quint64 slot, qint64 sample, int code = 0) noexcept;
    MidiLaunchLane *launchLane(quint64 lane, bool create) noexcept;
    void launch(const MidiCommand &command) noexcept;
    void stopLane(MidiLaunchLane &lane, quint64 serial, int quantize) noexcept;
    void stopLaunches(const MidiCommand &command) noexcept;
    void launchTransitions(qint64 position) noexcept;
    qint64 nextLaunchTransition(qint64 position) const noexcept;
    void endLaunches(bool report) noexcept;
    void startVoice(int pitch, quint16 velocity) noexcept;
    void releaseVoices(int pitch) noexcept; // -1: every voice
    void renderAudition(float *output, int frames, int channels) noexcept;
    void clearMidi() noexcept;

    const AudioStreamFormat m_format;
    AudioSpscRing<EngineCommand> m_commands{kCommandCapacity};
    AudioSpscRing<EngineEvent> m_events{kEventCapacity};
    AudioSpscRing<float> m_capture;
    std::unique_ptr<double[]> m_scratch;
    std::unique_ptr<AudioMeterTap> m_outputTap;
    std::unique_ptr<AudioChannelMeterSink> m_channelMeters;
    std::atomic<const AudioRenderGraph *> m_pending{nullptr};
    std::atomic<quint64> m_adopted{0};
    const AudioRenderGraph *m_graph = nullptr;
    // Realtime-owned transport state, published through atomics.
    qint64 m_position = 0;
    bool m_playing = false;
    bool m_recording = false;
    bool m_loopEnabled = false;
    qint64 m_loopStart = 0;
    qint64 m_loopEnd = 0;
    std::atomic<qint64> m_publishedPosition{0};
    std::atomic<bool> m_publishedPlaying{false};
    std::atomic<bool> m_publishedRecording{false};
    // Takes: frames enter the capture ring only while a take is open.
    bool m_takeAnnounced = false;
    qint64 m_takeFrames = 0;
    qint64 m_takeDropped = 0;
    qint64 m_takeJumpsDropped = 0;
    // AGENT-GUARD: its own ring, so wraps of a short loop can never crowd out
    // the command acknowledgments the event ring guarantees room for.
    AudioSpscRing<RecordJump> m_jumps{kJumpCapacity};
    quint64 m_pushed = 0;
    std::atomic<quint64> m_pushedPublished{0};
    std::atomic<quint64> m_takeSerial{0};
    std::atomic<quint64> m_takeEndTotal{0};
    std::atomic<quint64> m_writtenSerial{0};
    // MIDI: launcher lanes and audition voices, realtime-owned.
    AudioSpscRing<MidiCommand> m_midiCommands{kMidiCommandCapacity};
    AudioSpscRing<MidiEngineEvent> m_midiEvents{kMidiEventCapacity};
    MidiLaunchState m_launch;
    AuditionVoice m_voices[kAuditionVoices];
    double m_auditionStep[128] = {};
    quint64 m_voiceOrder = 0;
    std::atomic<int> m_publishedVoices{0};
    std::atomic<int> m_publishedLaunched{0};
};

// Drains the capture ring into float32 WAV takes on its own thread (never the
// device thread). `finished` runs on the writer thread.
class AudioTakeWriter {
public:
    using Finished = std::function<void(quint64 serial, const QString &path, qint64 frames, const QString &error)>;
    AudioTakeWriter(AudioEngineProcessor &processor, std::function<QString()> directory, Finished finished);
    ~AudioTakeWriter() { stop(); }
    // Finishes a take whose end is known, then joins. Call after closeAfterStop().
    void stop();
private:
    void run();
    AudioEngineProcessor &m_processor;
    const std::function<QString()> m_directory;
    const Finished m_finished;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

// ---- AudioEngine private records (audio_engine.cpp, audio_engine_transport.cpp) ----

struct AudioEngine::Run {
    quint64 serial = 0;
    AudioDeviceConfig config;
    AudioStreamFormat format;
    AudioDeviceStreamPtr stream;
    std::unique_ptr<AudioEngineProcessor> processor;
    std::unique_ptr<AudioTakeWriter> writer;
};

// Everything a worker-thread device change touches; the GUI reads it only
// after the future finished.
struct AudioEngine::Job {
    JobKind kind = JobKind::Apply;
    QString requestId;
    std::optional<AudioDeviceConfig> next;
    std::optional<AudioDeviceConfig> previous;
    State previousState = State::Stopped;
    QString previousMessage;
    std::shared_ptr<Run> old;
    AudioDeviceStreamPtr opened;
    AudioDeviceStreamPtr rollback;
    QString error;
    QVariantMap finalStatistics;
};

struct AudioEngine::Take {
    qint64 position = 0;
    QVector<QPair<qint64, qint64>> jumps; // (file frame, transport sample), WP-065
    qint64 jumpsDropped = 0;
    bool loopEnabled = false;
    qint64 loopStart = 0;
    qint64 loopEnd = 0;
    bool punchEnabled = false;
    qint64 punchStart = 0;
    qint64 punchEnd = 0;
    qint64 frames = 0;
    qint64 dropped = 0;
    int latency = 0;
    int sampleRate = 0;
    int channels = 0;
    bool ended = false;
    bool written = false;
    bool deviceLost = false;
    QString path;
    QString error;
};

struct AudioEngine::SharedDirectory {
    std::mutex mutex;
    QString path;
    QString get() { std::lock_guard lock(mutex); return path; }
    void set(const QString &value) { std::lock_guard lock(mutex); path = value; }
};

} // namespace QindaTK
