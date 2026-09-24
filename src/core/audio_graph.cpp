// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_graph.h"

#include "timeline_model.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace QindaTK {
namespace {

std::atomic<quint64> s_generations{0};

bool isAudio(const QString &kind)
{
    return kind.isEmpty() || kind == QLatin1String("audio");
}

// Cubic segments go through CurveEvaluator, which builds a curve per call;
// the engine instead reads kCubicSteps linear pieces evaluated here.
QVector<AutomationPoint> flattened(const AutomationParameter &parameter, const QVector<AutomationPoint> &points)
{
    QVector<AutomationPoint> out;
    for (qsizetype i = 0; i < points.size(); ++i) {
        const AutomationPoint &point = points.at(i);
        if (point.interpolation != CurveInterpolation::CubicBezier || i + 1 == points.size()) {
            AutomationPoint copy = point;
            if (copy.interpolation == CurveInterpolation::CubicBezier) copy.interpolation = CurveInterpolation::Linear;
            out.push_back(copy);
            continue;
        }
        const auto span = TimelineTime::subtract(points.at(i + 1).ticks, point.ticks);
        for (int step = 0; span && step < AudioRenderGraph::kCubicSteps; ++step) {
            const auto offset = TimelineTime::mulDiv(*span, quint64(step), AudioRenderGraph::kCubicSteps, TimeMap::Floor);
            AutomationPoint piece;
            piece.id = point.id;
            piece.ticks = point.ticks + offset.value_or(0);
            piece.value = AutomationCurve::valueAt(parameter, points, piece.ticks);
            piece.interpolation = CurveInterpolation::Linear;
            if (out.isEmpty() || out.last().ticks < piece.ticks) out.push_back(piece);
        }
    }
    return out;
}

} // namespace

QString AudioRenderGraph::gainParameterId(const QString &trackId)
{
    return QStringLiteral("track:%1/gain").arg(trackId);
}

int AudioRenderGraph::clipCount() const
{
    int count = 0;
    for (const Track &track : m_tracks) count += int(track.clips.size());
    return count;
}

std::shared_ptr<const AudioRenderGraph> AudioRenderGraph::compile(const Sources &sources)
{
    std::shared_ptr<AudioRenderGraph> graph(new AudioRenderGraph);
    graph->m_generation = ++s_generations;
    graph->m_sampleRate = sources.sampleRate;
    graph->m_schedule = sources.schedule;
    const TimelineModel *timeline = sources.timeline;
    if (!timeline || sources.sampleRate < 1) return graph;
    graph->m_timebase = timeline->timebase();
    const TimelineRate rate{sources.sampleRate, 1};
    const auto toSamples = [&](qint64 ticks) {
        return TimelineTime::rescale(ticks, graph->m_timebase, rate, TimeMap::NearestAway);
    };
    bool anySolo = false;
    for (const QUuid &id : timeline->trackOrder())
        if (const TimelineTrack *track = timeline->trackRecord(id); track && track->solo && isAudio(track->kind))
            anySolo = true;
    for (const QUuid &trackId : timeline->trackOrder()) {
        const TimelineTrack *record = timeline->trackRecord(trackId);
        if (!record || !isAudio(record->kind) || record->muted || (anySolo && !record->solo)) continue;
        Track track;
        for (const auto &entry : timeline->trackIndex(trackId)) {
            const TimelineClip *clip = timeline->clipRecord(entry.id);
            if (!clip || clip->resourceId.isEmpty() || !isAudio(clip->kind)) continue;
            AudioBufferPtr media = sources.media ? sources.media(clip->resourceId, sources.sampleRate) : nullptr;
            if (!media || media->frames == 0 || media->channels < 1) {
                if (!graph->m_missing.contains(clip->resourceId)) graph->m_missing.push_back(clip->resourceId);
                continue;
            }
            const auto start = toSamples(clip->start);
            const auto end = toSamples(clip->end());
            const auto offset = toSamples(clip->sourceOffset);
            if (!start || !end || !offset || *end <= *start) continue;
            track.clips.push_back({*start, *end, *offset, std::move(media)});
            graph->m_length = std::max(graph->m_length, *end);
        }
        std::sort(track.clips.begin(), track.clips.end(),
                  [](const Clip &a, const Clip &b) { return a.start < b.start; });
        const QString id = trackId.toString(QUuid::WithoutBraces);
        const auto *gain = sources.schedule ? sources.schedule->parameter(gainParameterId(id)) : nullptr;
        if (gain) {
            track.gainParameter = gain->parameter;
            track.decibels = gain->parameter.range.unitId.compare(QLatin1String("dB"), Qt::CaseInsensitive) == 0;
            const auto linear = [&](double value) { return track.decibels ? std::pow(10.0, value / 20.0) : value; };
            if (gain->overriding) {
                track.constantGain = linear(gain->liveValue);
            } else if (gain->points.isEmpty()) {
                track.constantGain = linear(gain->parameter.defaultValue);
            } else {
                track.gainPoints = flattened(gain->parameter, gain->points);
                track.automated = true;
            }
        }
        if (!track.clips.empty()) graph->m_tracks.push_back(std::move(track));
    }
    return graph;
}

void AudioRenderGraph::render(qint64 start, int frames, float *out, int channels, double *scratch) const noexcept
{
    if (frames <= 0 || frames > kMaxBlock || channels <= 0) return;
    const qint64 stop = start + frames;
    const TimelineRate rate{m_sampleRate, 1};
    for (const Track &track : m_tracks) {
        if (track.automated) {
            for (int i = 0; i < frames; ++i) {
                const auto ticks = TimelineTime::rescale(start + i, rate, m_timebase, TimeMap::NearestAway);
                const double value = ticks ? AutomationCurve::valueAt(track.gainParameter, track.gainPoints, *ticks)
                                           : track.gainParameter.defaultValue;
                scratch[i] = track.decibels ? std::pow(10.0, value / 20.0) : value;
            }
        }
        for (const Clip &clip : track.clips) {
            if (clip.start >= stop) break;
            if (clip.end <= start) continue;
            const AudioBuffer &media = *clip.media;
            const qint64 from = std::max(clip.start, start);
            const qint64 to = std::min(clip.end, stop);
            for (qint64 sample = from; sample < to; ++sample) {
                const qint64 frame = clip.sourceStart + (sample - clip.start);
                if (frame < 0 || frame >= media.frames) continue;
                const int i = int(sample - start);
                const double gain = track.automated ? scratch[i] : track.constantGain;
                for (int c = 0; c < channels; ++c)
                    out[i * channels + c] += float(double(media.channel(std::min(c, media.channels - 1))[frame]) * gain);
            }
        }
    }
}

} // namespace QindaTK
