// SPDX-License-Identifier: LGPL-3.0-or-later
// Private: the wire between qindatk and its plugin host process
// (src/pluginhost). Both sides are built from this one header.
#pragma once

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <ctime>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace QindaTK::PluginIpc {

// AGENT-CONTRACT (D-190, docs/audio-plugins.md): one shared memory Block per
// plugin instance, created by qindatk (memfd) and inherited by the host
// process (qindatk-plugin-host --serve ... --fd N). The library's realtime
// side (AudioPluginProcessor) and the host process's audio thread
// (src/pluginhost/plugin_host_audio.cpp) are the only readers and writers.
// A block is one request/response: the library fills frames, flags, events
// and input, then publishes request = n and wakes the futex; the host
// processes, writes output and status, and publishes response = n. Nothing
// here allocates; both waits are futex waits with a timeout.
// Control messages (load, activate, state, parameters, editor) travel as JSON
// lines over the host's stdin/stdout (AudioPluginInstance <-> plugin_host_main).
// Changing any layout below means bumping kVersion; the host refuses a block
// with another version, so a stale host binary fails loudly, never silently.
inline constexpr std::uint32_t kMagic = 0x51544b50u; // "QTKP"
inline constexpr std::uint32_t kVersion = 1;
inline constexpr int kMaxChannels = 2;
inline constexpr int kMaxFrames = 256;  // == AudioRenderGraph::kChunk (static_assert in audio_graph_plugins.cpp)
inline constexpr int kMaxEvents = 256;

enum EventType : std::uint32_t {
    NoteOn = 1,
    NoteOff = 2,
    ParamValue = 3,
    GestureBegin = 4,
    GestureEnd = 5,
};

struct Event {
    std::uint32_t type = 0;
    std::uint32_t time = 0;   // frame within the block
    std::uint32_t param = 0;  // CLAP parameter ID (ParamValue, Gesture*)
    std::int16_t channel = 0; // MIDI channel 0..15 (notes)
    std::int16_t key = 0;     // MIDI key 0..127 (notes)
    double value = 0.0;       // note velocity 0..1, or the plain parameter value
};

enum RequestFlag : std::uint32_t {
    Reset = 1u << 0,         // call plugin->reset() before this block
    Discontinuity = 1u << 1, // the transport jumped: end every sounding note first
};

enum Status : std::int32_t {
    Processed = 0,
    PluginError = 1, // process() returned CLAP_PROCESS_ERROR: output is silence
    NotActive = 2,   // the plugin is not activated: output is silence
};

struct Block {
    std::uint32_t magic = kMagic;
    std::uint32_t version = kVersion;
    alignas(64) std::atomic<std::uint32_t> request{0};
    alignas(64) std::atomic<std::uint32_t> response{0};
    alignas(64) std::uint32_t frames = 0;
    std::uint32_t inputChannels = 0;
    std::uint32_t outputChannels = 0;
    std::uint32_t flags = 0;
    std::int64_t steadyTime = 0;
    std::uint32_t eventCount = 0;
    std::int32_t status = Processed;
    Event events[kMaxEvents];
    float input[kMaxChannels][kMaxFrames];
    float output[kMaxChannels][kMaxFrames];
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "futex words must be plain 32-bit integers");
static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t), "futex words must be plain 32-bit integers");

// Shared (not FUTEX_PRIVATE) operations: the words live in a MAP_SHARED
// mapping seen by two processes. Both are async-signal-safe syscalls.
inline long futexWait(std::atomic<std::uint32_t> *word, std::uint32_t expected, const timespec *timeout) noexcept
{
    return syscall(SYS_futex, reinterpret_cast<std::uint32_t *>(word), FUTEX_WAIT, expected, timeout, nullptr, 0);
}

inline void futexWake(std::atomic<std::uint32_t> *word) noexcept
{
    syscall(SYS_futex, reinterpret_cast<std::uint32_t *>(word), FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
}

inline std::int64_t monotonicNs() noexcept
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return std::int64_t(now.tv_sec) * 1000000000 + now.tv_nsec;
}

inline timespec relative(std::int64_t nanoseconds) noexcept
{
    timespec out{};
    out.tv_sec = time_t(nanoseconds / 1000000000);
    out.tv_nsec = long(nanoseconds % 1000000000);
    return out;
}

// Control protocol version, checked by the "load" answer.
inline constexpr int kProtocol = 1;

} // namespace QindaTK::PluginIpc
