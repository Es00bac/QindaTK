// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioRenderGraph: plugin inserts and latency compensation (WP-066,
// docs/audio-plugins.md). compilePlugins() runs on the GUI thread;
// runInserts() and delayRoute() run inside render() and never allocate,
// lock or perform I/O (AudioPluginProcessor waits on a futex only).
#include "audio_graph.h"

#include "audio_plugin_ipc_p.h"

#include <algorithm>

namespace QindaTK {

static_assert(AudioRenderGraph::kChunk <= PluginIpc::kMaxFrames,
              "a mixing chunk must fit one plugin block");

void AudioRenderGraph::compilePlugins(const Sources &sources)
{
    const EngineScheduleSnapshot *schedule = sources.schedule.get();
    if (sources.plugins) {
        for (Node &node : m_nodes) {
            node.insertFirst = m_inserts.size();
            for (const AudioPluginInsertSpec &spec : sources.plugins->entries) {
                if (spec.channelId != node.channelId) continue;
                Insert insert;
                insert.processor = spec.processor;
                insert.bypassed = spec.bypassed;
                insert.latency = spec.processor ? std::max(0, spec.latency) : 0;
                // The first instrument of a MIDI track plays its notes; the
                // built-in test instrument is silent then.
                insert.notes = spec.notes && node.midi && !node.pluginInstrument;
                if (insert.notes) node.pluginInstrument = true;
                insert.parameterFirst = m_insertParameters.size();
                for (const auto &[index, parameterId] : spec.parameters)
                    if (const int curve = addCurve(schedule, parameterId); curve >= 0)
                        m_insertParameters.push_back({index, curve});
                insert.parameterCount = m_insertParameters.size() - insert.parameterFirst;
                node.insertLatency += insert.latency;
                m_inserts.push_back(std::move(insert));
            }
            node.insertCount = m_inserts.size() - node.insertFirst;
        }
    }

    // AGENT-CONTRACT (D-192): latency compensation. A channel's output is
    // late by its sources' arrival plus its own inserts; every route into a
    // destination waits until the latest route into it arrives, so every
    // path reaches each bus, the master and each port aligned. Routes only
    // lead to later nodes, so one pass in processing order is exact.
    std::vector<int> arrival(m_nodes.size(), 0);
    std::vector<int> portArrival(m_ports.size(), 0);
    std::vector<int> output(m_nodes.size(), 0);
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        output[i] = arrival[i] + m_nodes[i].insertLatency;
        for (const Route &route : m_nodes[i].routes) {
            if (route.node >= 0) arrival[std::size_t(route.node)] = std::max(arrival[std::size_t(route.node)], output[i]);
            else if (route.port >= 0) portArrival[std::size_t(route.port)] = std::max(portArrival[std::size_t(route.port)], output[i]);
        }
    }
    std::size_t delaySamples = 0;
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        Node &node = m_nodes[i];
        for (Route &route : node.routes) {
            const int target = route.node >= 0 ? arrival[std::size_t(route.node)]
                               : route.port >= 0 ? portArrival[std::size_t(route.port)] : output[i];
            route.delay = std::max(0, target - output[i]);
            if (route.delay == 0) continue;
            route.delayBuffer = delaySamples;
            route.delaySlot = m_delayCursor.size();
            m_delayCursor.push_back(0);
            delaySamples += std::size_t(node.lanes) * std::size_t(route.delay);
        }
    }
    m_delay.assign(delaySamples, 0.0);
    for (const int late : portArrival) m_pluginLatency = std::max(m_pluginLatency, late);
}

int AudioRenderGraph::insertLatency(const QString &channelId) const
{
    for (const Node &node : m_nodes)
        if (node.channelId == channelId) return node.insertLatency;
    return -1;
}

int AudioRenderGraph::compensation(const QString &channelId) const
{
    for (const Node &node : m_nodes)
        if (node.channelId == channelId) return node.routes.empty() ? 0 : node.routes.front().delay;
    return -1;
}

void AudioRenderGraph::runInserts(const Node &node, qint64 start, int frames, double *const in[2],
                                  const MidiLaunchState *launch) const noexcept
{
    for (std::size_t k = 0; k < node.insertCount; ++k) {
        const Insert &insert = m_inserts[node.insertFirst + k];
        AudioPluginProcessor *processor = insert.processor.get();
        if (!processor) continue; // a missing plugin passes the signal unchanged
        if (processor->prepare(frames, m_offline, insert.bypassed)) {
            // AGENT-NOTE: parameter values are sent at block starts (at most
            // kChunk frames apart), not per sample; docs/audio-plugins.md.
            for (std::size_t p = 0; p < insert.parameterCount; ++p) {
                const InsertParameter &parameter = m_insertParameters[insert.parameterFirst + p];
                double value = 0.0;
                evaluate(parameter.curve, 0.0, start, 1, &value);
                processor->setParameter(parameter.index, value);
            }
            if (insert.notes) {
                MidiScheduledEvent events[PluginIpc::kMaxEvents];
                const int count = std::min(nodeMidiEvents(node, start, frames, events, PluginIpc::kMaxEvents, launch),
                                           PluginIpc::kMaxEvents);
                for (int e = 0; e < count; ++e) {
                    const MidiScheduledEvent &event = events[e];
                    const int at = int(event.sample - start);
                    if (event.type == MidiScheduledEvent::NoteOn)
                        processor->addNote(at, true, event.channel, event.number, double(event.value) / 65535.0);
                    else if (event.type == MidiScheduledEvent::NoteOff)
                        processor->addNote(at, false, event.channel, event.number, 0.0);
                }
            }
        }
        processor->run(start, frames, in, node.lanes);
    }
}

void AudioRenderGraph::delayRoute(const Route &route, int lanes, double *const in[2], double *const out[2],
                                  int frames) const noexcept
{
    const std::size_t delay = std::size_t(route.delay);
    int &cursor = m_delayCursor[route.delaySlot];
    for (int l = 0; l < lanes; ++l) {
        double *line = m_delay.data() + route.delayBuffer + std::size_t(l) * delay;
        std::size_t at = std::size_t(cursor);
        for (int i = 0; i < frames; ++i) {
            out[l][i] = line[at];
            line[at] = in[l][i];
            if (++at == delay) at = 0;
        }
    }
    cursor = int((std::size_t(cursor) + std::size_t(frames)) % delay);
}

} // namespace QindaTK
