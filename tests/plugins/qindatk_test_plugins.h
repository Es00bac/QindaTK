// SPDX-License-Identifier: LGPL-3.0-or-later
// Shared pieces of the test CLAP plugins (qindatk-test.clap). These are real
// binary plugins built by the test tree and loaded by qindatk-plugin-host;
// they exercise hosting, parameters, state, latency, editors and failures
// (tst_audio_plugin_host, tst_audio_plugin_engine; docs/audio-plugins.md).
#pragma once

#include <clap/clap.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace QindaTKTestPlugins {

// Appends "<event>[ <text>][ <number>]" to $QINDATK_TEST_PLUGIN_LOG (if
// set): the tests read the calls a plugin received, in order, across processes.
inline void trace(const char *event, const char *text = nullptr, long long number = LLONG_MIN)
{
    const char *path = std::getenv("QINDATK_TEST_PLUGIN_LOG");
    if (!path || !*path) return;
    if (std::FILE *file = std::fopen(path, "a")) {
        std::fputs(event, file);
        if (text) std::fprintf(file, " %s", text);
        if (number != LLONG_MIN) std::fprintf(file, " %lld", number);
        std::fputc('\n', file);
        std::fclose(file);
    }
}

struct Param {
    clap_id id;
    const char *name;
    double minimum;
    double maximum;
    double initial;
    clap_param_info_flags flags;
};

// Stereo (or no) main audio ports and optional note input; every test plugin
// uses one set of callbacks for the common extensions.
struct Base {
    clap_plugin_t plugin{};
    const clap_host_t *host = nullptr;
    const Param *params = nullptr;
    std::uint32_t paramCount = 0;
    std::vector<double> values;
    std::uint32_t inputs = 2;  // main input channels (0: instrument)
    std::uint32_t outputs = 2;
    bool notes = false;
    double sampleRate = 0.0;
    bool active = false;
    virtual ~Base() = default;
    virtual bool init() { return true; }
    virtual bool activate(double rate) { sampleRate = rate; return true; }
    virtual void deactivate() {}
    virtual void reset() {}
    virtual clap_process_status process(const clap_process_t *process) = 0;
    virtual void paramChanged(clap_id id, double value) { (void)id; (void)value; }
    virtual std::uint32_t latency() const { return 0; }
    virtual bool save(const clap_ostream_t *stream);
    virtual bool load(const clap_istream_t *stream);
    virtual const void *extension(const char *id);
    virtual double valueOf(int index) const { return values[std::size_t(index)]; }
    // State load: every value, read-only ones included.
    virtual void restore(int index, double value)
    {
        values[std::size_t(index)] = value;
        if (!(params[index].flags & CLAP_PARAM_IS_READONLY)) paramChanged(params[index].id, value);
    }
    void bind(const clap_host_t *owner, const clap_plugin_descriptor_t *desc);
    int indexOf(clap_id id) const;
    void setValue(clap_id id, double value);
    void applyEvents(const clap_input_events_t *events, const clap_output_events_t *out);
    virtual void onEvent(const clap_event_header_t *event) { (void)event; }
    virtual void flushOutput(const clap_output_events_t *out) { (void)out; }
};

const clap_plugin_t *create(const clap_host_t *host, const char *id);
std::uint32_t pluginCount();
const clap_plugin_descriptor_t *descriptor(std::uint32_t index);

} // namespace QindaTKTestPlugins
