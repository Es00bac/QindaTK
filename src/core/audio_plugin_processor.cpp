// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioPluginProcessor: the shared block and the realtime exchange with a
// plugin host process. prepare/setParameter/addNote/run run on the rendering
// thread and never allocate, lock or perform I/O (futex syscalls only).
#include "audio_plugin_processor.h"

#include "audio_plugin_ipc_p.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>
#include <poll.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <linux/memfd.h>
#endif

namespace QindaTK {

AudioPluginProcessor::AudioPluginProcessor()
{
#ifdef __ANDROID__
    // AGENT-NOTE (D-354): bionic declares memfd_create only from API 30; the
    // minimum here is 28, so call the syscall (present in every kernel Android 9+ ships).
    m_fd = int(::syscall(SYS_memfd_create, "qindatk-plugin-block", MFD_CLOEXEC));
#else
    m_fd = ::memfd_create("qindatk-plugin-block", MFD_CLOEXEC);
#endif
    if (m_fd < 0 || ::ftruncate(m_fd, off_t(sizeof(PluginIpc::Block))) != 0) {
        m_error = QCoreApplication::translate("AudioPluginHost", "The shared audio block cannot be created");
        return;
    }
    void *memory = ::mmap(nullptr, sizeof(PluginIpc::Block), PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (memory == MAP_FAILED) {
        m_error = QCoreApplication::translate("AudioPluginHost", "The shared audio block cannot be mapped");
        return;
    }
    m_block = new (memory) PluginIpc::Block;
}

AudioPluginProcessor::~AudioPluginProcessor()
{
    if (m_block) {
        m_block->~Block();
        ::munmap(m_block, sizeof(PluginIpc::Block));
    }
    if (m_fd >= 0) ::close(m_fd);
    if (m_pidfd >= 0) ::close(m_pidfd);
}

void AudioPluginProcessor::watchProcess(qint64 pid)
{
    if (m_pidfd >= 0) ::close(m_pidfd);
    m_pidfd = pid > 0 ? int(::syscall(SYS_pidfd_open, pid_t(pid), 0)) : -1;
}

int AudioPluginProcessor::claim(int owner, std::int64_t timeoutNs) noexcept
{
    const int previous = m_owner.exchange(owner, std::memory_order_seq_cst);
    // AGENT-GUARD: pairs with prepare(): the renderer sets m_busy, then reads
    // the owner (both seq_cst). Once the owner changed and m_busy is clear, no
    // renderer is inside prepare/run, and none will enter until release().
    const std::int64_t deadline = PluginIpc::monotonicNs() + std::max(m_offlineNs, m_realtimeNs) + 1000000000;
    while (m_busy.load(std::memory_order_seq_cst) && PluginIpc::monotonicNs() < deadline) sched_yield();
    if (m_pending && m_block) {
        if (await(m_sequence, timeoutNs)) m_pending = false;
        else m_hung.store(true, std::memory_order_release);
    }
    return previous;
}

void AudioPluginProcessor::release(int owner) noexcept
{
    m_owner.store(owner, std::memory_order_seq_cst);
}

void AudioPluginProcessor::configure(int sampleRate, int inputs, int outputs, int latency,
                                     const QVector<quint32> &parameterIds)
{
    m_sampleRate = std::max(1, sampleRate);
    m_inputs = std::clamp(inputs, 0, PluginIpc::kMaxChannels);
    m_outputs = std::clamp(outputs, 0, PluginIpc::kMaxChannels);
    m_latency = std::max(0, latency);
    m_dry.assign(std::size_t(2) * std::size_t(std::max(1, m_latency)), 0.0);
    m_dryCursor = 0;
    m_parameterIds.assign(parameterIds.cbegin(), parameterIds.cend());
    m_lastSent.assign(m_parameterIds.size(), std::numeric_limits<double>::quiet_NaN());
    m_hangFrames = std::int64_t(m_sampleRate) * m_hangMs / 1000;
    m_nextStart = -1;
    m_resetPending.store(true, std::memory_order_release);
    m_configured = true;
}

void AudioPluginProcessor::setTimeouts(int realtimeMs, int offlineMs, int hangMs)
{
    m_realtimeNs = std::int64_t(std::max(1, realtimeMs)) * 1000000;
    m_offlineNs = std::int64_t(std::max(1, offlineMs)) * 1000000;
    m_hangMs = std::max(1, hangMs);
    m_hangFrames = std::int64_t(std::max(1, m_sampleRate)) * m_hangMs / 1000;
}

void AudioPluginProcessor::setDead() noexcept
{
    m_dead.store(true, std::memory_order_release);
    if (m_block) PluginIpc::futexWake(&m_block->response); // end a wait in progress now
}

bool AudioPluginProcessor::prepare(int frames, bool offline, bool bypass) noexcept
{
    m_usable = false;
    m_owned = false;
    m_offlineRun = offline;
    m_busy.store(true, std::memory_order_seq_cst);
    if (m_owner.load(std::memory_order_seq_cst) != (offline ? Offline : Device)) return false;
    if (!m_block || !m_configured) return false;
    m_owned = true;
    // A reset (new configuration, or the start or end of an offline render)
    // clears the dry line here and reaches the plugin with the next block.
    if (m_resetPending.exchange(false, std::memory_order_acq_rel)) {
        std::fill(m_dry.begin(), m_dry.end(), 0.0);
        m_dryCursor = 0;
        m_sendReset = true;
    }
    if (m_dead.load(std::memory_order_acquire) || frames <= 0 || frames > PluginIpc::kMaxFrames) return false;
    if (m_pending) {
        // The previous block missed its deadline: nothing new is sent until
        // the host answers it, and waiting too long means the plugin hangs.
        if (m_block->response.load(std::memory_order_acquire) != m_sequence) {
            m_lateFrames += frames;
            if (m_lateFrames >= m_hangFrames) m_hung.store(true, std::memory_order_release);
            return false;
        }
        m_pending = false;
        m_lateFrames = 0;
    }
    if (bypass || m_hung.load(std::memory_order_acquire)) return false;
    m_block->eventCount = 0;
    m_usable = true;
    return true;
}

void AudioPluginProcessor::setParameter(int index, double value) noexcept
{
    if (!m_usable || index < 0 || std::size_t(index) >= m_lastSent.size()) return;
    double &last = m_lastSent[std::size_t(index)];
    if (last == value || m_block->eventCount >= std::uint32_t(PluginIpc::kMaxEvents)) return;
    PluginIpc::Event &event = m_block->events[m_block->eventCount++];
    event.type = PluginIpc::ParamValue;
    event.time = 0;
    event.param = m_parameterIds[std::size_t(index)];
    event.channel = 0;
    event.key = 0;
    event.value = value;
    last = value;
}

void AudioPluginProcessor::addNote(int time, bool on, int channel, int key, double velocity) noexcept
{
    if (!m_usable || m_block->eventCount >= std::uint32_t(PluginIpc::kMaxEvents)) return;
    PluginIpc::Event &event = m_block->events[m_block->eventCount++];
    event.type = on ? PluginIpc::NoteOn : PluginIpc::NoteOff;
    event.time = std::uint32_t(std::max(0, time));
    event.param = 0;
    event.channel = std::int16_t(std::clamp(channel, 0, 15));
    event.key = std::int16_t(std::clamp(key, 0, 127));
    event.value = velocity;
}

// Waits in slices of at most 5 ms; between slices a zero-timeout poll on the
// pidfd tells whether the host process died (then the processor is dead).
bool AudioPluginProcessor::await(std::uint32_t sequence, std::int64_t timeoutNs) noexcept
{
    const std::int64_t begin = PluginIpc::monotonicNs();
    for (;;) {
        const std::uint32_t current = m_block->response.load(std::memory_order_acquire);
        if (current == sequence) return true;
        if (m_dead.load(std::memory_order_acquire)) return false;
        if (m_pidfd >= 0) {
            pollfd exited{m_pidfd, POLLIN, 0};
            if (::poll(&exited, 1, 0) > 0) {
                m_dead.store(true, std::memory_order_release);
                return false;
            }
        }
        const std::int64_t left = timeoutNs - (PluginIpc::monotonicNs() - begin);
        if (left <= 0) return false;
        const timespec wait = PluginIpc::relative(std::min<std::int64_t>(left, 5000000));
        PluginIpc::futexWait(&m_block->response, current, &wait);
    }
}

void AudioPluginProcessor::run(std::int64_t start, int frames, double *const lanes[2], int laneCount) noexcept
{
    // AGENT-GUARD: a renderer that does not own the processor must not touch
    // the dry line or the block: the owner (GUI thread reconfiguring, or an
    // offline render) may be reallocating them. Its lanes pass unchanged.
    if (!m_owned) {
        m_busy.store(false, std::memory_order_seq_cst);
        return;
    }
    bool processed = false;
    if (m_usable) {
        PluginIpc::Block &block = *m_block;
        for (int c = 0; c < m_inputs; ++c) {
            float *in = block.input[c];
            for (int i = 0; i < frames; ++i) {
                const double x = laneCount == 1 ? lanes[0][i]
                                 : m_inputs == 1 ? (lanes[0][i] + lanes[1][i]) * 0.5 : lanes[c][i];
                in[i] = float(x);
            }
        }
        std::uint32_t flags = 0;
        if (std::exchange(m_sendReset, false)) flags |= PluginIpc::Reset;
        if (start != m_nextStart) flags |= PluginIpc::Discontinuity;
        block.frames = std::uint32_t(frames);
        block.inputChannels = std::uint32_t(m_inputs);
        block.outputChannels = std::uint32_t(m_outputs);
        block.flags = flags;
        block.steadyTime = m_steady;
        block.status = PluginIpc::NotActive;
        const std::uint32_t sequence = ++m_sequence;
        block.request.store(sequence, std::memory_order_release);
        PluginIpc::futexWake(&block.request);
        if (await(sequence, m_offlineRun ? m_offlineNs : m_realtimeNs)) {
            processed = block.status == PluginIpc::Processed;
            m_processed.fetch_add(1, std::memory_order_relaxed);
            if (!processed) m_failed.fetch_add(1, std::memory_order_relaxed);
        } else if (!m_dead.load(std::memory_order_acquire)) {
            m_pending = true;
            m_lateFrames = 0;
            m_late.fetch_add(1, std::memory_order_relaxed);
            // Offline there is no later period to catch up in: a render that
            // waited its whole deadline treats the plugin as hung.
            if (m_offlineRun) m_hung.store(true, std::memory_order_release);
        }
        m_steady += frames;
        m_nextStart = start + frames;
    }
    finish(lanes, laneCount, frames, processed);
    m_usable = false;
    m_owned = false;
    m_busy.store(false, std::memory_order_seq_cst);
}

// The dry line always advances, so bypass and failure stay aligned with the
// processed signal (D-192): out = plugin output, or input delayed by latency.
void AudioPluginProcessor::finish(double *const lanes[2], int laneCount, int frames, bool processed) noexcept
{
    const int latency = m_configured ? m_latency : 0;
    const float *out0 = m_block ? m_block->output[0] : nullptr;
    const float *out1 = m_block ? m_block->output[m_outputs > 1 ? 1 : 0] : nullptr;
    const std::size_t stride = std::size_t(std::max(1, latency));
    for (int i = 0; i < frames; ++i) {
        double wet[2] = {0.0, 0.0};
        if (processed && m_outputs > 0) {
            const double a = double(out0[i]);
            const double b = double(out1[i]);
            if (laneCount == 1) {
                wet[0] = m_outputs == 1 ? a : (a + b) * 0.5;
            } else {
                wet[0] = a;
                wet[1] = b;
            }
        }
        for (int c = 0; c < laneCount; ++c) {
            const double x = lanes[c][i];
            double dry = x;
            if (latency > 0) {
                double &slot = m_dry[std::size_t(c) * stride + std::size_t(m_dryCursor)];
                dry = slot;
                slot = x;
            }
            lanes[c][i] = processed ? wet[c] : dry;
        }
        if (latency > 0 && ++m_dryCursor == latency) m_dryCursor = 0;
    }
}

} // namespace QindaTK
