// SPDX-License-Identifier: LGPL-3.0-or-later
// The test plugins of qindatk-test.clap:
//   org.qindatk.test.gain        stereo effect: out = gain * in delayed by `Latency`
//                                (a restart applies a new latency); read-only
//                                `Blocks` counts processed blocks and is saved in
//                                the state; an embeddable x11 editor that asks to
//                                be resized and sets Gain to 0.25 from its editor.
//   org.qindatk.test.synth       instrument: out = Level * sum of held note
//                                velocities, sample accurate, both channels.
//   org.qindatk.test.faulty      pass-through that crashes (Crash >= 0.5) or
//                                hangs (Hang >= 0.5) inside process().
//   org.qindatk.test.init-fails  init() returns false.
//   org.qindatk.test.slow        activate() takes 30 s (request time-outs).
#include "qindatk_test_plugins.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <string>
#include <thread>
#include <unistd.h>

namespace QindaTKTestPlugins {
namespace {

// ---- gain ----------------------------------------------------------------------
struct Gain : Base {
    static constexpr Param kParams[] = {
        {1, "Gain", 0.0, 2.0, 1.0, CLAP_PARAM_IS_AUTOMATABLE},
        {2, "Latency", 0.0, 1024.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED},
        {3, "Blocks", 0.0, 1.0e12, 0.0, CLAP_PARAM_IS_READONLY},
    };
    std::uint32_t activeLatency = 0;
    std::vector<float> line[2];
    std::uint32_t cursor = 0;
    std::atomic<std::uint64_t> blocks{0};
    std::atomic<bool> editorChange{false};
    std::uint32_t width = 400;
    std::uint32_t height = 300;

    Gain()
    {
        params = kParams;
        paramCount = 3;
    }
    bool activate(double rate) override
    {
        sampleRate = rate;
        activeLatency = std::uint32_t(values[1]);
        for (auto &channel : line) channel.assign(std::max<std::uint32_t>(1, activeLatency), 0.0f);
        cursor = 0;
        return true;
    }
    void reset() override
    {
        for (auto &channel : line) std::fill(channel.begin(), channel.end(), 0.0f);
        cursor = 0;
    }
    std::uint32_t latency() const override { return activeLatency; }
    double valueOf(int index) const override { return index == 2 ? double(blocks.load()) : values[std::size_t(index)]; }
    void restore(int index, double value) override
    {
        if (index == 2) blocks.store(std::uint64_t(value));
        else Base::restore(index, value);
    }
    void paramChanged(clap_id id, double value) override
    {
        // CLAP: latency changes only in activate(); ask the host to restart.
        if (id == 2 && active && std::uint32_t(value) != activeLatency && host) host->request_restart(host);
    }
    void flushOutput(const clap_output_events_t *out) override
    {
        if (!editorChange.exchange(false)) return;
        clap_event_param_gesture_t gesture{};
        gesture.header = {std::uint32_t(sizeof gesture), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_GESTURE_BEGIN, 0};
        gesture.param_id = 1;
        out->try_push(out, &gesture.header);
        clap_event_param_value_t value{};
        value.header = {std::uint32_t(sizeof value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        value.param_id = 1;
        value.note_id = -1;
        value.port_index = -1;
        value.channel = -1;
        value.key = -1;
        value.value = 0.25;
        out->try_push(out, &value.header);
        gesture.header.type = CLAP_EVENT_PARAM_GESTURE_END;
        out->try_push(out, &gesture.header);
        values[0] = 0.25;
    }
    clap_process_status process(const clap_process_t *process) override
    {
        applyEvents(process->in_events, process->out_events);
        if (process->audio_outputs_count < 1) return CLAP_PROCESS_ERROR;
        const clap_audio_buffer_t &output = process->audio_outputs[0];
        const clap_audio_buffer_t *input = process->audio_inputs_count ? &process->audio_inputs[0] : nullptr;
        const double gain = values[0];
        for (std::uint32_t i = 0; i < process->frames_count; ++i) {
            for (std::uint32_t c = 0; c < output.channel_count && c < 2; ++c) {
                const float x = input && input->channel_count ? input->data32[std::min(c, input->channel_count - 1)][i] : 0.0f;
                float y = x;
                if (activeLatency) {
                    y = line[c][cursor];
                    line[c][cursor] = x;
                }
                output.data32[c][i] = float(gain * double(y));
            }
            if (activeLatency && ++cursor == activeLatency) cursor = 0;
        }
        blocks.fetch_add(1);
        return CLAP_PROCESS_CONTINUE;
    }
    const void *extension(const char *id) override;
};

Gain *gain(const clap_plugin_t *plugin)
{
    return static_cast<Gain *>(static_cast<Base *>(plugin->plugin_data));
}

bool guiSupported(const clap_plugin_t *, const char *api, bool)
{
    return api && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
}
bool guiPreferred(const clap_plugin_t *, const char **api, bool *floating)
{
    *api = CLAP_WINDOW_API_X11;
    *floating = false;
    return true;
}
bool guiCreate(const clap_plugin_t *, const char *api, bool floating)
{
    trace(floating ? "gui.create floating" : "gui.create embedded", api);
    return true;
}
void guiDestroy(const clap_plugin_t *) { trace("gui.destroy"); }
bool guiScale(const clap_plugin_t *, double) { return true; }
bool guiSize(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height)
{
    *width = gain(plugin)->width;
    *height = gain(plugin)->height;
    return true;
}
bool guiCanResize(const clap_plugin_t *) { return false; }
bool guiHints(const clap_plugin_t *, clap_gui_resize_hints_t *) { return false; }
bool guiAdjust(const clap_plugin_t *, uint32_t *, uint32_t *) { return false; }
bool guiSetSize(const clap_plugin_t *, uint32_t, uint32_t) { return false; }
bool guiParent(const clap_plugin_t *, const clap_window_t *window)
{
    trace("gui.set_parent", nullptr, static_cast<long long>(window->x11));
    return true;
}
bool guiTransient(const clap_plugin_t *, const clap_window_t *window)
{
    trace("gui.set_transient", nullptr, static_cast<long long>(window->x11));
    return true;
}
void guiTitle(const clap_plugin_t *, const char *title) { trace("gui.title", title); }
bool guiShow(const clap_plugin_t *plugin)
{
    trace("gui.show");
    Gain *self = gain(plugin);
    const auto *hostGui = static_cast<const clap_host_gui_t *>(self->host->get_extension(self->host, CLAP_EXT_GUI));
    if (hostGui && hostGui->request_resize(self->host, 420, 320)) {
        self->width = 420;
        self->height = 320;
    }
    // A change made in the editor: Gain 0.25 in one gesture, delivered at the
    // next process() or params flush.
    self->editorChange.store(true);
    const auto *hostParams = static_cast<const clap_host_params_t *>(self->host->get_extension(self->host, CLAP_EXT_PARAMS));
    if (hostParams) hostParams->request_flush(self->host);
    return true;
}
bool guiHide(const clap_plugin_t *)
{
    trace("gui.hide");
    return true;
}
const clap_plugin_gui_t kGainGui{guiSupported, guiPreferred, guiCreate, guiDestroy, guiScale, guiSize, guiCanResize,
                                 guiHints, guiAdjust, guiSetSize, guiParent, guiTransient, guiTitle, guiShow, guiHide};

const void *Gain::extension(const char *id)
{
    return std::strcmp(id, CLAP_EXT_GUI) == 0 ? &kGainGui : Base::extension(id);
}

// ---- synth ---------------------------------------------------------------------
struct Synth : Base {
    static constexpr Param kParams[] = {{1, "Level", 0.0, 1.0, 0.25, CLAP_PARAM_IS_AUTOMATABLE}};
    double velocity[16][128] = {};
    double total = 0.0;
    Synth()
    {
        params = kParams;
        paramCount = 1;
        inputs = 0;
        notes = true;
    }
    void reset() override
    {
        std::fill(&velocity[0][0], &velocity[0][0] + 16 * 128, 0.0);
        total = 0.0;
    }
    void onEvent(const clap_event_header_t *event) override
    {
        if (event->type != CLAP_EVENT_NOTE_ON && event->type != CLAP_EVENT_NOTE_OFF) return;
        const auto *note = reinterpret_cast<const clap_event_note_t *>(event);
        if (note->channel < 0 || note->channel > 15 || note->key < 0 || note->key > 127) return;
        double &slot = velocity[note->channel][note->key];
        total -= slot;
        slot = event->type == CLAP_EVENT_NOTE_ON ? note->velocity : 0.0;
        total += slot;
        if (total < 1e-12) total = 0.0;
    }
    clap_process_status process(const clap_process_t *process) override
    {
        const clap_input_events_t *events = process->in_events;
        const std::uint32_t count = events ? events->size(events) : 0;
        std::uint32_t next = 0;
        const auto apply = [&](const clap_event_header_t *event) {
            if (event->type == CLAP_EVENT_PARAM_VALUE)
                setValue(reinterpret_cast<const clap_event_param_value_t *>(event)->param_id,
                         reinterpret_cast<const clap_event_param_value_t *>(event)->value);
            else
                onEvent(event);
        };
        const clap_audio_buffer_t &output = process->audio_outputs[0];
        for (std::uint32_t i = 0; i < process->frames_count; ++i) {
            for (; next < count; ++next) {
                const clap_event_header_t *event = events->get(events, next);
                if (event->time > i) break;
                apply(event);
            }
            const float value = float(values[0] * total);
            for (std::uint32_t c = 0; c < output.channel_count; ++c) output.data32[c][i] = value;
        }
        for (; next < count; ++next) apply(events->get(events, next));
        return CLAP_PROCESS_CONTINUE;
    }
};

// ---- failures -------------------------------------------------------------------
struct Faulty : Base {
    static constexpr Param kParams[] = {
        {1, "Crash", 0.0, 1.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED},
        {2, "Hang", 0.0, 1.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED},
    };
    Faulty()
    {
        params = kParams;
        paramCount = 2;
    }
    clap_process_status process(const clap_process_t *process) override
    {
        applyEvents(process->in_events, process->out_events);
        if (values[0] >= 0.5) {
            trace("faulty.crash");
            std::raise(SIGSEGV);
        }
        if (values[1] >= 0.5) {
            trace("faulty.hang");
            for (;;) pause();
        }
        const clap_audio_buffer_t &output = process->audio_outputs[0];
        const clap_audio_buffer_t &input = process->audio_inputs[0];
        for (std::uint32_t c = 0; c < output.channel_count; ++c)
            std::copy_n(input.data32[std::min(c, input.channel_count - 1)], process->frames_count, output.data32[c]);
        return CLAP_PROCESS_CONTINUE;
    }
};

struct InitFails : Base {
    bool init() override { return false; }
    clap_process_status process(const clap_process_t *) override { return CLAP_PROCESS_ERROR; }
};

struct Slow : Base {
    bool activate(double rate) override
    {
        sampleRate = rate;
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return true;
    }
    clap_process_status process(const clap_process_t *) override { return CLAP_PROCESS_CONTINUE; }
};

// ---- descriptors ----------------------------------------------------------------
const char *const kEffect[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
const char *const kInstrument[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};

constexpr clap_plugin_descriptor_t describe(const char *id, const char *name, const char *const *features)
{
    return {CLAP_VERSION_INIT, id, name, "QindaTK tests", "", "", "", "1.0.0", "QindaTK test plugin", features};
}

const clap_plugin_descriptor_t kDescriptors[] = {
    describe("org.qindatk.test.gain", "Test Gain", kEffect),
    describe("org.qindatk.test.synth", "Test Synth", kInstrument),
    describe("org.qindatk.test.faulty", "Test Faulty", kEffect),
    describe("org.qindatk.test.init-fails", "Test Init Fails", kEffect),
    describe("org.qindatk.test.slow", "Test Slow", kEffect),
};

} // namespace

std::uint32_t pluginCount()
{
    return std::uint32_t(sizeof kDescriptors / sizeof kDescriptors[0]);
}

const clap_plugin_descriptor_t *descriptor(std::uint32_t index)
{
    return index < pluginCount() ? &kDescriptors[index] : nullptr;
}

const clap_plugin_t *create(const clap_host_t *host, const char *id)
{
    Base *plugin = nullptr;
    const clap_plugin_descriptor_t *desc = nullptr;
    for (std::uint32_t i = 0; i < pluginCount(); ++i)
        if (std::strcmp(kDescriptors[i].id, id) == 0) desc = &kDescriptors[i];
    if (!desc) return nullptr;
    const std::string name = desc->id;
    if (name == "org.qindatk.test.gain") plugin = new Gain;
    else if (name == "org.qindatk.test.synth") plugin = new Synth;
    else if (name == "org.qindatk.test.faulty") plugin = new Faulty;
    else if (name == "org.qindatk.test.init-fails") plugin = new InitFails;
    else plugin = new Slow;
    plugin->bind(host, desc);
    trace("create", desc->id);
    return &plugin->plugin;
}

} // namespace QindaTKTestPlugins
