// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioSession: recorded takes, loop passes, punch and take comps (WP-065,
// docs/audio-comping.md). The comp content lives in `comps`; the timeline
// holds the clip that places it (resourceId == comp id).
#include "audio_session.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace QindaTK {
namespace {

bool isAudio(const QString &kind)
{
    return kind.isEmpty() || kind == QLatin1String("audio");
}

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

qint64 number(const QVariantMap &map, const char *key)
{
    return map.value(QString::fromLatin1(key)).toString().toLongLong();
}

} // namespace

void AudioSession::setPassTakes(bool enabled)
{
    if (m_passTakes == enabled) return;
    m_passTakes = enabled;
    emit passTakesChanged();
}

QStringList AudioSession::missingMedia() const
{
    QStringList missing;
    const auto add = [&](const QString &id) {
        if (!missing.contains(id)) missing.push_back(id);
    };
    for (const QUuid &track : m_timeline->trackOrder())
        for (const auto &entry : m_timeline->trackIndex(track)) {
            const TimelineClip *clip = m_timeline->clipRecord(entry.id);
            if (!clip || !isAudio(clip->kind) || clip->resourceId.isEmpty()) continue;
            if (!AudioRenderGraph::isCompResource(clip->resourceId)) {
                if (!m_decoded.contains(clip->resourceId)) add(clip->resourceId);
                continue;
            }
            // A comp is missing only by its takes' media (or when unknown).
            const AudioTakeComp *comp = m_comps->comp(clip->resourceId);
            if (!comp) add(clip->resourceId);
            for (const AudioTake &take : comp ? comp->takes : QVector<AudioTake>{})
                if (!m_decoded.contains(take.resourceId)) add(take.resourceId);
        }
    return missing;
}

QString AudioSession::generateTestTake(const QString &file, const QVariantMap &options)
{
    clearError();
    const int rate = options.value(QStringLiteral("sampleRate"), sampleRate()).toInt();
    AudioBuffer buffer;
    buffer.sampleRate = rate;
    buffer.channels = 1;
    for (const QVariant &value : options.value(QStringLiteral("segments")).toList())
        buffer.frames += std::max<qint64>(0, value.toMap().value(QStringLiteral("frames")).toLongLong());
    if (rate < 1 || buffer.frames < 1 || buffer.frames > qint64(rate) * 600) {
        fail(tr("A test take needs a sample rate and 1 frame to 10 minutes of segments"));
        return {};
    }
    buffer.samples.resize(size_t(buffer.frames));
    qint64 at = 0;
    double phase = 0.0;
    for (const QVariant &value : options.value(QStringLiteral("segments")).toList()) {
        const QVariantMap segment = value.toMap();
        const qint64 frames = std::max<qint64>(0, segment.value(QStringLiteral("frames")).toLongLong());
        const double step = 2.0 * std::numbers::pi * segment.value(QStringLiteral("frequency"), 220.0).toDouble() / rate;
        const double amplitude = std::clamp(segment.value(QStringLiteral("amplitude"), 0.5).toDouble(), 0.0, 1.0);
        const double pulse = 2.0 * std::numbers::pi * segment.value(QStringLiteral("pulse"), 3.0).toDouble() / rate;
        for (qint64 i = 0; i < frames; ++i, ++at, phase += step)
            buffer.samples[size_t(at)] = float(amplitude * (0.775 + 0.225 * std::sin(pulse * double(i))) * std::sin(phase));
    }
    const QString base = m_mediaDirectory.isEmpty() ? QDir::tempPath() : m_mediaDirectory;
    const QString name = file.isEmpty()
        ? QStringLiteral("test-take-%1.wav").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)) : file;
    const QString path = QDir(base).absoluteFilePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QString error;
    if (!AudioFile::writeWav(path, buffer, &error)) {
        fail(error);
        return {};
    }
    return path;
}

QString AudioSession::compForClip(const QString &clipId) const
{
    const TimelineClip *clip = m_timeline->clipRecord(QUuid::fromString(clipId));
    return clip && AudioRenderGraph::isCompResource(clip->resourceId) ? clip->resourceId : QString();
}

QString AudioSession::compClipOver(const QString &trackId, int rate, qint64 from, qint64 to, bool media) const
{
    const TimelineRate samples{rate, 1};
    for (const auto &entry : m_timeline->trackIndex(QUuid::fromString(trackId))) {
        const TimelineClip *clip = m_timeline->clipRecord(entry.id);
        if (!clip || !isAudio(clip->kind) || clip->resourceId.isEmpty()) continue;
        const bool comp = AudioRenderGraph::isCompResource(clip->resourceId);
        if (comp == media) continue;
        if (comp) {
            const AudioTakeComp *content = m_comps->comp(clip->resourceId);
            if (!content || content->sampleRate != rate) continue;
        } else {
            const AudioBufferPtr source = m_decoded.contains(clip->resourceId) ? m_edits->buffer(clip->resourceId) : nullptr;
            if (!source || source->sampleRate != rate) continue;
        }
        const auto start = TimelineTime::rescale(clip->start, m_timeline->timebase(), samples, TimeMap::NearestAway);
        const auto end = TimelineTime::rescale(clip->end(), m_timeline->timebase(), samples, TimeMap::NearestAway);
        if (start && end && *start < to && *end > from) return clip->id.toString(QUuid::WithoutBraces);
    }
    return {};
}

QVariantMap AudioSession::placeTake(const QVariantMap &take)
{
    clearError();
    const QString path = take.value(QStringLiteral("path")).toString();
    const qint64 frames = number(take, "frames");
    const qint64 start = number(take, "startSample");
    const int latency = take.value(QStringLiteral("latencyFrames")).toInt();
    const qint64 recorded = take.contains(QStringLiteral("recordedSample")) ? number(take, "recordedSample")
                                                                             : start + latency;
    const int rate = take.value(QStringLiteral("sampleRate")).toInt();
    QString error = take.value(QStringLiteral("error")).toString();
    const QString trackId = armedTrack();
    // Latency compensation can place the first samples before the session
    // start; those are trimmed as a source offset, never moved.
    const qint64 skip = start < 0 ? -start : 0;
    if (error.isEmpty() && (frames <= skip || rate < 1)) error = tr("The take has no samples after latency compensation");
    if (error.isEmpty() && trackId.isEmpty()) error = tr("No track is armed; the take was kept at %1").arg(path);
    const QString resourceId = QStringLiteral("take:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (error.isEmpty() && !m_resources->registerResource(resourceId, QUrl::fromLocalFile(path).toString()))
        error = m_resources->lastError();
    if (error.isEmpty() && !decode(resourceId)) error = tr("The recorded take cannot be read back from %1").arg(path);
    if (!error.isEmpty()) {
        fail(error);
        return failure(error);
    }

    // AGENT-NOTE (D-177): file frame f was captured at the transport sample
    // of frame f - latency in the stream (what the performer heard), so a
    // jump at file frame F starts a new pass at F + latency, heard from the
    // jump's position. Frames before the first jump + latency are pass 1.
    QVector<TakePass> passes;
    qint64 fileStart = 0;
    qint64 heard = recorded - latency;
    for (const QVariant &value : take.value(QStringLiteral("jumps")).toList()) {
        const QVariantMap jump = value.toMap();
        const qint64 boundary = std::clamp<qint64>(number(jump, "frame") + latency, fileStart, frames);
        if (boundary > fileStart) passes.push_back({fileStart, boundary, heard});
        fileStart = boundary;
        heard = number(jump, "position");
    }
    if (frames > fileStart) passes.push_back({fileStart, frames, heard});
    const bool reliable = number(take, "jumpsDropped") == 0 && number(take, "droppedFrames") == 0;
    const bool usePasses = m_passTakes && reliable && passes.size() >= 2;
    std::optional<std::pair<qint64, qint64>> punch;
    if (take.value(QStringLiteral("punchEnabled")).toBool() && number(take, "punchEnd") > number(take, "punchStart"))
        punch = std::pair{number(take, "punchStart"), number(take, "punchEnd")};
    if (!usePasses) passes = {{0, frames, start}};

    QString clipId;
    QString compId;
    if (usePasses || punch) {
        const QVariantMap placed = placeTakeInComp(trackId, resourceId, rate, passes, punch);
        if (!placed.value(QStringLiteral("ok")).toBool()) {
            error = placed.value(QStringLiteral("error")).toString();
        } else {
            clipId = placed.value(QStringLiteral("clipId")).toString();
            compId = placed.value(QStringLiteral("compId")).toString();
        }
    } else {
        // One linear clip (WP-060 behaviour).
        const auto toTicks = [&](qint64 samples) {
            return TimelineTime::rescale(samples, {rate, 1}, m_timeline->timebase(), TimeMap::NearestAway);
        };
        const auto at = toTicks(std::max<qint64>(0, start));
        const auto offset = toTicks(skip);
        const auto total = toTicks(frames);
        if (!at || !offset || !total) {
            error = tr("The take is out of the timeline range");
        } else {
            TimelineClip clip;
            clip.id = QUuid::createUuid();
            clip.duration = *total - *offset;
            clip.sourceOffset = *offset;
            clip.sourceDuration = *total;
            clip.resourceId = resourceId;
            clip.kind = QStringLiteral("audio");
            clip.label = tr("Take");
            clipId = insertClip(clip, trackId, *at, &error);
        }
    }
    if (!error.isEmpty()) {
        fail(error);
        return failure(error);
    }
    emit mediaChanged();
    emit takeRecorded(clipId, resourceId, take);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("clipId"), clipId},
            {QStringLiteral("resourceId"), resourceId}, {QStringLiteral("compId"), compId}};
}

QVariantMap AudioSession::placeTakeInComp(const QString &trackId, const QString &resourceId, int rate,
                                          const QVector<TakePass> &passes,
                                          std::optional<std::pair<qint64, qint64>> punch)
{
    const TimelineRate samples{rate, 1};
    const auto toTicks = [&](qint64 value) {
        return TimelineTime::rescale(value, samples, m_timeline->timebase(), TimeMap::NearestAway);
    };
    const auto toSamples = [&](qint64 ticks) {
        return TimelineTime::rescale(ticks, m_timeline->timebase(), samples, TimeMap::NearestAway);
    };
    qint64 low = passes.first().heard;
    qint64 high = low;
    for (const TakePass &pass : passes) {
        low = std::min(low, pass.heard);
        high = std::max(high, pass.heard + (pass.fileEnd - pass.fileStart));
    }
    const qint64 rangeStart = punch ? std::max(punch->first, low) : low;
    const qint64 rangeEnd = punch ? std::min(punch->second, high) : high;
    if (rangeEnd <= rangeStart) return failure(tr("The recording does not reach the punch range"));

    // Takes relative to a comp origin, and the ranges they are selected
    // over inside a window (timeline samples), later passes winning.
    const auto build = [&](qint64 origin, qint64 windowStart, qint64 windowEnd, qint64 length, int first,
                           QVector<AudioTake> &takes, QVector<AudioTakeCompModel::Selection> &selections) {
        for (qsizetype k = 0; k < passes.size(); ++k) {
            const TakePass &pass = passes.at(k);
            AudioTake take;
            take.id = AudioComp::newId("take");
            take.resourceId = resourceId;
            take.label = tr("Take %1").arg(first + int(k));
            take.sourceStart = pass.fileStart;
            take.sourceEnd = pass.fileEnd;
            take.sourceOffset = pass.fileStart - (pass.heard - origin);
            takes.push_back(take);
            const qint64 from = std::max({pass.heard, rangeStart, windowStart, origin});
            const qint64 to = std::min({pass.heard + (pass.fileEnd - pass.fileStart), rangeEnd, windowEnd, origin + length});
            if (from < to) selections.push_back({take.id, from - origin, to - origin});
        }
    };
    const auto result = [](const QString &clipId, const QString &compId) {
        return QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("clipId"), clipId},
                           {QStringLiteral("compId"), compId}};
    };
    QString error;

    // 1. A comp clip already there takes the new passes as one comp step.
    if (const QString clipId = compClipOver(trackId, rate, rangeStart, rangeEnd, false); !clipId.isEmpty()) {
        const TimelineClip *clip = m_timeline->clipRecord(QUuid::fromString(clipId));
        const AudioTakeComp *comp = m_comps->comp(clip->resourceId);
        const auto windowStart = toSamples(clip->start);
        const auto windowEnd = toSamples(clip->end());
        const auto offset = toSamples(clip->sourceOffset);
        if (!windowStart || !windowEnd || !offset) return failure(tr("The comp clip is out of the timeline range"));
        QVector<AudioTake> takes;
        QVector<AudioTakeCompModel::Selection> selections;
        build(*windowStart - *offset, *windowStart, *windowEnd, comp->length, int(comp->takes.size()) + 1, takes,
              selections);
        const QString compId = comp->id;
        if (!m_comps->record(compId, takes, selections, &error)) return failure(error);
        return result(clipId, compId);
    }

    // 2. Punching into a media clip turns it into a comp whose first take is
    //    the clip's source, in one timeline step (a source switch).
    const QString mediaClipId = punch ? compClipOver(trackId, rate, rangeStart, rangeEnd, true) : QString();
    if (!mediaClipId.isEmpty()) {
        const TimelineClip clip = *m_timeline->clipRecord(QUuid::fromString(mediaClipId));
        const AudioBufferPtr source = m_edits->buffer(clip.resourceId);
        const auto windowStart = toSamples(clip.start);
        const auto windowEnd = toSamples(clip.end());
        const auto offset = toSamples(clip.sourceOffset);
        const auto available = source ? toTicks(source->frames) : std::nullopt;
        if (!windowStart || !windowEnd || !offset || !available) return failure(tr("The clip cannot hold takes"));
        AudioTakeComp comp;
        comp.id = AudioComp::newId("comp");
        comp.sampleRate = rate;
        comp.length = source->frames;
        comp.fadeSamples = rate / 200;
        comp.takes.push_back({AudioComp::newId("take"), clip.resourceId,
                              clip.label.isEmpty() ? tr("Take 1") : clip.label, 0, 0, source->frames});
        const qint64 origin = *windowStart - *offset;
        const qint64 kept = std::min(source->frames, *offset + (*windowEnd - *windowStart));
        if (*offset < kept) error = AudioComp::select(comp, comp.takes.first().id, *offset, kept);
        QVector<AudioTake> takes;
        QVector<AudioTakeCompModel::Selection> selections;
        build(origin, *windowStart, *windowEnd, comp.length, 2, takes, selections);
        if (error.isEmpty()) error = AudioComp::addTakes(comp, takes);
        for (const auto &selection : std::as_const(selections))
            if (error.isEmpty()) error = AudioComp::select(comp, selection.takeId, selection.start, selection.end);
        if (!error.isEmpty() || !m_comps->insert(comp, &error)) return failure(error);
        TimelineEditRequest request;
        request.kind = TimelineEditKind::SwitchSource;
        request.clipIds = {clip.id};
        request.resourceId = comp.id;
        request.sourceOffset = clip.sourceOffset;
        request.sourceDuration = *available;
        request.label = tr("Punch in");
        TimelineEditInput input;
        input.pointerTicks = clip.start;
        input.snap = false;
        if (const OperationError problem = m_timeline->apply(request, input)) {
            m_comps->remove(comp.id);
            return failure(problem.message);
        }
        return result(mediaClipId, comp.id);
    }

    // 3. Otherwise a new comp clip spans the passes.
    AudioTakeComp comp;
    comp.id = AudioComp::newId("comp");
    comp.sampleRate = rate;
    const qint64 origin = std::max<qint64>(0, low);
    comp.length = high - origin;
    comp.fadeSamples = rate / 200;
    QVector<AudioTake> takes;
    QVector<AudioTakeCompModel::Selection> selections;
    build(origin, origin, high, comp.length, 1, takes, selections);
    error = AudioComp::addTakes(comp, takes);
    for (const auto &selection : std::as_const(selections))
        if (error.isEmpty()) error = AudioComp::select(comp, selection.takeId, selection.start, selection.end);
    const auto at = toTicks(origin);
    const auto duration = toTicks(comp.length);
    if (error.isEmpty() && (comp.length < 1 || !at || !duration || *duration < 1))
        error = tr("The take is out of the timeline range");
    if (!error.isEmpty() || !m_comps->insert(comp, &error)) return failure(error);
    TimelineClip clip;
    clip.id = QUuid::createUuid();
    clip.duration = *duration;
    clip.sourceDuration = *duration;
    clip.resourceId = comp.id;
    clip.kind = QStringLiteral("audio");
    clip.label = tr("Takes");
    const QString clipId = insertClip(clip, trackId, *at, &error);
    if (clipId.isEmpty()) {
        m_comps->remove(comp.id);
        return failure(error);
    }
    return result(clipId, comp.id);
}

} // namespace QindaTK
