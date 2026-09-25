// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioRenderGraph::render: the only mixing code (device and offline). Every
// function here runs on the device thread: no allocation, lock or I/O.
#include "audio_graph.h"

#include "audio_channel_meters.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {

double *AudioRenderGraph::lane(std::size_t buffer, int index) const noexcept
{
    return m_work.data() + (buffer + std::size_t(index)) * std::size_t(kChunk);
}

void AudioRenderGraph::evaluate(int curve, double fallback, qint64 start, int frames, double *out) const noexcept
{
    if (curve < 0) {
        std::fill_n(out, frames, fallback);
        return;
    }
    const Curve &entry = m_curves[std::size_t(curve)];
    if (!entry.automated) {
        std::fill_n(out, frames, entry.constant);
        return;
    }
    const TimelineRate rate{m_sampleRate, 1};
    for (int i = 0; i < frames; ++i) {
        const auto ticks = TimelineTime::rescale(start + i, rate, m_timebase, TimeMap::NearestAway);
        out[i] = entry.convert(ticks ? AutomationCurve::valueAt(entry.parameter, entry.points, *ticks)
                                     : entry.parameter.defaultValue);
    }
}

// Media to channel lanes (D-111): a mono file plays on both lanes of a stereo
// channel; a mono channel takes the average of the first two file channels.
void AudioRenderGraph::addClips(const Node &node, qint64 start, int frames, double *const in[2]) const noexcept
{
    const qint64 stop = start + frames;
    for (const Clip &clip : node.clips) {
        if (clip.start >= stop) break;
        if (clip.end <= start) continue;
        const AudioBuffer &media = *clip.media;
        const float *first = media.channel(0);
        const float *second = media.channel(std::min(1, media.channels - 1));
        const qint64 from = std::max(clip.start, start);
        const qint64 to = std::min(clip.end, stop);
        for (qint64 sample = from; sample < to; ++sample) {
            const qint64 frame = clip.sourceStart + (sample - clip.start);
            if (frame < 0 || frame >= media.frames) continue;
            const int i = int(sample - start);
            const double a = double(first[frame]);
            const double b = double(second[frame]);
            if (node.lanes == 1) {
                in[0][i] += media.channels == 1 ? a : (a + b) * 0.5;
            } else {
                in[0][i] += a;
                in[1][i] += b;
            }
        }
    }
}

// One route of one channel: fader (unless pre-fader), send level, then the
// panner for the lane pair (D-111). Mono to stereo follows the pan law; a
// stereo source applies width (mid/side) and balance; stereo to mono averages.
void AudioRenderGraph::mix(const Node &node, const Route &route, double *const in[2], int frames, const double *gain,
                           const double *pan, const double *width, const double *level) const noexcept
{
    std::size_t buffer = 0;
    int destLanes = 2;
    if (route.node >= 0) {
        const Node &target = m_nodes[std::size_t(route.node)];
        buffer = target.buffer;
        destLanes = target.lanes;
    } else {
        const PortTarget &port = m_ports[std::size_t(route.port)];
        buffer = port.buffer;
        destLanes = port.lanes;
    }
    double *left = lane(buffer, 0);
    double *right = lane(buffer, destLanes > 1 ? 1 : 0);
    double c0 = 1.0;
    double c1 = 1.0;
    const auto coefficients = [&](double position) noexcept {
        if (node.lanes == 1 && destLanes == 2) AudioMixer::panGains(m_panLaw, position, c0, c1);
        else if (node.lanes == 2) AudioMixer::balanceGains(position, c0, c1);
    };
    const bool fixed = constant(node.pan);
    if (fixed) coefficients(pan[0]);
    for (int i = 0; i < frames; ++i) {
        if (!fixed) coefficients(pan[i]);
        const double f = (route.preFader ? 1.0 : gain[i]) * level[i];
        if (node.lanes == 1) {
            const double x = in[0][i];
            if (destLanes == 1) {
                left[i] += x * f;
            } else {
                left[i] += x * c0 * f;
                right[i] += x * c1 * f;
            }
            continue;
        }
        double l = in[0][i];
        double r = in[1][i];
        if (width[i] != 1.0) {
            const double m = (l + r) * 0.5;
            const double s = (l - r) * 0.5 * width[i];
            l = m + s;
            r = m - s;
        }
        if (destLanes == 2) {
            left[i] += l * c0 * f;
            right[i] += r * c1 * f;
        } else {
            left[i] += (l * c0 + r * c1) * 0.5 * f;
        }
    }
}

void AudioRenderGraph::mixChunk(qint64 start, int frames, float *out, int channels, double *scratch,
                                AudioChannelMeterSink *meters, const MidiLaunchState *launch) const noexcept
{
    for (std::size_t index = 0; index < m_lanes; ++index) std::fill_n(lane(index, 0), frames, 0.0);
    double *gain = scratch;
    double *pan = scratch + kChunk;
    double *width = scratch + 2 * kChunk;
    double *level = scratch + 3 * kChunk;
    for (const Node &node : m_nodes) {
        double *in[2] = {lane(node.buffer, 0), lane(node.buffer, node.lanes > 1 ? 1 : 0)};
        if (!node.clips.empty()) addClips(node, start, frames, in);
        if (node.midi && !node.pluginInstrument) addMidi(node, start, frames, in, launch);
        if (!node.audible) continue; // muted: feeds nothing, and its meter reads silence
        if (node.insertCount) runInserts(node, start, frames, in, launch); // pre-fader (WP-066)
        evaluate(node.gain, 1.0, start, frames, gain);
        if (meters && node.meterSlot >= 0)
            for (int l = 0; l < node.lanes; ++l) meters->add(node.meterSlot, l, in[l], gain, frames);
        if (node.routes.empty()) continue;
        evaluate(node.pan, 0.0, start, frames, pan);
        evaluate(node.width, 1.0, start, frames, width);
        for (const Route &route : node.routes) {
            evaluate(route.level, 1.0, start, frames, level);
            if (route.delay <= 0) {
                mix(node, route, in, frames, gain, pan, width, level);
                continue;
            }
            // Latency compensation (D-192): this route waits for slower paths.
            double *delayed[2] = {scratch + 4 * kChunk, scratch + 5 * kChunk};
            delayRoute(route, node.lanes, in, delayed, frames);
            if (node.lanes == 1) delayed[1] = delayed[0];
            mix(node, route, delayed, frames, gain, pan, width, level);
        }
    }
    writePorts(frames, out, channels);
}

void AudioRenderGraph::writePorts(int frames, float *out, int channels) const noexcept
{
    const std::size_t stride = std::size_t(channels);
    for (const PortTarget &port : m_ports) {
        const double *a = lane(port.buffer, 0);
        const double *b = lane(port.buffer, port.lanes > 1 ? 1 : 0);
        if (port.main) {
            // "main" is the first output channels; a mono output gets the average.
            if (channels == 1) {
                for (int i = 0; i < frames; ++i) out[i] += float((a[i] + b[i]) * 0.5);
            } else {
                for (int i = 0; i < frames; ++i) {
                    out[std::size_t(i) * stride] += float(a[i]);
                    out[std::size_t(i) * stride + 1] += float(b[i]);
                }
            }
            continue;
        }
        if (port.channel + port.lanes > channels) continue;
        for (int i = 0; i < frames; ++i) {
            float *frame = out + std::size_t(i) * stride + std::size_t(port.channel);
            frame[0] += float(a[i]);
            if (port.lanes > 1) frame[1] += float(b[i]);
        }
    }
}

void AudioRenderGraph::render(qint64 start, int frames, float *out, int channels, double *scratch,
                              AudioChannelMeterSink *meters, const MidiLaunchState *launch) const noexcept
{
    if (frames <= 0 || frames > kMaxBlock || channels <= 0 || !out || !scratch || m_work.empty()) return;
    if (meters) meters->begin(m_generation, meterSlots());
    for (int done = 0; done < frames;) {
        int count = std::min(kChunk, frames - done);
        if (meters) count = std::min(count, meters->remaining());
        mixChunk(start + done, count, out + std::size_t(done) * std::size_t(channels), channels, scratch, meters, launch);
        if (meters) meters->advance(count);
        done += count;
    }
}

} // namespace QindaTK
