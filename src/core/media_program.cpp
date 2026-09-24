// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_program.h"
#include "timeline_model.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QSet>
#include <algorithm>

namespace QindaTK {
namespace {
constexpr int kMaxDimension = 8192;
const QString kSchema = QStringLiteral("qindatk.media-program/1");

QString text(qint64 value)
{
    return QString::number(value);
}

std::optional<qint64> decimal(const QJsonValue &value)
{
    return value.isString() ? TimelineTime::parseDecimal(value.toString()) : std::nullopt;
}

QJsonObject rateJson(TimelineRate rate)
{
    return {{QStringLiteral("numerator"), text(rate.numerator)},
            {QStringLiteral("denominator"), text(rate.denominator)}};
}

std::optional<TimelineRate> rateFrom(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const auto numerator = decimal(object.value(QStringLiteral("numerator")));
    const auto denominator = decimal(object.value(QStringLiteral("denominator")));
    if (!numerator || !denominator) return std::nullopt;
    return TimelineRate::normalized(*numerator, *denominator);
}

QJsonObject spanJson(const MediaClipSpan &span)
{
    return {{QStringLiteral("clipId"), span.clipId}, {QStringLiteral("resourceId"), span.resourceId},
            {QStringLiteral("uri"), span.uri}, {QStringLiteral("revision"), span.revision},
            {QStringLiteral("start"), text(span.start)}, {QStringLiteral("duration"), text(span.duration)},
            {QStringLiteral("sourceOffset"), text(span.sourceOffset)}};
}

std::optional<MediaClipSpan> spanFrom(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const auto start = decimal(object.value(QStringLiteral("start")));
    const auto duration = decimal(object.value(QStringLiteral("duration")));
    const auto offset = decimal(object.value(QStringLiteral("sourceOffset")));
    if (!start || !duration || !offset || *duration < 1 || !TimelineTime::add(*start, *duration))
        return std::nullopt;
    MediaClipSpan span;
    span.clipId = object.value(QStringLiteral("clipId")).toString();
    span.resourceId = object.value(QStringLiteral("resourceId")).toString();
    span.uri = object.value(QStringLiteral("uri")).toString();
    span.revision = object.value(QStringLiteral("revision")).toString();
    span.start = *start;
    span.duration = *duration;
    span.sourceOffset = *offset;
    return span;
}

bool validSize(const QSize &size)
{
    return size.width() >= 1 && size.height() >= 1 && size.width() <= kMaxDimension
        && size.height() <= kMaxDimension;
}
} // namespace

QString MediaProgram::cacheKey() const
{
    QStringList parts{QString::number(int(kind)), programId, revision, playbackUri,
                      QStringLiteral("%1x%2").arg(frameSize.width()).arg(frameSize.height()),
                      source.uri, source.revision};
    for (const QVector<MediaClipSpan> *list : {&video, &audio}) {
        for (const MediaClipSpan &span : *list)
            parts << span.clipId << span.uri << span.revision;
    }
    return QString::fromLatin1(QCryptographicHash::hash(parts.join(QLatin1Char('|')).toUtf8(),
                                                        QCryptographicHash::Sha1).toHex());
}

std::optional<qint64> MediaProgram::frameStartTicks(qint64 frame) const
{
    if (frame < 0 || frame > frameCount) return std::nullopt;
    if (kind == Kind::Source) {
        if (!sourceInfo) return std::nullopt;
        return frame == frameCount ? sourceInfo->durationTicks(timebase)
                                   : sourceInfo->frameStartTicks(frame, timebase);
    }
    if (kind != Kind::Timeline) return std::nullopt;
    const auto offset = TimelineTime::rescale(frame, frameRate, timebase, TimeMap::Ceil);
    return offset ? TimelineTime::add(originTicks, *offset) : std::nullopt;
}

std::optional<qint64> MediaProgram::frameStartSample(qint64 frame) const
{
    const TimelineRate samples{sampleRate, 1};
    if (frame < 0 || frame > frameCount) return std::nullopt;
    if (kind == Kind::Source) {
        if (!sourceInfo) return std::nullopt;
        return frame == frameCount ? sourceInfo->durationTicks(samples)
                                   : sourceInfo->frameStartTicks(frame, samples);
    }
    const auto ticks = frameStartTicks(frame);
    return ticks ? TimelineTime::rescale(*ticks, timebase, samples, TimeMap::Ceil) : std::nullopt;
}

std::optional<qint64> MediaProgram::frameAtSample(qint64 sample) const
{
    if (sample < 0 || isEmpty()) return std::nullopt;
    const TimelineRate samples{sampleRate, 1};
    if (kind == Kind::Source)
        return sourceInfo ? sourceInfo->frameAtTicks(sample, samples) : std::nullopt;
    const auto ticks = TimelineTime::rescale(sample, samples, timebase, TimeMap::Floor);
    const auto relative = ticks ? TimelineTime::subtract(*ticks, originTicks) : std::nullopt;
    if (!relative || *relative < 0) return std::nullopt;
    const auto frame = TimelineTime::rescale(*relative, timebase, frameRate, TimeMap::Floor);
    if (!frame || *frame >= frameCount) return std::nullopt;
    return frame;
}

qint64 MediaProgram::endSample() const
{
    return isEmpty() ? 0 : frameStartSample(frameCount).value_or(0);
}

QVariantMap MediaProgram::describe() const
{
    const QString kindName = kind == Kind::Source ? QStringLiteral("source")
        : kind == Kind::Timeline ? QStringLiteral("timeline") : QStringLiteral("empty");
    return {{QStringLiteral("kind"), kindName}, {QStringLiteral("programId"), programId},
            {QStringLiteral("revision"), revision}, {QStringLiteral("frameCount"), text(frameCount)},
            {QStringLiteral("frameRate"), QStringLiteral("%1/%2").arg(frameRate.numerator).arg(frameRate.denominator)},
            {QStringLiteral("frameRateNumerator"), text(frameRate.numerator)},
            {QStringLiteral("frameRateDenominator"), text(frameRate.denominator)},
            {QStringLiteral("timebaseNumerator"), text(timebase.numerator)},
            {QStringLiteral("timebaseDenominator"), text(timebase.denominator)},
            {QStringLiteral("sampleRate"), sampleRate}, {QStringLiteral("channels"), channels},
            {QStringLiteral("width"), frameSize.width()}, {QStringLiteral("height"), frameSize.height()},
            {QStringLiteral("proxy"), proxy}, {QStringLiteral("playbackUri"), playbackUri},
            {QStringLiteral("unresolved"), unresolved}};
}

std::shared_ptr<MediaProgram> MediaProgram::fromTimeline(const TimelineModel &timeline, const Resolver &resolve,
                                                         const QSize &frameSize, int channels, QString *error)
{
    const auto reject = [error](const QString &message) -> std::shared_ptr<MediaProgram> {
        if (error) *error = message;
        return nullptr;
    };
    if (!validSize(frameSize)) return reject(QStringLiteral("A timeline program needs 1–8192 pixels per side"));
    if (channels < 1 || channels > 8) return reject(QStringLiteral("A timeline program needs 1–8 audio channels"));
    TimeMap *map = timeline.timeMap();
    if (!map) return reject(QStringLiteral("The timeline has no time map"));
    auto program = std::make_shared<MediaProgram>();
    program->kind = Kind::Timeline;
    program->programId = timeline.documentId();
    program->revision = timeline.revisionText();
    program->timebase = timeline.timebase();
    const auto rate = TimelineRate::normalized(map->frameRateNumerator(), map->frameRateDenominator());
    if (!rate || map->sampleRate() < 1) return reject(QStringLiteral("The timeline frame or sample rate is invalid"));
    program->frameRate = *rate;
    program->sampleRate = map->sampleRate();
    program->channels = channels;
    program->frameSize = frameSize;
    const auto originSample = TimelineTime::parseDecimal(map->originSample());
    const auto originTicks = originSample
        ? TimelineTime::rescale(*originSample, TimelineRate{program->sampleRate, 1}, program->timebase,
                                TimeMap::NearestAway)
        : std::nullopt;
    if (!originTicks) return reject(QStringLiteral("The timeline origin is invalid"));
    program->originTicks = *originTicks;
    const QVector<QUuid> order = timeline.trackOrder();
    bool anySolo = false;
    for (const QUuid &id : order) {
        const TimelineTrack *track = timeline.trackRecord(id);
        anySolo = anySolo || (track && track->kind == QLatin1String("audio") && track->solo);
    }
    QSet<QString> unresolved;
    int clipCount = 0;
    for (qsizetype t = order.size() - 1; t >= 0; --t) {
        const TimelineTrack *track = timeline.trackRecord(order.at(t));
        if (!track) continue;
        const bool video = track->kind == QLatin1String("video") && !track->hidden;
        const bool audio = track->kind == QLatin1String("audio") && !track->muted && (!anySolo || track->solo);
        if (!video && !audio) continue;
        for (const TimelineModel::IndexEntry &entry : timeline.trackIndex(track->id)) {
            const TimelineClip *clip = timeline.clipRecord(entry.id);
            if (!clip || clip->resourceId.isEmpty()) continue;
            if (!clip->kind.isEmpty() && clip->kind != track->kind) continue;
            if (++clipCount > kMaxClips) return reject(QStringLiteral("The timeline exceeds 100000 media clips"));
            MediaClipSpan span;
            span.clipId = clip->id.toString(QUuid::WithoutBraces);
            span.resourceId = clip->resourceId;
            if (resolve) {
                const auto resolved = resolve(clip->resourceId);
                span.uri = resolved.first;
                span.revision = resolved.second;
            }
            if (span.uri.isEmpty()) unresolved.insert(clip->resourceId);
            span.start = clip->start;
            span.duration = clip->duration;
            span.sourceOffset = clip->sourceOffset;
            (video ? program->video : program->audio).append(span);
        }
    }
    program->unresolved = unresolved.values();
    program->unresolved.sort();
    const auto relative = TimelineTime::subtract(timeline.contentEnd(), program->originTicks);
    program->frameCount = relative && *relative > 0
        ? TimelineTime::rescale(*relative, program->timebase, program->frameRate, TimeMap::Ceil).value_or(0)
        : 0;
    return program;
}

std::shared_ptr<MediaProgram> MediaProgram::forSource(const MediaClipSpan &original, const QString &playbackUri,
                                                      bool proxy, MediaSourceInfoPtr info, int sampleRate,
                                                      int channels)
{
    auto program = std::make_shared<MediaProgram>();
    program->source = original;
    program->playbackUri = playbackUri.isEmpty() ? original.uri : playbackUri;
    program->proxy = proxy;
    program->sampleRate = std::clamp(sampleRate, 8000, 192000);
    program->channels = std::clamp(channels, 1, 8);
    program->programId = original.resourceId;
    program->revision = original.revision;
    if (!info || !info->hasVideo()) return program; // Kind::Empty until an indexed video source exists
    program->kind = Kind::Source;
    program->sourceInfo = info;
    program->frameRate = info->nominalRate;
    program->frameSize = QSize(info->width, info->height);
    program->frameCount = info->frameCount();
    return program;
}

QJsonObject MediaProgram::toJson() const
{
    QJsonArray videoSpans, audioSpans;
    for (const MediaClipSpan &span : video) videoSpans.append(spanJson(span));
    for (const MediaClipSpan &span : audio) audioSpans.append(spanJson(span));
    return {{QStringLiteral("schema"), kSchema},
            {QStringLiteral("kind"), kind == Kind::Timeline ? QStringLiteral("timeline") : QStringLiteral("unsupported")},
            {QStringLiteral("programId"), programId}, {QStringLiteral("revision"), revision},
            {QStringLiteral("timebase"), rateJson(timebase)}, {QStringLiteral("frameRate"), rateJson(frameRate)},
            {QStringLiteral("sampleRate"), sampleRate}, {QStringLiteral("channels"), channels},
            {QStringLiteral("originTicks"), text(originTicks)}, {QStringLiteral("frameCount"), text(frameCount)},
            {QStringLiteral("width"), frameSize.width()}, {QStringLiteral("height"), frameSize.height()},
            {QStringLiteral("video"), videoSpans}, {QStringLiteral("audio"), audioSpans}};
}

std::shared_ptr<MediaProgram> MediaProgram::fromJson(const QJsonObject &json, QString *error)
{
    const auto reject = [error](const QString &message) -> std::shared_ptr<MediaProgram> {
        if (error) *error = message;
        return nullptr;
    };
    if (json.value(QStringLiteral("schema")).toString() != kSchema
        || json.value(QStringLiteral("kind")).toString() != QLatin1String("timeline"))
        return reject(QStringLiteral("Expected a %1 timeline program").arg(kSchema));
    auto program = std::make_shared<MediaProgram>();
    program->kind = Kind::Timeline;
    program->programId = json.value(QStringLiteral("programId")).toString();
    program->revision = json.value(QStringLiteral("revision")).toString();
    const auto timebase = rateFrom(json.value(QStringLiteral("timebase")));
    const auto frameRate = rateFrom(json.value(QStringLiteral("frameRate")));
    const auto origin = decimal(json.value(QStringLiteral("originTicks")));
    const auto frames = decimal(json.value(QStringLiteral("frameCount")));
    program->sampleRate = json.value(QStringLiteral("sampleRate")).toInt();
    program->channels = json.value(QStringLiteral("channels")).toInt();
    program->frameSize = QSize(json.value(QStringLiteral("width")).toInt(),
                               json.value(QStringLiteral("height")).toInt());
    if (!timebase || !frameRate || !origin || !frames || *frames < 0 || !validSize(program->frameSize)
        || program->sampleRate < 8000 || program->sampleRate > 192000 || program->channels < 1
        || program->channels > 8)
        return reject(QStringLiteral("The program's rates, size, channels or frame count are invalid"));
    program->timebase = *timebase;
    program->frameRate = *frameRate;
    program->originTicks = *origin;
    program->frameCount = *frames;
    const QJsonArray videoSpans = json.value(QStringLiteral("video")).toArray();
    const QJsonArray audioSpans = json.value(QStringLiteral("audio")).toArray();
    if (videoSpans.size() + audioSpans.size() > kMaxClips)
        return reject(QStringLiteral("The program exceeds 100000 media clips"));
    for (const auto &[array, target] : {std::pair{&videoSpans, &program->video},
                                        std::pair{&audioSpans, &program->audio}}) {
        for (const QJsonValue &value : *array) {
            const auto span = spanFrom(value);
            if (!span) return reject(QStringLiteral("A program clip has invalid exact ticks"));
            target->append(*span);
        }
    }
    return program;
}

} // namespace QindaTK
