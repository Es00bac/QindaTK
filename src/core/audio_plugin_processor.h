// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QPair>
#include <QString>
#include <QVector>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace QindaTK {

namespace PluginIpc { struct Block; }

// AGENT-CONTRACT (WP-066, D-190/D-192, docs/audio-plugins.md): the realtime
// side of one hosted plugin instance. It owns the shared memory block the
// plugin's host process reads (audio_plugin_ipc_p.h) and outlives the
// instance while any compiled AudioRenderGraph still holds it.
//
// Rendering thread (the device thread, or the thread of an offline render):
//   prepare(frames, offline, bypass) -> setParameter()/addNote() -> run()
// run() sends one block and waits for the answer for at most the realtime
// (or offline) deadline. It never allocates or locks; the wait is a futex
// wait. Whenever no answer can be used — bypassed, failed, dead, late, or
// claimed by another renderer — the lanes carry the dry signal delayed by the
// plugin's reported latency, so the channel stays aligned (D-192).
// A block that misses its deadline stays pending: no new block is sent until
// the host answers it; audio time spent waiting beyond the hang limit marks
// the processor hung, and the GUI side kills the process.
//
// GUI thread: claim(Control) before reconfiguring (activation, latency,
// parameters), release(Device) after; claim(Offline) for an offline render.
class AudioPluginProcessor {
public:
    enum Owner : int { Device = 0, Offline = 1, Control = 2 };

    AudioPluginProcessor();
    ~AudioPluginProcessor();
    AudioPluginProcessor(const AudioPluginProcessor &) = delete;
    AudioPluginProcessor &operator=(const AudioPluginProcessor &) = delete;

    bool valid() const { return m_block != nullptr; }
    QString error() const { return m_error; }
    int fd() const { return m_fd; }

    // ---- GUI thread ----
    // Hands the processor to `owner` once the current renderer left run();
    // a block still pending is awaited for at most `timeoutNs`. Returns the
    // previous owner.
    int claim(int owner, std::int64_t timeoutNs) noexcept;
    void release(int owner) noexcept;
    int owner() const noexcept { return m_owner.load(std::memory_order_seq_cst); }
    // Only while claimed (Control or Offline).
    void configure(int sampleRate, int inputs, int outputs, int latency, const QVector<quint32> &parameterIds);
    void setTimeouts(int realtimeMs, int offlineMs, int hangMs);
    void setDead() noexcept;
    // The host process: a wait notices its exit within a few milliseconds
    // (pidfd), even while the GUI thread is busy in an offline render.
    void watchProcess(qint64 pid);
    void requestReset() noexcept { m_resetPending.store(true, std::memory_order_release); }
    bool dead() const noexcept { return m_dead.load(std::memory_order_acquire); }
    bool hung() const noexcept { return m_hung.load(std::memory_order_acquire); }
    bool configured() const noexcept { return m_configured; }
    int latency() const noexcept { return m_latency; }
    int sampleRate() const noexcept { return m_sampleRate; }
    quint64 processedBlocks() const noexcept { return m_processed.load(std::memory_order_relaxed); }
    quint64 lateBlocks() const noexcept { return m_late.load(std::memory_order_relaxed); }
    quint64 failedBlocks() const noexcept { return m_failed.load(std::memory_order_relaxed); }

    // ---- rendering thread ----
    // True when a block will be sent to the plugin: the caller then adds
    // parameters and notes. run() must follow every prepare().
    bool prepare(int frames, bool offline, bool bypass) noexcept;
    // Sends the value at the start of the block when it differs from the
    // last value sent (index into the configured parameter IDs).
    void setParameter(int index, double value) noexcept;
    void addNote(int time, bool on, int channel, int key, double velocity) noexcept;
    // `lanes[1]` is `lanes[0]` for a mono channel (laneCount 1).
    void run(std::int64_t start, int frames, double *const lanes[2], int laneCount) noexcept;

private:
    bool await(std::uint32_t sequence, std::int64_t timeoutNs) noexcept;
    void finish(double *const lanes[2], int laneCount, int frames, bool processed) noexcept;

    int m_fd = -1;
    int m_pidfd = -1;
    PluginIpc::Block *m_block = nullptr;
    QString m_error;

    std::atomic<int> m_owner{Control}; // nothing renders until the first configure/release
    std::atomic<bool> m_busy{false};
    std::atomic<bool> m_dead{false};
    std::atomic<bool> m_hung{false};
    std::atomic<bool> m_resetPending{true};
    std::atomic<quint64> m_processed{0};
    std::atomic<quint64> m_late{0};
    std::atomic<quint64> m_failed{0};

    // Written under a claim, read by the renderer.
    bool m_configured = false;
    int m_sampleRate = 0;
    int m_inputs = 0;
    int m_outputs = 0;
    int m_latency = 0;
    std::int64_t m_realtimeNs = 50000000;  // 50 ms
    std::int64_t m_offlineNs = 5000000000; // 5 s
    int m_hangMs = 2000;
    std::int64_t m_hangFrames = 96000;
    std::vector<quint32> m_parameterIds;
    std::vector<double> m_lastSent;
    std::vector<double> m_dry; // 2 lanes x max(1, latency)

    // Renderer state.
    bool m_usable = false;
    bool m_sendReset = false;
    bool m_offlineRun = false;
    bool m_pending = false;
    std::uint32_t m_sequence = 0;
    std::int64_t m_lateFrames = 0;
    std::int64_t m_nextStart = -1;
    std::int64_t m_steady = 0;
    int m_dryCursor = 0;
};

using AudioPluginProcessorPtr = std::shared_ptr<AudioPluginProcessor>;

// One insert of a channel's chain as the graph compiles it (GUI thread).
struct AudioPluginInsertSpec {
    QString channelId;
    QString instanceId;
    AudioPluginProcessorPtr processor; // null: missing or failed; the insert passes dry
    bool bypassed = false;
    bool notes = false;                // receives the MIDI track's notes (an instrument)
    int latency = 0;                   // frames, as reported when compiled
    // (index into the processor's parameter IDs, automation parameter ID)
    QVector<QPair<int, QString>> parameters;
};

// Chains in channel order, each chain in slot order (AudioPluginHost::inserts).
struct AudioPluginInserts {
    std::vector<AudioPluginInsertSpec> entries;
};
using AudioPluginInsertsPtr = std::shared_ptr<const AudioPluginInserts>;

} // namespace QindaTK
