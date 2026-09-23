// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_codec.h"
#include "timeline_model.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
QString text(const QUuid &id)
{
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

std::optional<qint64> decimal(const QString &value)
{
    return TimelineTime::parseDecimal(value);
}

TimeMap::Rounding rounding(int value)
{
    return value >= TimeMap::TowardZero && value <= TimeMap::NearestAway
               ? TimeMap::Rounding(value) : TimeMap::NearestAway;
}

QString number(const std::optional<qint64> &value)
{
    return value ? QString::number(*value) : QString();
}
} // namespace

QStringList TimelineModel::trackIds() const
{
    QStringList result;
    for (const auto &id : m_trackOrder) result.push_back(text(id));
    return result;
}

QStringList TimelineModel::visibleTrackIds() const
{
    QStringList result;
    for (const auto &id : visibleTrackOrder()) result.push_back(text(id));
    return result;
}

QVariantMap TimelineModel::trackData(const QString &id) const
{
    const auto *track = trackRecord(QUuid(id));
    if (!track) return {};
    QVariantMap map = TimelineCodec::toMap(*track);
    map.insert(QStringLiteral("index"), int(m_trackOrder.indexOf(track->id)));
    map.insert(QStringLiteral("visibleIndex"), int(visibleTrackOrder().indexOf(track->id)));
    map.insert(QStringLiteral("clipCount"), int(trackIndex(track->id).size()));
    return map;
}

QVariantMap TimelineModel::clipData(const QString &id) const
{
    const auto *clip = clipRecord(QUuid(id));
    return clip ? TimelineCodec::toMap(*clip) : QVariantMap();
}

QVariantMap TimelineModel::effectiveClipData(const QString &id) const
{
    const auto clip = effectiveClip(QUuid(id));
    return clip ? TimelineCodec::toMap(*clip) : QVariantMap();
}

QVariantMap TimelineModel::markerData(const QString &id) const
{
    const auto *marker = markerRecord(QUuid(id));
    return marker ? TimelineCodec::toMap(*marker) : QVariantMap();
}

QStringList TimelineModel::markerIds() const
{
    QVector<const TimelineMarker *> sorted;
    for (auto it = m_markers.cbegin(); it != m_markers.cend(); ++it) sorted.push_back(&it.value());
    std::sort(sorted.begin(), sorted.end(), [](const auto *a, const auto *b) {
        return a->position != b->position ? a->position < b->position : a->id < b->id;
    });
    QStringList result;
    for (const auto *marker : sorted) result.push_back(text(marker->id));
    return result;
}

QStringList TimelineModel::clipIdsInRange(const QString &trackId, const QString &start,
                                          const QString &end, int limit) const
{
    const auto from = decimal(start), to = decimal(end);
    QStringList result;
    if (!from || !to) return result;
    for (const auto &id : clipsInRange(QUuid(trackId), *from, *to, std::clamp(limit, 0, kMaxQueryIds)))
        result.push_back(text(id));
    return result;
}

QStringList TimelineModel::clipIdsInRect(const QString &fromTrackId, const QString &toTrackId,
                                         const QString &start, const QString &end, int limit) const
{
    QStringList result;
    const auto from = decimal(start), to = decimal(end);
    const QVector<QUuid> order = visibleTrackOrder();
    qsizetype a = order.indexOf(QUuid(fromTrackId)), b = order.indexOf(QUuid(toTrackId));
    if (!from || !to || a < 0 || b < 0) return result;
    if (a > b) std::swap(a, b);
    limit = std::clamp(limit, 0, kMaxQueryIds);
    for (qsizetype i = a; i <= b && result.size() < limit; ++i) {
        for (const auto &id : clipsInRange(order.at(i), std::min(*from, *to), std::max(*from, *to),
                                           limit - int(result.size())))
            result.push_back(text(id));
    }
    return result;
}

QString TimelineModel::clipAt(const QString &trackId, const QString &ticks) const
{
    const auto at = decimal(ticks);
    if (!at || *at == std::numeric_limits<qint64>::max()) return {};
    const auto hits = clipsInRange(QUuid(trackId), *at, *at + 1);
    return hits.isEmpty() ? QString() : text(hits.last());
}

QString TimelineModel::nearestClip(const QString &trackId, const QString &ticks) const
{
    const QString hit = clipAt(trackId, ticks);
    if (!hit.isEmpty()) return hit;
    const auto at = decimal(ticks);
    const auto &entries = trackIndex(QUuid(trackId));
    if (!at || entries.empty()) return {};
    const auto next = std::lower_bound(entries.cbegin(), entries.cend(), *at,
                                       [](const IndexEntry &entry, qint64 value) {
                                           return entry.start < value;
                                       });
    if (next == entries.cbegin()) return text(next->id);
    const auto previous = std::prev(next);
    if (next == entries.cend()) return text(previous->id);
    const quint64 before = quint64(*at) - quint64(previous->end);
    const quint64 after = quint64(next->start) - quint64(*at);
    return text(before <= after ? previous->id : next->id);
}

QString TimelineModel::adjacentClip(const QString &clipId, int direction) const
{
    const auto *clip = clipRecord(QUuid(clipId));
    if (!clip || direction == 0) return {};
    const auto &entries = trackIndex(clip->trackId);
    const auto it = std::lower_bound(entries.cbegin(), entries.cend(), *clip,
                                     [](const IndexEntry &entry, const TimelineClip &value) {
                                         return entry.start != value.start ? entry.start < value.start
                                                                           : entry.id < value.id;
                                     });
    if (it == entries.cend() || it->id != clip->id) return {};
    if (direction < 0) return it == entries.cbegin() ? QString() : text(std::prev(it)->id);
    const auto next = std::next(it);
    return next == entries.cend() ? QString() : text(next->id);
}

QString TimelineModel::edgeClip(const QString &trackId, bool last) const
{
    const auto &entries = trackIndex(QUuid(trackId));
    if (entries.empty()) return {};
    return text(last ? entries.back().id : entries.front().id);
}

QString TimelineModel::adjacentMarker(const QString &ticks, int direction) const
{
    const auto at = decimal(ticks);
    if (!at || direction == 0) return {};
    const auto &edges = markerEdges();
    const TimelineMarker *best = nullptr;
    for (const auto &edge : edges) {
        if (edge.end) continue;
        const auto *marker = markerRecord(edge.id);
        if (direction > 0 && marker->position > *at) {
            if (!best || marker->position < best->position) best = marker;
        } else if (direction < 0 && marker->position < *at) {
            if (!best || marker->position >= best->position) best = marker;
        }
    }
    return best ? text(best->id) : QString();
}

bool TimelineModel::hasClip(const QString &id) const
{
    return m_clips.contains(QUuid(id));
}

QStringList TimelineModel::allClipIds() const
{
    QStringList result;
    result.reserve(m_clips.size());
    for (auto it = m_clips.cbegin(); it != m_clips.cend(); ++it) result.push_back(text(it.key()));
    return result;
}

QString TimelineModel::addTicks(const QString &left, const QString &right) const
{
    const auto a = decimal(left), b = decimal(right);
    return a && b ? number(TimelineTime::add(*a, *b)) : QString();
}

QString TimelineModel::subtractTicks(const QString &left, const QString &right) const
{
    const auto a = decimal(left), b = decimal(right);
    return a && b ? number(TimelineTime::subtract(*a, *b)) : QString();
}

int TimelineModel::compareTicks(const QString &left, const QString &right) const
{
    const auto a = decimal(left), b = decimal(right);
    if (!a || !b) return 2;
    return (*a > *b) - (*a < *b);
}

QString TimelineModel::ticksToSamplesText(const QString &ticks, int mode) const
{
    const auto value = decimal(ticks);
    return value ? number(ticksToSamples(*value, rounding(mode))) : QString();
}

QString TimelineModel::samplesToTicksText(const QString &samples, int mode) const
{
    const auto value = decimal(samples);
    return value ? number(samplesToTicks(*value, rounding(mode))) : QString();
}

QString TimelineModel::ticksToFramesText(const QString &ticks, int mode) const
{
    const auto value = decimal(ticks);
    return value ? number(ticksToFrames(*value, rounding(mode))) : QString();
}

QString TimelineModel::frameStartTicksText(const QString &frame) const
{
    const auto value = decimal(frame);
    return value ? number(frameStartTicks(*value)) : QString();
}

double TimelineModel::ticksToSeconds(const QString &ticks) const
{
    const auto value = decimal(ticks);
    if (!value) return 0.0;
    return double(*value) * double(m_timebase.denominator) / double(m_timebase.numerator);
}

QString TimelineModel::secondsToTicks(double seconds) const
{
    if (!std::isfinite(seconds)) return {};
    const double ticks = seconds * double(m_timebase.numerator) / double(m_timebase.denominator);
    if (std::fabs(ticks) >= 9.2e18) return {};
    return QString::number(std::llround(ticks));
}

QString TimelineModel::formatTicks(const QString &ticks, const QString &mode, bool dropFrame) const
{
    const auto value = decimal(ticks);
    if (!value) return {};
    if (mode == QLatin1String("seconds"))
        return TimelineTime::formatSeconds(*value, m_timebase, 3) + QStringLiteral("s");
    if (mode == QLatin1String("samples")) return number(ticksToSamples(*value, TimeMap::Floor));
    if (mode == QLatin1String("frames")) return number(ticksToFrames(*value, TimeMap::Floor));
    if (mode == QLatin1String("ticks")) return ticks;
    const auto frame = ticksToFrames(*value, TimeMap::Floor);
    if (!frame) return {};
    const QVariantMap label = timeMap()->formatTimecode(QString::number(*frame), dropFrame);
    return label.value(QStringLiteral("ok")).toBool() ? label.value(QStringLiteral("value")).toString()
                                                       : QString();
}

QVariantMap TimelineModel::parseTicks(const QString &input, const QString &mode, bool dropFrame) const
{
    const QString trimmed = input.trimmed();
    std::optional<qint64> result;
    if (mode == QLatin1String("seconds")) {
        QString value = trimmed;
        if (value.endsWith(QLatin1Char('s'))) value.chop(1);
        result = TimelineTime::parseSeconds(value.trimmed(), m_timebase, TimeMap::NearestAway);
    } else if (mode == QLatin1String("samples")) {
        const auto samples = decimal(trimmed);
        if (samples) result = samplesToTicks(*samples, TimeMap::NearestAway);
    } else if (mode == QLatin1String("frames")) {
        const auto frame = decimal(trimmed);
        if (frame) result = frameStartTicks(*frame);
    } else if (mode == QLatin1String("ticks")) {
        result = decimal(trimmed);
    } else {
        const QVariantMap parsed = timeMap()->parseTimecode(trimmed, dropFrame);
        if (parsed.value(QStringLiteral("ok")).toBool()) {
            const auto frame = decimal(parsed.value(QStringLiteral("value")).toString());
            if (frame) result = frameStartTicks(*frame);
        }
    }
    if (!result)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), tr("“%1” is not a valid %2 value").arg(input, mode)}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("value"), QString::number(*result)}};
}

QString TimelineModel::stepTicks(const QString &fromTicks, const QString &grid, int steps) const
{
    const auto from = decimal(fromTicks);
    if (!from) return {};
    TimelineSnapGrid kind = TimelineSnapGrid::None;
    if (grid == QLatin1String("frames")) kind = TimelineSnapGrid::Frames;
    else if (grid == QLatin1String("seconds")) kind = TimelineSnapGrid::Seconds;
    else if (grid == QLatin1String("samples")) kind = TimelineSnapGrid::Samples;
    quint64 numerator = 0, denominator = 0;
    qint64 origin = 0;
    if (!gridStep(kind, numerator, denominator, origin))
        return number(TimelineTime::add(*from, steps));
    const auto relative = TimelineTime::subtract(*from, origin);
    const auto index = relative ? TimelineTime::mulDiv(*relative, denominator, numerator, TimeMap::Floor)
                                : std::nullopt;
    if (!index) return {};
    const auto line = TimelineTime::mulDiv(*index, numerator, denominator, TimeMap::Ceil);
    const bool onLine = line && TimelineTime::add(origin, *line) == from;
    const auto target = TimelineTime::add(*index, qint64(steps) + ((steps < 0 && !onLine) ? 1 : 0));
    const auto offset = target ? TimelineTime::mulDiv(*target, numerator, denominator, TimeMap::Ceil)
                               : std::nullopt;
    return offset ? number(TimelineTime::add(origin, *offset)) : QString();
}

bool TimelineModel::setEditRangeTicks(qint64 start, qint64 end)
{
    if (start > end) std::swap(start, end);
    if (m_hasRange && m_rangeStart == start && m_rangeEnd == end) return true;
    m_hasRange = true;
    m_rangeStart = start;
    m_rangeEnd = end;
    emit editRangeChanged();
    return true;
}

bool TimelineModel::setEditRange(const QString &start, const QString &end)
{
    const auto a = decimal(start), b = decimal(end);
    if (!a || !b) return fail(tr("The edit range needs exact decimal ticks"));
    if (!policy()->isAvailable(QStringLiteral("range")))
        return fail(policy()->unavailableReason(QStringLiteral("range")));
    return setEditRangeTicks(*a, *b);
}

void TimelineModel::clearEditRange()
{
    if (!m_hasRange) return;
    m_hasRange = false;
    m_rangeStart = m_rangeEnd = 0;
    emit editRangeChanged();
}

} // namespace QindaTK
