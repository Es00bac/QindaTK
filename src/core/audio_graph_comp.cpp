// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioRenderGraph: take comp clips (WP-065, docs/audio-comping.md). A comp
// clip becomes one ordinary graph clip per resolved piece, reading the take's
// own media with its range limit and crossfade regions, so the device and
// offline renders play comps through the same render() as every clip.
#include "audio_graph.h"

#include "timeline_model.h"

#include <algorithm>

namespace QindaTK {

void AudioRenderGraph::addComp(Node &node, const TimelineClip &clip, const Sources &sources)
{
    const auto missing = [this](const QString &resourceId) {
        if (!m_missing.contains(resourceId)) m_missing.push_back(resourceId);
    };
    const AudioCompPlayback comp = sources.comp ? sources.comp(clip.resourceId) : AudioCompPlayback{};
    if (!comp.valid || comp.sampleRate < 1) return missing(clip.resourceId);
    const TimelineRate graphRate{m_sampleRate, 1};
    const TimelineRate compRate{comp.sampleRate, 1};
    const auto toSamples = [&](qint64 ticks) {
        return TimelineTime::rescale(ticks, m_timebase, graphRate, TimeMap::NearestAway);
    };
    // Comp samples (and take source frames, which share the comp rate) at
    // the graph rate: the identity when the device runs at the comp's rate.
    const auto atGraphRate = [&](qint64 samples) -> std::optional<qint64> {
        if (comp.sampleRate == m_sampleRate) return samples;
        return TimelineTime::rescale(samples, compRate, graphRate, TimeMap::NearestAway);
    };
    const auto windowStart = toSamples(clip.start);
    const auto windowEnd = toSamples(clip.end());
    const auto offset = toSamples(clip.sourceOffset);
    if (!windowStart || !windowEnd || !offset || *windowEnd <= *windowStart) return;
    // AGENT-CONTRACT (D-174): the clip's sourceOffset is the comp position at
    // its start, exactly as a media clip's is the source position.
    const qint64 origin = *windowStart - *offset;
    for (const AudioCompPlayback::Part &part : comp.parts) {
        AudioBufferPtr media = sources.media ? sources.media(part.resourceId, m_sampleRate) : nullptr;
        if (!media || media->frames == 0 || media->channels < 1) {
            missing(part.resourceId);
            continue;
        }
        const AudioCompPiece &piece = part.piece;
        const auto playStart = atGraphRate(piece.playStart);
        const auto playEnd = atGraphRate(piece.playEnd);
        const auto sourceOffset = atGraphRate(part.sourceOffset);
        const auto first = atGraphRate(part.sourceStart);
        const auto last = atGraphRate(part.sourceEnd);
        const auto fadeInStart = atGraphRate(piece.fadeInStart);
        const auto fadeInEnd = atGraphRate(piece.fadeInEnd);
        const auto fadeOutStart = atGraphRate(piece.fadeOutStart);
        const auto fadeOutEnd = atGraphRate(piece.fadeOutEnd);
        if (!playStart || !playEnd || !sourceOffset || !first || !last || !fadeInStart || !fadeInEnd
            || !fadeOutStart || !fadeOutEnd)
            continue;
        Clip entry;
        entry.start = std::max(origin + *playStart, *windowStart);
        entry.end = std::min(origin + *playEnd, *windowEnd);
        if (entry.end <= entry.start) continue;
        // The media frame at graph sample g is sourceOffset + (g - origin).
        entry.sourceStart = *sourceOffset + (entry.start - origin);
        entry.media = std::move(media);
        entry.mediaFirst = *first;
        entry.mediaEnd = *last;
        // Fade regions keep their unclipped extent, so trimming the comp clip
        // never changes a weight, only which samples are heard.
        entry.fadeInStart = origin + *fadeInStart;
        entry.fadeInEnd = origin + *fadeInEnd;
        entry.fadeOutStart = origin + *fadeOutStart;
        entry.fadeOutEnd = origin + *fadeOutEnd;
        entry.fadeShape = comp.fadeShape;
        m_length = std::max(m_length, entry.end);
        node.clips.push_back(std::move(entry));
    }
}

} // namespace QindaTK
