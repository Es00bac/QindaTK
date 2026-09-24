// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_device.h"
#include "audio_engine.h"
#include "audio_graph.h"
#include "audio_meter_tap.h"
#include "audio_ring.h"

#include <QString>
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

    AudioEngineProcessor(const AudioStreamFormat &format, const EngineTransportState &initial,
                         double captureSeconds = 4.0);

    // ---- GUI / worker side ----
    const AudioStreamFormat &format() const { return m_format; }
    bool post(const EngineCommand &command) { return m_commands.push(command); }
    bool takeEvent(EngineEvent &event) { return m_events.pop(event); }
    // Returns a previously offered graph that was never adopted (releasable now).
    const AudioRenderGraph *offer(const AudioRenderGraph *graph);
    quint64 adoptedGeneration() const { return m_adopted.load(std::memory_order_acquire); }
    qint64 position() const { return m_publishedPosition.load(std::memory_order_acquire); }
    bool playing() const { return m_publishedPlaying.load(std::memory_order_acquire); }
    bool recording() const { return m_publishedRecording.load(std::memory_order_acquire); }
    // Only after the device thread was joined: ends an open take and stops.
    void closeAfterStop();
    EngineTransportState stateAfterStop() const;

    // ---- meter consumer (GUI thread) ----
    // Output measurement after each rendered period; null without output.
    AudioMeterTap *outputTap() { return m_outputTap.get(); }

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
    void publish() noexcept;

    const AudioStreamFormat m_format;
    AudioSpscRing<EngineCommand> m_commands{kCommandCapacity};
    AudioSpscRing<EngineEvent> m_events{kEventCapacity};
    AudioSpscRing<float> m_capture;
    std::unique_ptr<double[]> m_scratch;
    std::unique_ptr<AudioMeterTap> m_outputTap;
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
    quint64 m_pushed = 0;
    std::atomic<quint64> m_pushedPublished{0};
    std::atomic<quint64> m_takeSerial{0};
    std::atomic<quint64> m_takeEndTotal{0};
    std::atomic<quint64> m_writtenSerial{0};
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
