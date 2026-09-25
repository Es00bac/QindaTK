// SPDX-License-Identifier: LGPL-3.0-or-later
// ClapInstance audio thread: waits on the shared block's request futex, runs
// plugin->process() on one block and publishes the response. Everything here
// runs on that thread and never allocates: buffers, event storage and the
// output queue are sized on the main thread (constructor, activate).
#include "plugin_host_clap.h"

#include <algorithm>
#include <cstring>

namespace QindaTK::PluginHost {
namespace {

using EventList = std::vector<const clap_event_header_t *>;

uint32_t inputSize(const clap_input_events_t *list)
{
    return uint32_t(static_cast<const EventList *>(list->ctx)->size());
}

const clap_event_header_t *inputGet(const clap_input_events_t *list, uint32_t index)
{
    const EventList &events = *static_cast<const EventList *>(list->ctx);
    return index < events.size() ? events[index] : nullptr;
}

bool outputPush(const clap_output_events_t *list, const clap_event_header_t *event)
{
    auto *instance = static_cast<ClapInstance *>(list->ctx);
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return true;
    PluginIpc::Event out;
    if (event->type == CLAP_EVENT_PARAM_VALUE) {
        const auto *value = reinterpret_cast<const clap_event_param_value_t *>(event);
        out.type = PluginIpc::ParamValue;
        out.param = value->param_id;
        out.value = value->value;
    } else if (event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || event->type == CLAP_EVENT_PARAM_GESTURE_END) {
        const auto *gesture = reinterpret_cast<const clap_event_param_gesture_t *>(event);
        out.type = event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ? PluginIpc::GestureBegin : PluginIpc::GestureEnd;
        out.param = gesture->param_id;
    } else {
        return true; // note ends and the rest are not forwarded
    }
    out.time = event->time;
    instance->pushOutput(out);
    return true;
}

} // namespace

void ClapInstance::attach(PluginIpc::Block *block)
{
    m_block = block;
    m_inEvents.ctx = &m_eventList;
    m_inEvents.size = inputSize;
    m_inEvents.get = inputGet;
    m_outEvents.ctx = this;
    m_outEvents.try_push = outputPush;
    m_audioThread = std::thread([this] {
        m_audioThreadId.store(std::this_thread::get_id(), std::memory_order_release);
        audioLoop();
    });
}

void ClapInstance::pushOutput(const PluginIpc::Event &event)
{
    std::lock_guard lock(m_outputMutex);
    if (m_output.size() < m_output.capacity()) m_output.push_back(event); // full: dropped, never grown here
}

void ClapInstance::audioLoop()
{
    std::uint32_t seen = m_block->request.load(std::memory_order_acquire);
    while (!m_quit.load(std::memory_order_acquire)) {
        if (m_stopRequest.load(std::memory_order_acquire)) {
            if (m_processing) m_plugin->stop_processing(m_plugin);
            m_processing = false;
            m_stopRequest.store(false, std::memory_order_release);
            m_stopDone.store(1, std::memory_order_release);
            PluginIpc::futexWake(&m_stopDone);
        }
        if (m_active.load(std::memory_order_acquire) && m_flushPending.exchange(false, std::memory_order_acq_rel)) {
            flushQueued();
            m_flushDone.fetch_add(1, std::memory_order_acq_rel);
            PluginIpc::futexWake(&m_flushDone);
        }
        const std::uint32_t request = m_block->request.load(std::memory_order_acquire);
        if (request == seen) {
            const timespec wait = PluginIpc::relative(100000000);
            PluginIpc::futexWait(&m_block->request, seen, &wait);
            continue;
        }
        seen = request;
        processBlock();
        m_block->response.store(request, std::memory_order_release);
        PluginIpc::futexWake(&m_block->response);
    }
    if (m_processing) m_plugin->stop_processing(m_plugin);
    m_processing = false;
}

void ClapInstance::buildEvents(const PluginIpc::Block &block) noexcept
{
    m_eventList.clear(); // keeps its capacity
    std::size_t used = 0;
    const auto note = [&](std::uint16_t type, std::uint32_t time, int channel, int key, double velocity) {
        if (used >= m_eventStorage.size()) return;
        clap_event_note_t &event = m_eventStorage[used++].note;
        event.header.size = sizeof(clap_event_note_t);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.header.flags = 0;
        event.note_id = -1;
        event.port_index = 0;
        event.channel = std::int16_t(channel);
        event.key = std::int16_t(key);
        event.velocity = velocity;
        m_eventList.push_back(&event.header);
    };
    if (block.flags & PluginIpc::Discontinuity) {
        for (int channel = 0; channel < 16; ++channel)
            for (int key = 0; key < 128; ++key)
                if (m_held[channel][key]) {
                    note(CLAP_EVENT_NOTE_OFF, 0, channel, key, 0.0);
                    m_held[channel][key] = 0;
                }
    }
    const std::uint32_t count = std::min<std::uint32_t>(block.eventCount, PluginIpc::kMaxEvents);
    const std::uint32_t last = block.frames > 0 ? block.frames - 1 : 0;
    for (std::uint32_t i = 0; i < count && used < m_eventStorage.size(); ++i) {
        const PluginIpc::Event &in = block.events[i];
        const std::uint32_t time = std::min(in.time, last);
        const int channel = std::clamp<int>(in.channel, 0, 15);
        const int key = std::clamp<int>(in.key, 0, 127);
        switch (in.type) {
        case PluginIpc::NoteOn:
            note(CLAP_EVENT_NOTE_ON, time, channel, key, in.value);
            m_held[channel][key] = 1;
            break;
        case PluginIpc::NoteOff:
            note(CLAP_EVENT_NOTE_OFF, time, channel, key, in.value);
            m_held[channel][key] = 0;
            break;
        case PluginIpc::ParamValue: {
            clap_event_param_value_t &event = m_eventStorage[used++].param;
            event.header.size = sizeof(clap_event_param_value_t);
            event.header.time = time;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = CLAP_EVENT_PARAM_VALUE;
            event.header.flags = 0;
            event.param_id = in.param;
            event.cookie = nullptr;
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = in.value;
            m_eventList.push_back(&event.header);
            break;
        }
        default:
            break;
        }
    }
}

void ClapInstance::processBlock() noexcept
{
    PluginIpc::Block &block = *m_block;
    const std::uint32_t frames = std::min<std::uint32_t>(block.frames, PluginIpc::kMaxFrames);
    for (auto &channel : block.output) std::fill_n(channel, frames, 0.0f);
    if (!m_active.load(std::memory_order_acquire) || frames == 0) {
        block.status = PluginIpc::NotActive;
        return;
    }
    if (!m_processing) {
        m_processing = m_plugin->start_processing(m_plugin);
        if (!m_processing) {
            block.status = PluginIpc::PluginError;
            return;
        }
    }
    if (block.flags & PluginIpc::Reset) {
        m_plugin->reset(m_plugin);
        std::memset(m_held, 0, sizeof m_held);
    }
    buildEvents(block);
    // The main ports read and write the block's lanes; other ports and
    // channels use the scratch buffers prepared at activation.
    const auto bind = [](PortSet &ports, float (*lanes)[PluginIpc::kMaxFrames], std::uint32_t frames, bool clear) {
        std::size_t next = 0;
        for (int i = 0; i < ports.count; ++i) {
            clap_audio_buffer_t &buffer = ports.buffers[std::size_t(i)];
            for (std::uint32_t c = 0; c < buffer.channel_count; ++c) {
                float *scratch = ports.scratch[next + c].data();
                const bool lane = i == ports.main && c < std::uint32_t(PluginIpc::kMaxChannels);
                ports.pointers[next + c] = lane ? lanes[c] : scratch;
                if (!lane && clear) std::fill_n(scratch, frames, 0.0f);
            }
            next += buffer.channel_count;
        }
    };
    bind(m_inputs, block.input, frames, true);
    bind(m_outputs, block.output, frames, false);
    clap_process_t process{};
    process.steady_time = block.steadyTime;
    process.frames_count = frames;
    process.transport = nullptr; // free running: the library does not publish its transport (docs/audio-plugins.md)
    process.audio_inputs = m_inputs.count ? m_inputs.buffers.data() : nullptr;
    process.audio_inputs_count = std::uint32_t(m_inputs.count);
    process.audio_outputs = m_outputs.count ? m_outputs.buffers.data() : nullptr;
    process.audio_outputs_count = std::uint32_t(m_outputs.count);
    process.in_events = &m_inEvents;
    process.out_events = &m_outEvents;
    const clap_process_status status = m_plugin->process(m_plugin, &process);
    if (status == CLAP_PROCESS_ERROR) {
        for (auto &channel : block.output) std::fill_n(channel, frames, 0.0f);
        block.status = PluginIpc::PluginError;
        return;
    }
    // A mono main output feeds both lanes; the library averages or copies.
    block.status = PluginIpc::Processed;
}

void ClapInstance::flushQueued() noexcept
{
    if (!m_params) return;
    QVector<QPair<quint32, double>> values;
    {
        std::lock_guard lock(m_flushMutex);
        values.swap(m_flushValues);
    }
    m_eventList.clear();
    std::size_t used = 0;
    for (const auto &[id, value] : std::as_const(values)) {
        if (used >= m_eventStorage.size()) break;
        clap_event_param_value_t &event = m_eventStorage[used++].param;
        event.header.size = sizeof(clap_event_param_value_t);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = 0;
        event.param_id = id;
        event.cookie = nullptr;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        m_eventList.push_back(&event.header);
    }
    m_params->flush(m_plugin, &m_inEvents, &m_outEvents);
    m_eventList.clear();
}

} // namespace QindaTK::PluginHost
