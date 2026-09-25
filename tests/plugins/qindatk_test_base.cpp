// SPDX-License-Identifier: LGPL-3.0-or-later
// Test CLAP plugins: the common plugin callbacks, extensions and the entry.
#include "qindatk_test_plugins.h"

#include <cmath>
#include <new>

namespace QindaTKTestPlugins {
namespace {

Base *self(const clap_plugin_t *plugin)
{
    return static_cast<Base *>(plugin->plugin_data);
}

// ---- audio and note ports ---------------------------------------------------
uint32_t portCount(const clap_plugin_t *plugin, bool input)
{
    return (input ? self(plugin)->inputs : self(plugin)->outputs) > 0 ? 1 : 0;
}

bool portGet(const clap_plugin_t *plugin, uint32_t index, bool input, clap_audio_port_info_t *info)
{
    const uint32_t channels = input ? self(plugin)->inputs : self(plugin)->outputs;
    if (index != 0 || channels == 0) return false;
    info->id = 0;
    std::snprintf(info->name, sizeof info->name, "%s", input ? "Main input" : "Main output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = channels;
    info->port_type = channels == 2 ? CLAP_PORT_STEREO : CLAP_PORT_MONO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t kAudioPorts{portCount, portGet};

uint32_t notePortCount(const clap_plugin_t *plugin, bool input)
{
    return input && self(plugin)->notes ? 1 : 0;
}

bool notePortGet(const clap_plugin_t *plugin, uint32_t index, bool input, clap_note_port_info_t *info)
{
    if (!input || index != 0 || !self(plugin)->notes) return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof info->name, "%s", "Notes");
    return true;
}
const clap_plugin_note_ports_t kNotePorts{notePortCount, notePortGet};

// ---- parameters ---------------------------------------------------------------
uint32_t paramCount(const clap_plugin_t *plugin)
{
    return self(plugin)->paramCount;
}

bool paramInfo(const clap_plugin_t *plugin, uint32_t index, clap_param_info_t *info)
{
    const Base *base = self(plugin);
    if (index >= base->paramCount) return false;
    const Param &param = base->params[index];
    info->id = param.id;
    info->flags = param.flags;
    info->cookie = nullptr;
    std::snprintf(info->name, sizeof info->name, "%s", param.name);
    std::snprintf(info->module, sizeof info->module, "%s", "");
    info->min_value = param.minimum;
    info->max_value = param.maximum;
    info->default_value = param.initial;
    return true;
}

bool paramValue(const clap_plugin_t *plugin, clap_id id, double *value)
{
    const int index = self(plugin)->indexOf(id);
    if (index < 0) return false;
    *value = self(plugin)->valueOf(index);
    return true;
}

bool paramText(const clap_plugin_t *plugin, clap_id id, double value, char *out, uint32_t capacity)
{
    if (self(plugin)->indexOf(id) < 0) return false;
    std::snprintf(out, capacity, "%.3f", value);
    return true;
}

bool paramParse(const clap_plugin_t *plugin, clap_id id, const char *text, double *value)
{
    if (self(plugin)->indexOf(id) < 0 || !text) return false;
    char *end = nullptr;
    *value = std::strtod(text, &end);
    return end != text;
}

void paramFlush(const clap_plugin_t *plugin, const clap_input_events_t *in, const clap_output_events_t *out)
{
    self(plugin)->applyEvents(in, out);
}
const clap_plugin_params_t kParams{paramCount, paramInfo, paramValue, paramText, paramParse, paramFlush};

// ---- state and latency --------------------------------------------------------
bool stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) { return self(plugin)->save(stream); }
bool stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) { return self(plugin)->load(stream); }
const clap_plugin_state_t kState{stateSave, stateLoad};

uint32_t latencyGet(const clap_plugin_t *plugin) { return self(plugin)->latency(); }
const clap_plugin_latency_t kLatency{latencyGet};

// ---- plugin -------------------------------------------------------------------
bool pluginInit(const clap_plugin_t *plugin) { return self(plugin)->init(); }
void pluginDestroy(const clap_plugin_t *plugin)
{
    trace("destroy", self(plugin)->plugin.desc->id);
    delete self(plugin);
}
bool pluginActivate(const clap_plugin_t *plugin, double rate, uint32_t, uint32_t)
{
    Base *base = self(plugin);
    trace("activate", nullptr, static_cast<long long>(rate));
    base->active = base->activate(rate);
    return base->active;
}
void pluginDeactivate(const clap_plugin_t *plugin)
{
    self(plugin)->active = false;
    self(plugin)->deactivate();
    trace("deactivate");
}
bool pluginStart(const clap_plugin_t *) { return true; }
void pluginStop(const clap_plugin_t *) {}
void pluginReset(const clap_plugin_t *plugin) { self(plugin)->reset(); }
clap_process_status pluginProcess(const clap_plugin_t *plugin, const clap_process_t *process)
{
    return self(plugin)->process(process);
}
const void *pluginExtension(const clap_plugin_t *plugin, const char *id) { return self(plugin)->extension(id); }
void pluginMainThread(const clap_plugin_t *) {}

} // namespace

const void *Base::extension(const char *id)
{
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &kAudioPorts;
    if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return notes ? &kNotePorts : nullptr;
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return paramCount ? &kParams : nullptr;
    if (!std::strcmp(id, CLAP_EXT_STATE)) return &kState;
    if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &kLatency;
    return nullptr;
}

int Base::indexOf(clap_id id) const
{
    for (std::uint32_t i = 0; i < paramCount; ++i)
        if (params[i].id == id) return int(i);
    return -1;
}

void Base::setValue(clap_id id, double value)
{
    const int index = indexOf(id);
    if (index < 0 || (params[index].flags & CLAP_PARAM_IS_READONLY)) return;
    const double clamped = std::fmin(params[index].maximum, std::fmax(params[index].minimum, value));
    values[std::size_t(index)] = clamped;
    paramChanged(id, clamped);
}

void Base::applyEvents(const clap_input_events_t *events, const clap_output_events_t *out)
{
    const uint32_t count = events ? events->size(events) : 0;
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t *event = events->get(events, i);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto *value = reinterpret_cast<const clap_event_param_value_t *>(event);
            setValue(value->param_id, value->value);
        } else {
            onEvent(event);
        }
    }
    if (out) flushOutput(out);
}

// State: "QTKS", the value count, then the values (host byte order: the
// state only travels between processes of this machine in the tests).
bool Base::save(const clap_ostream_t *stream)
{
    const char magic[4] = {'Q', 'T', 'K', 'S'};
    const std::uint32_t count = paramCount;
    if (stream->write(stream, magic, 4) != 4 || stream->write(stream, &count, sizeof count) != std::int64_t(sizeof count)) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        const double value = valueOf(int(i));
        if (stream->write(stream, &value, sizeof value) != std::int64_t(sizeof value)) return false;
    }
    trace("state.save", nullptr, count);
    return true;
}

bool Base::load(const clap_istream_t *stream)
{
    char magic[4] = {};
    std::uint32_t count = 0;
    if (stream->read(stream, magic, 4) != 4 || std::memcmp(magic, "QTKS", 4) != 0
        || stream->read(stream, &count, sizeof count) != std::int64_t(sizeof count) || count != paramCount)
        return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        double value = 0.0;
        if (stream->read(stream, &value, sizeof value) != std::int64_t(sizeof value)) return false;
        restore(int(i), value);
    }
    trace("state.load", nullptr, count);
    return true;
}

void Base::bind(const clap_host_t *owner, const clap_plugin_descriptor_t *desc)
{
    host = owner;
    values.resize(paramCount);
    for (std::uint32_t i = 0; i < paramCount; ++i) values[i] = params[i].initial;
    plugin.desc = desc;
    plugin.plugin_data = this;
    plugin.init = pluginInit;
    plugin.destroy = pluginDestroy;
    plugin.activate = pluginActivate;
    plugin.deactivate = pluginDeactivate;
    plugin.start_processing = pluginStart;
    plugin.stop_processing = pluginStop;
    plugin.reset = pluginReset;
    plugin.process = pluginProcess;
    plugin.get_extension = pluginExtension;
    plugin.on_main_thread = pluginMainThread;
}

namespace {

uint32_t factoryCount(const clap_plugin_factory_t *) { return pluginCount(); }
const clap_plugin_descriptor_t *factoryDescriptor(const clap_plugin_factory_t *, uint32_t index)
{
    return descriptor(index);
}
const clap_plugin_t *factoryCreate(const clap_plugin_factory_t *, const clap_host_t *host, const char *id)
{
    return create(host, id);
}
const clap_plugin_factory_t kFactory{factoryCount, factoryDescriptor, factoryCreate};

bool entryInit(const char *) { return true; }
void entryDeinit() {}
const void *entryFactory(const char *id)
{
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace
} // namespace QindaTKTestPlugins

extern "C" {
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, QindaTKTestPlugins::entryInit,
                                                    QindaTKTestPlugins::entryDeinit, QindaTKTestPlugins::entryFactory};
}
