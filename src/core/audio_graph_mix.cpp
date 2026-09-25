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
        const bool faded = clip.faded();
        for (qint64 sample = from; sample < to; ++sample) {
            const qint64 frame = clip.sourceStart + (sample - clip.start);
            if (frame < 0 || frame >= media.frames || frame < clip.mediaFirst || frame >= clip.mediaEnd) continue;
            const int i = int(sample - start);
            double a = double(first[frame]);
            double b = double(second[frame]);
            if (faded) {
                // Comp crossfades (D-175): computed per sample, no tables.
                const double w = AudioComp::fadeWeight(clip.fadeShape, true, sample, clip.fadeInStart, clip.fadeInEnd)
                               * AudioComp::fadeWeight(clip.fadeShape, false, sample, clip.fadeOutStart, clip.fadeOutEnd);
                a *= w;
                b *= w;
            }
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
// A mono or stereo source into a Quad or 5.1 destination is placed by pan and
// depth (mixSpatial); a surround source converts through its fixed matrix.
void AudioRenderGraph::mix(const Node &node, const Route &route, double *const in[], int frames,
                           const Controls &controls) const noexcept
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
    const double *gain = controls.gain;
    const double *level = controls.level;
    if (node.lanes > 2) {
        double *out[AudioMixer::kMaxLanes] = {};
        for (int d = 0; d < destLanes; ++d) out[d] = lane(buffer, d);
        for (int i = 0; i < frames; ++i) {
            const double f = (route.preFader ? 1.0 : gain[i]) * level[i];
            for (int d = 0; d < destLanes; ++d) {
                const double *row = route.matrix.data() + std::size_t(d) * AudioMixer::kMaxLanes;
                double sum = 0.0;
                for (int l = 0; l < node.lanes; ++l) sum += row[l] * in[l][i];
                out[d][i] += sum * f;
            }
        }
        return;
    }
    if (destLanes > 2) {
        double *out[AudioMixer::kMaxLanes] = {};
        for (int d = 0; d < destLanes; ++d) out[d] = lane(buffer, d);
        mixSpatial(node, out, destLanes, in, frames, controls, route.preFader);
        return;
    }
    const double *pan = controls.pan;
    const double *width = controls.width;
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

// D-111 spatial panning: a mono source is one point at (pan, depth); each
// side of a stereo source (after width) is a point, left at pan - 1 and
// right at pan + 1 (clamped), so a centred stereo pair keeps its sides.
void AudioRenderGraph::mixSpatial(const Node &node, double *const out[], int destLanes, double *const in[], int frames,
                                  const Controls &controls, bool preFader) const noexcept
{
    const auto layout = AudioMixer::layoutForLanes(destLanes).value_or(AudioLayout::Quad);
    double first[AudioMixer::kMaxLanes] = {};
    double second[AudioMixer::kMaxLanes] = {};
    const auto place = [&](double x, double y) noexcept {
        if (node.lanes == 1) {
            AudioMixer::spatialGains(layout, m_panLaw, x, y, first);
            return;
        }
        AudioMixer::spatialGains(layout, m_panLaw, std::max(-1.0, x - 1.0), y, first);
        AudioMixer::spatialGains(layout, m_panLaw, std::min(1.0, x + 1.0), y, second);
    };
    const bool fixed = constant(node.pan) && constant(node.depth);
    if (fixed) place(controls.pan[0], controls.depth[0]);
    for (int i = 0; i < frames; ++i) {
        if (!fixed) place(controls.pan[i], controls.depth[i]);
        const double f = (preFader ? 1.0 : controls.gain[i]) * controls.level[i];
        double l = in[0][i];
        double r = node.lanes > 1 ? in[1][i] : 0.0;
        if (node.lanes > 1 && controls.width[i] != 1.0) {
            const double m = (l + r) * 0.5;
            const double s = (l - r) * 0.5 * controls.width[i];
            l = m + s;
            r = m - s;
        }
        for (int d = 0; d < destLanes; ++d) out[d][i] += (l * first[d] + r * second[d]) * f;
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
    double *depth = scratch + 4 * kChunk;
    const Controls controls{gain, pan, width, depth, level};
    for (const Node &node : m_nodes) {
        double *in[AudioMixer::kMaxLanes] = {};
        for (int l = 0; l < AudioMixer::kMaxLanes; ++l) in[l] = lane(node.buffer, std::min(l, node.lanes - 1));
        if (!node.clips.empty()) addClips(node, start, frames, in);
        if (node.midi) addMidi(node, start, frames, in, launch);
        if (!node.audible) continue; // muted: feeds nothing, and its meter reads silence
        evaluate(node.gain, 1.0, start, frames, gain);
        if (meters && node.meterSlot >= 0)
            for (int l = 0; l < node.lanes; ++l) meters->add(node.meterSlot, l, in[l], gain, frames);
        if (node.routes.empty()) continue;
        evaluate(node.pan, 0.0, start, frames, pan);
        evaluate(node.width, 1.0, start, frames, width);
        evaluate(node.depth, 1.0, start, frames, depth);
        for (const Route &route : node.routes) {
            evaluate(route.level, 1.0, start, frames, level);
            mix(node, route, in, frames, controls);
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
        for (int l = 0; l < port.lanes; ++l) {
            const double *samples = lane(port.buffer, l);
            for (int i = 0; i < frames; ++i) out[std::size_t(i) * stride + std::size_t(port.channel + l)] += float(samples[i]);
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
