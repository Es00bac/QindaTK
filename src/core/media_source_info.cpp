// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_source_info.h"

#include <QFileInfo>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QUrl>
#include <algorithm>
#include <initializer_list>

namespace QindaTK {
namespace {
constexpr TimelineRate kIdentityTicks{TimelineTime::kDefaultTicksPerSecond, 1};

QString rateText(TimelineRate rate)
{
    return QStringLiteral("%1/%2").arg(rate.numerator).arg(rate.denominator);
}

QString decimal(std::optional<qint64> value)
{
    return value ? QString::number(*value) : QString();
}

// Exclusive end of the last frame relative to pts[0], in pts units.
std::optional<qint64> variableEndPts(const MediaSourceInfo &info)
{
    const auto &frames = info.frames;
    if (frames.isEmpty()) return std::nullopt;
    const MediaVideoFrameEntry &last = frames.constLast();
    qint64 duration = last.duration;
    if (duration <= 0 && frames.size() >= 2)
        duration = last.pts - frames.at(frames.size() - 2).pts;
    if (duration <= 0) {
        const auto nominal = TimelineTime::rescale(1, info.nominalRate, info.ptsPerSecond,
                                                   TimeMap::Ceil);
        duration = nominal && *nominal > 0 ? *nominal : 1;
    }
    const auto relative = TimelineTime::subtract(last.pts, frames.constFirst().pts);
    return relative ? TimelineTime::add(*relative, duration) : std::nullopt;
}

QString appendDamage(const QString &existing, const QString &message)
{
    return existing.isEmpty() ? message : existing + QStringLiteral("; ") + message;
}
} // namespace

std::optional<qint64> MediaSourceInfo::frameStartTicks(qint64 index, TimelineRate rate) const
{
    if (!rate.isValid() || index < 0 || index >= frames.size()) return std::nullopt;
    if (constantRate)
        return TimelineTime::rescale(index, nominalRate, rate, TimeMap::Ceil);
    const auto relative = TimelineTime::subtract(frames.at(index).pts, frames.constFirst().pts);
    return relative ? TimelineTime::rescale(*relative, ptsPerSecond, rate, TimeMap::Ceil)
                    : std::nullopt;
}

std::optional<qint64> MediaSourceInfo::durationTicks(TimelineRate rate) const
{
    if (!rate.isValid() || frames.isEmpty()) return std::nullopt;
    if (constantRate)
        return TimelineTime::rescale(frames.size(), nominalRate, rate, TimeMap::Ceil);
    const auto end = variableEndPts(*this);
    return end ? TimelineTime::rescale(*end, ptsPerSecond, rate, TimeMap::Ceil) : std::nullopt;
}

std::optional<qint64> MediaSourceInfo::frameAtTicks(qint64 ticks, TimelineRate rate) const
{
    if (!rate.isValid() || frames.isEmpty() || ticks < 0) return std::nullopt;
    const auto duration = durationTicks(rate);
    if (!duration || ticks >= *duration) return std::nullopt;
    if (constantRate) {
        const auto index = TimelineTime::rescale(ticks, rate, nominalRate, TimeMap::Floor);
        if (!index || *index < 0 || *index >= frames.size()) return std::nullopt;
        return index;
    }
    // Hold: the last frame whose start is at or before `ticks`.
    qint64 low = 0;
    qint64 high = frames.size() - 1;
    while (low < high) {
        const qint64 middle = low + (high - low + 1) / 2;
        const auto start = frameStartTicks(middle, rate);
        if (!start) return std::nullopt;
        if (*start <= ticks) low = middle;
        else high = middle - 1;
    }
    return low;
}

qint64 MediaSourceInfo::indexOfPts(qint64 pts) const
{
    const auto it = std::lower_bound(frames.cbegin(), frames.cend(), pts,
        [](const MediaVideoFrameEntry &entry, qint64 value) { return entry.pts < value; });
    return it != frames.cend() && it->pts == pts ? qint64(it - frames.cbegin()) : -1;
}

qint64 MediaSourceInfo::keyframeAtOrBefore(qint64 index) const
{
    if (frames.isEmpty()) return 0;
    for (qint64 i = std::min<qint64>(index, frames.size() - 1); i >= 0; --i)
        if (frames.at(i).key) return i;
    return 0;
}

qint64 MediaSourceInfo::audioPacketAtOrBefore(qint64 sample) const
{
    const auto it = std::upper_bound(audioPackets.cbegin(), audioPackets.cend(), sample,
        [](qint64 value, const MediaAudioPacketEntry &entry) { return value < entry.firstSample; });
    return it == audioPackets.cbegin() ? -1 : qint64(it - audioPackets.cbegin()) - 1;
}

qint64 MediaSourceInfo::audioFirstSampleOfPts(qint64 pts) const
{
    const auto it = std::lower_bound(audioPackets.cbegin(), audioPackets.cend(), pts,
        [](const MediaAudioPacketEntry &entry, qint64 value) { return entry.pts < value; });
    return it != audioPackets.cend() && it->pts == pts ? it->firstSample : -1;
}

QColorSpace MediaSourceInfo::frameColorSpace(QString *policy) const
{
    const auto named = [policy](const char *name) {
        if (policy) *policy = QString::fromLatin1(name);
    };
    const QColorSpace::Primaries primaries =
        colorPrimaries == QLatin1String("bt2020") ? QColorSpace::Primaries::Bt2020
        : colorPrimaries == QLatin1String("smpte432") ? QColorSpace::Primaries::DciP3D65
        : QColorSpace::Primaries::SRgb;
    if (colorTransfer == QLatin1String("smpte2084")) {
        named("hdr-pq");
        return QColorSpace(primaries, QColorSpace::TransferFunction::St2084);
    }
    if (colorTransfer == QLatin1String("arib-std-b67")) {
        named("hdr-hlg");
        return QColorSpace(primaries, QColorSpace::TransferFunction::Hlg);
    }
    if (colorTransfer == QLatin1String("linear")) {
        named("linear");
        return QColorSpace(primaries, QColorSpace::TransferFunction::Linear);
    }
    if (primaries == QColorSpace::Primaries::Bt2020) {
        named("wide-gamut-bt2020");
        return QColorSpace(primaries, QColorSpace::TransferFunction::SRgb);
    }
    if (primaries == QColorSpace::Primaries::DciP3D65) {
        named("wide-gamut-p3");
        return QColorSpace(primaries, QColorSpace::TransferFunction::SRgb);
    }
    named("srgb-approximation");
    return QColorSpace(QColorSpace::SRgb);
}

QVariantMap MediaSourceInfo::toVariantMap() const
{
    QString policy;
    frameColorSpace(&policy);
    const auto duration = hasVideo() ? durationTicks(kIdentityTicks) : std::nullopt;
    return {{QStringLiteral("path"), path},
        {QStringLiteral("fileSize"), QString::number(fileSize)},
        {QStringLiteral("container"), container},
        {QStringLiteral("backend"), backendVersion},
        {QStringLiteral("hasVideo"), hasVideo()},
        {QStringLiteral("hasAudio"), hasAudio()},
        {QStringLiteral("codec"), videoCodec},
        {QStringLiteral("pixelFormat"), pixelFormat},
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
        {QStringLiteral("resolution"), hasVideo() ? QStringLiteral("%1×%2").arg(width).arg(height)
                                                  : QString()},
        {QStringLiteral("sampleAspect"), QStringLiteral("%1:%2").arg(sampleAspect.numerator)
                                             .arg(sampleAspect.denominator)},
        {QStringLiteral("frameCount"), QString::number(frameCount())},
        {QStringLiteral("frameRate"), rateText(nominalRate)},
        {QStringLiteral("frameRateNumerator"), QString::number(nominalRate.numerator)},
        {QStringLiteral("frameRateDenominator"), QString::number(nominalRate.denominator)},
        {QStringLiteral("constantRate"), constantRate},
        {QStringLiteral("durationTicks"), decimal(duration)},
        {QStringLiteral("ticksPerSecond"), QString::number(kIdentityTicks.numerator)},
        {QStringLiteral("durationSeconds"), duration ? double(*duration) / double(kIdentityTicks.numerator)
                                                     : containerSeconds},
        {QStringLiteral("fieldOrder"), fieldOrder},
        {QStringLiteral("colorPrimaries"), colorPrimaries},
        {QStringLiteral("colorTransfer"), colorTransfer},
        {QStringLiteral("colorMatrix"), colorMatrix},
        {QStringLiteral("colorRange"), colorRange},
        {QStringLiteral("colorPolicy"), policy},
        {QStringLiteral("hasAlpha"), hasAlpha},
        {QStringLiteral("audioCodec"), audioCodec},
        {QStringLiteral("audioSampleRate"), audioSampleRate},
        {QStringLiteral("audioChannels"), audioChannels},
        {QStringLiteral("audioSampleCount"), QString::number(audioSampleCount)},
        {QStringLiteral("audioOffsetSamples"), QString::number(audioOffsetSamples)},
        {QStringLiteral("sourceTimecode"), sourceTimecode},
        {QStringLiteral("truncated"), truncated},
        {QStringLiteral("damaged"), damaged},
        {QStringLiteral("damage"), damage}};
}

bool MediaSourceInfo::isConstantRate(const QVector<MediaVideoFrameEntry> &frames,
                                     TimelineRate ptsPerSecond, TimelineRate candidate)
{
    if (!candidate.isValid() || !ptsPerSecond.isValid() || frames.isEmpty()) return false;
    // Container defaults above 1000 fps are time bases, not frame rates.
    if (candidate.numerator > candidate.denominator * 1000) return false;
    const qint64 origin = frames.constFirst().pts;
    for (qsizetype i = 0; i < frames.size(); ++i) {
        // AGENT-NOTE: one pts unit of slack absorbs container rounding, such
        // as Matroska's millisecond timestamps for 24000/1001 video.
        const auto expected = TimelineTime::rescale(i, candidate, ptsPerSecond, TimeMap::NearestAway);
        const auto actual = TimelineTime::subtract(frames.at(i).pts, origin);
        if (!expected || !actual) return false;
        const auto difference = TimelineTime::subtract(*actual, *expected);
        if (!difference || *difference > 1 || *difference < -1) return false;
    }
    return true;
}

void MediaSourceInfo::finalizeIndex(std::optional<TimelineRate> realRate,
                                    std::optional<TimelineRate> averageRate)
{
    std::stable_sort(frames.begin(), frames.end(),
        [](const MediaVideoFrameEntry &a, const MediaVideoFrameEntry &b) { return a.pts < b.pts; });
    qsizetype write = 0;
    int duplicates = 0;
    for (qsizetype read = 0; read < frames.size(); ++read) {
        if (write > 0 && frames.at(read).pts == frames.at(write - 1).pts) {
            ++duplicates;
            if (frames.at(read).key) frames[write - 1].key = true;
            continue;
        }
        frames[write++] = frames.at(read);
    }
    frames.resize(write);
    if (duplicates > 0) {
        damaged = true;
        damage = appendDamage(damage, QStringLiteral("%1 duplicate video timestamps dropped")
                                          .arg(duplicates));
    }
    std::stable_sort(audioPackets.begin(), audioPackets.end(),
        [](const MediaAudioPacketEntry &a, const MediaAudioPacketEntry &b) { return a.pts < b.pts; });
    bool chosen = false;
    for (const std::optional<TimelineRate> &candidate : {realRate, averageRate}) {
        if (candidate && isConstantRate(frames, ptsPerSecond, *candidate)) {
            nominalRate = *candidate;
            chosen = true;
            break;
        }
    }
    constantRate = chosen || frames.size() <= 1;
    if (!chosen) {
        if (averageRate && averageRate->isValid()) nominalRate = *averageRate;
        else if (realRate && realRate->isValid()) nominalRate = *realRate;
    }
    if (const auto normalized = TimelineRate::normalized(nominalRate.numerator, nominalRate.denominator))
        nominalRate = *normalized;
}

namespace MediaIndexCache {
namespace {
constexpr qsizetype kLimit = 64;
struct Store {
    QMutex mutex;
    QList<MediaSourceInfoPtr> entries;
};
Store &store()
{
    static Store instance;
    return instance;
}
} // namespace

MediaSourceInfoPtr find(const QString &path, qint64 fileSize, qint64 modifiedMs)
{
    Store &cache = store();
    QMutexLocker lock(&cache.mutex);
    for (qsizetype i = 0; i < cache.entries.size(); ++i) {
        const MediaSourceInfoPtr entry = cache.entries.at(i);
        if (entry->path == path && entry->fileSize == fileSize && entry->modifiedMs == modifiedMs) {
            cache.entries.move(i, 0);
            return entry;
        }
    }
    return {};
}

void insert(const MediaSourceInfoPtr &info)
{
    if (!info || info->path.isEmpty()) return;
    Store &cache = store();
    QMutexLocker lock(&cache.mutex);
    cache.entries.removeIf([&info](const MediaSourceInfoPtr &entry) { return entry->path == info->path; });
    cache.entries.prepend(info);
    while (cache.entries.size() > kLimit) cache.entries.removeLast();
}

void clear()
{
    Store &cache = store();
    QMutexLocker lock(&cache.mutex);
    cache.entries.clear();
}
} // namespace MediaIndexCache

QString mediaLocalPath(const QString &uri)
{
    if (uri.trimmed().isEmpty()) return {};
    const QUrl url(uri);
    if (url.isLocalFile()) return QFileInfo(url.toLocalFile()).absoluteFilePath();
    if (url.scheme().isEmpty()) return QFileInfo(uri).absoluteFilePath();
    return {};
}

} // namespace QindaTK
