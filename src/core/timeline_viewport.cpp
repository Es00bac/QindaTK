// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_viewport.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr double kMaxTickMagnitude = 9.0e18;

QList<QByteArray> clipRoles()
{
    // Role names avoid Item properties (x, width, layer, index...) so a
    // delegate can declare each one as a required property.
    return {"key", "clipId", "trackId", "trackIndex", "clipX", "laneY", "clipWidth", "laneHeight",
            "label", "clipColor", "kind", "resourceId", "locked", "startTicks", "durationTicks",
            "endTicks", "sourceOffsetTicks", "sourceDurationTicks", "visibleSourceStartTicks",
            "visibleSourceEndTicks", "clippedStart", "clippedEnd", "preview", "ghost", "invalid",
            "summary", "summaryCount", "stackOrder", "anchorMode"};
}

QList<QByteArray> trackRoles()
{
    return {"key", "trackId", "trackIndex", "orderIndex", "trackY", "trackHeight", "name", "kind",
            "muted", "solo", "armed", "locked", "hidden", "routing", "trackColor", "clipCount",
            "preview"};
}

QList<QByteArray> markerRoles()
{
    return {"key", "markerId", "markerX", "markerWidth", "positionTicks", "durationTicks",
            "endTicks", "label", "markerColor", "kind", "locked", "anchorMode", "preview", "ghost",
            "clippedStart", "clippedEnd"};
}

std::optional<qint64> roundTicks(double value, TimeMap::Rounding rounding)
{
    if (!std::isfinite(value) || std::fabs(value) >= kMaxTickMagnitude) return std::nullopt;
    switch (rounding) {
    case TimeMap::TowardZero: return qint64(std::trunc(value));
    case TimeMap::Floor: return qint64(std::floor(value));
    case TimeMap::Ceil: return qint64(std::ceil(value));
    case TimeMap::NearestAway: return qint64(std::llround(value));
    }
    return std::nullopt;
}
} // namespace

TimelineViewport::TimelineViewport(QObject *parent)
    : QObject(parent),
      m_clips(new TimelineVisibleList(clipRoles(), this)),
      m_tracks(new TimelineVisibleList(trackRoles(), this)),
      m_markers(new TimelineVisibleList(markerRoles(), this))
{
}

void TimelineViewport::setModel(TimelineModel *model)
{
    if (m_model == model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (model) {
        connect(model, &TimelineModel::revisionChanged, this, &TimelineViewport::refresh);
        connect(model, &TimelineModel::previewChanged, this, &TimelineViewport::refresh);
        connect(model, &QObject::destroyed, this, &TimelineViewport::refresh);
    }
    emit modelChanged();
    refresh();
}

void TimelineViewport::changed()
{
    emit viewChanged();
    refresh();
}

void TimelineViewport::setOrigin(qint64 ticks)
{
    if (m_origin == ticks) return;
    m_origin = ticks;
    changed();
}

void TimelineViewport::setOriginTicksText(const QString &ticks)
{
    if (const auto value = TimelineTime::parseDecimal(ticks)) setOrigin(*value);
}

void TimelineViewport::setPixelsPerSecond(double value)
{
    if (!std::isfinite(value) || value <= 0) return;
    value = std::clamp(value, m_minimumZoom, m_maximumZoom);
    if (qFuzzyCompare(m_pixelsPerSecond, value)) return;
    m_pixelsPerSecond = value;
    changed();
}

void TimelineViewport::setMinimumPixelsPerSecond(double value)
{
    if (!std::isfinite(value) || value <= 0 || value > m_maximumZoom || value == m_minimumZoom) return;
    m_minimumZoom = value;
    if (m_pixelsPerSecond < value) m_pixelsPerSecond = value;
    changed();
}

void TimelineViewport::setMaximumPixelsPerSecond(double value)
{
    if (!std::isfinite(value) || value < m_minimumZoom || value == m_maximumZoom) return;
    m_maximumZoom = value;
    if (m_pixelsPerSecond > value) m_pixelsPerSecond = value;
    changed();
}

void TimelineViewport::setViewportWidth(double value)
{
    value = std::isfinite(value) ? std::max(0.0, value) : 0.0;
    if (m_width == value) return;
    m_width = value;
    changed();
}

void TimelineViewport::setViewportHeight(double value)
{
    value = std::isfinite(value) ? std::max(0.0, value) : 0.0;
    if (m_height == value) return;
    m_height = value;
    changed();
}

void TimelineViewport::setScrollY(double value)
{
    value = std::isfinite(value) ? std::clamp(value, 0.0, maximumScrollY()) : 0.0;
    if (m_scrollY == value) return;
    m_scrollY = value;
    changed();
}

void TimelineViewport::setDefaultTrackHeight(double value)
{
    if (!std::isfinite(value) || value <= 0 || m_defaultTrackHeight == value) return;
    m_defaultTrackHeight = value;
    changed();
}

void TimelineViewport::setMinimumTrackHeight(double value)
{
    if (!std::isfinite(value) || value <= 0 || m_minimumTrackHeight == value) return;
    m_minimumTrackHeight = value;
    changed();
}

void TimelineViewport::setOverscanPixels(double value)
{
    value = std::isfinite(value) ? std::max(0.0, value) : 0.0;
    if (m_overscan == value) return;
    m_overscan = value;
    changed();
}

void TimelineViewport::setSummaryPixels(double value)
{
    value = std::isfinite(value) ? std::max(0.0, value) : 0.0;
    if (m_summaryPixels == value) return;
    m_summaryPixels = value;
    changed();
}

void TimelineViewport::setHandlePixels(double value)
{
    value = std::isfinite(value) ? std::max(0.0, value) : 0.0;
    if (m_handlePixels == value) return;
    m_handlePixels = value;
    emit viewChanged();
}

void TimelineViewport::setMaximumClipRows(int value)
{
    value = std::clamp(value, 16, 8192);
    if (m_maximumClipRows == value) return;
    m_maximumClipRows = value;
    changed();
}

void TimelineViewport::setShowGhosts(bool show)
{
    if (m_showGhosts == show) return;
    m_showGhosts = show;
    changed();
}

double TimelineViewport::pixelsPerTick() const
{
    const TimelineRate rate = m_model ? m_model->timebase()
                                      : TimelineRate{TimelineTime::kDefaultTicksPerSecond, 1};
    return m_pixelsPerSecond * double(rate.denominator) / double(rate.numerator);
}

double TimelineViewport::xFor(qint64 ticks) const
{
    const auto delta = TimelineTime::subtract(ticks, m_origin);
    if (!delta) return ticks > m_origin ? 1e300 : -1e300;
    return double(*delta) * pixelsPerTick();
}

std::optional<qint64> TimelineViewport::ticksAt(double x, TimeMap::Rounding rounding) const
{
    const auto delta = roundTicks(x / pixelsPerTick(), rounding);
    return delta ? TimelineTime::add(m_origin, *delta) : std::nullopt;
}

std::optional<qint64> TimelineViewport::ticksForLength(double pixels) const
{
    return roundTicks(std::fabs(pixels) / pixelsPerTick(), TimeMap::NearestAway);
}

double TimelineViewport::xForTicks(const QString &ticks) const
{
    const auto value = TimelineTime::parseDecimal(ticks);
    return value ? xFor(*value) : 0.0;
}

QString TimelineViewport::ticksForX(double x, int rounding) const
{
    const auto mode = rounding >= TimeMap::TowardZero && rounding <= TimeMap::NearestAway
                          ? TimeMap::Rounding(rounding) : TimeMap::NearestAway;
    const auto value = ticksAt(x, mode);
    return value ? QString::number(*value) : QString();
}

QString TimelineViewport::ticksForPixels(double pixels) const
{
    const auto value = ticksForLength(pixels);
    return value ? QString::number(*value) : QString();
}

double TimelineViewport::pixelsForTicks(const QString &ticks) const
{
    const auto value = TimelineTime::parseDecimal(ticks);
    return value ? double(*value) * pixelsPerTick() : 0.0;
}

int TimelineViewport::trackIndexAtY(double y) const
{
    const double content = y + m_scrollY;
    if (!std::isfinite(content) || content < 0 || m_order.isEmpty()) return -1;
    auto it = std::upper_bound(m_order.cbegin(), m_order.cend(), content,
                               [](double value, const TrackSlot &slot) { return value < slot.top; });
    if (it == m_order.cbegin()) return -1;
    --it;
    return content < it->top + it->height ? int(it - m_order.cbegin()) : -1;
}

QString TimelineViewport::trackIdAtY(double y) const
{
    return trackIdAt(trackIndexAtY(y));
}

QString TimelineViewport::trackIdAt(int visibleIndex) const
{
    if (visibleIndex < 0 || visibleIndex >= m_order.size()) return {};
    return m_order.at(visibleIndex).id.toString(QUuid::WithoutBraces);
}

int TimelineViewport::trackIndexOf(const QString &trackId) const
{
    return m_slotIndex.value(QUuid(trackId), -1);
}

double TimelineViewport::trackY(const QString &trackId) const
{
    const int index = trackIndexOf(trackId);
    return index < 0 ? -1.0 : m_order.at(index).top - m_scrollY;
}

double TimelineViewport::trackHeight(const QString &trackId) const
{
    const int index = trackIndexOf(trackId);
    return index < 0 ? 0.0 : m_order.at(index).height;
}

QVariantMap TimelineViewport::hitTest(double x, double y) const
{
    QVariantMap result{{QStringLiteral("kind"), QStringLiteral("none")}};
    const int index = trackIndexAtY(y);
    const auto at = ticksAt(x, TimeMap::Floor);
    if (index < 0 || !at || !m_model) return result;
    const TrackSlot &slot = m_order.at(index);
    result.insert(QStringLiteral("kind"), QStringLiteral("track"));
    result.insert(QStringLiteral("trackId"), slot.id.toString(QUuid::WithoutBraces));
    result.insert(QStringLiteral("trackIndex"), index);
    result.insert(QStringLiteral("ticks"), QString::number(*at));
    const qint64 reach = ticksForLength(m_handlePixels).value_or(0) + 1;
    const auto low = TimelineTime::subtract(*at, reach), high = TimelineTime::add(*at, reach);
    if (!low || !high) return result;
    const auto candidates = clipsOnTrack(slot.id, *low, *high);
    const TimelineClip *inside = nullptr;
    for (const auto &clip : candidates)
        if (clip.start <= *at && *at < clip.end()) inside = &clip; // topmost: last in order
    const TimelineClip *target = inside;
    QString part = QStringLiteral("clip");
    const auto partFor = [&](const TimelineClip &clip) {
        const double left = xFor(clip.start), right = xFor(clip.end());
        const double handle = std::min(m_handlePixels, (right - left) / 3.0);
        if (x - left <= handle && x - left >= -m_handlePixels) return QStringLiteral("trimStart");
        if (right - x <= handle && right - x >= -m_handlePixels) return QStringLiteral("trimEnd");
        return QStringLiteral("clip");
    };
    if (inside) {
        part = partFor(*inside);
    } else {
        double best = m_handlePixels + 1;
        for (const auto &clip : candidates) {
            const double left = std::fabs(xFor(clip.start) - x), right = std::fabs(xFor(clip.end()) - x);
            if (left <= m_handlePixels && left < best) { best = left; target = &clip; part = QStringLiteral("trimStart"); }
            if (right <= m_handlePixels && right < best) { best = right; target = &clip; part = QStringLiteral("trimEnd"); }
        }
    }
    if (!target) return result;
    const auto *track = m_model->trackRecord(target->trackId);
    result.insert(QStringLiteral("kind"), part);
    result.insert(QStringLiteral("clipId"), target->id.toString(QUuid::WithoutBraces));
    result.insert(QStringLiteral("locked"), target->locked || (track && track->locked));
    return result;
}

QVariantMap TimelineViewport::clipGeometry(const QString &clipId) const
{
    if (!m_model) return {};
    const auto clip = m_model->effectiveClip(QUuid(clipId));
    if (!clip) return {};
    const int index = m_slotIndex.value(clip->trackId, -1);
    const double x = xFor(clip->start), right = xFor(clip->end());
    const double y = index < 0 ? -1 : m_order.at(index).top - m_scrollY;
    const double height = index < 0 ? 0 : m_order.at(index).height;
    const bool visible = index >= 0 && right > 0 && x < m_width && y + height > 0 && y < m_height;
    return {{QStringLiteral("x"), x}, {QStringLiteral("y"), y},
            {QStringLiteral("width"), right - x}, {QStringLiteral("height"), height},
            {QStringLiteral("visible"), visible}, {QStringLiteral("trackIndex"), index}};
}

void TimelineViewport::scrollByPixels(double dx, double dy)
{
    m_batch = true;
    if (dx != 0) {
        const auto delta = ticksForLength(dx);
        if (delta) {
            const auto next = dx > 0 ? TimelineTime::add(m_origin, *delta)
                                     : TimelineTime::subtract(m_origin, *delta);
            if (next) m_origin = *next;
        }
    }
    if (dy != 0) m_scrollY = std::clamp(m_scrollY + dy, 0.0, maximumScrollY());
    m_batch = false;
    changed();
}

void TimelineViewport::zoomAround(double x, double factor)
{
    if (!std::isfinite(factor) || factor <= 0) return;
    const auto anchor = ticksAt(x, TimeMap::NearestAway);
    const double zoom = std::clamp(m_pixelsPerSecond * factor, m_minimumZoom, m_maximumZoom);
    if (!anchor || qFuzzyCompare(zoom, m_pixelsPerSecond)) return;
    m_pixelsPerSecond = zoom;
    const auto offset = roundTicks(x / pixelsPerTick(), TimeMap::NearestAway);
    if (offset) {
        if (const auto origin = TimelineTime::subtract(*anchor, *offset)) m_origin = *origin;
    }
    changed();
}

bool TimelineViewport::ensureTicksVisible(const QString &ticks, double margin)
{
    const auto value = TimelineTime::parseDecimal(ticks);
    if (!value || m_width <= 0) return false;
    margin = std::clamp(margin, 0.0, m_width / 2);
    const double x = xFor(*value);
    double target = x;
    if (x < margin) target = margin;
    else if (x > m_width - margin) target = m_width - margin;
    else return false;
    const auto offset = roundTicks(target / pixelsPerTick(), TimeMap::NearestAway);
    const auto origin = offset ? TimelineTime::subtract(*value, *offset) : std::nullopt;
    if (!origin || *origin == m_origin) return false;
    setOrigin(*origin);
    return true;
}

bool TimelineViewport::ensureTrackVisible(const QString &trackId)
{
    const int index = trackIndexOf(trackId);
    if (index < 0) return false;
    const TrackSlot &slot = m_order.at(index);
    double next = m_scrollY;
    if (slot.top < m_scrollY) next = slot.top;
    else if (slot.top + slot.height > m_scrollY + m_height) next = slot.top + slot.height - m_height;
    if (next == m_scrollY) return false;
    setScrollY(next);
    return true;
}

bool TimelineViewport::ensureClipVisible(const QString &clipId, double margin)
{
    if (!m_model || m_width <= 0) return false;
    const auto clip = m_model->effectiveClip(QUuid(clipId));
    if (!clip) return false;
    bool moved = ensureTrackVisible(clip->trackId.toString(QUuid::WithoutBraces));
    margin = std::clamp(margin, 0.0, m_width / 4);
    const double left = xFor(clip->start), right = xFor(clip->end());
    if (left >= margin && right <= m_width - margin) return moved;
    // A clip that fits ends at the right margin when it lies to the right;
    // otherwise its start is placed at the left margin.
    const bool fits = right - left <= m_width - 2 * margin;
    const qint64 anchor = fits && left > margin ? clip->end() : clip->start;
    const double target = fits && left > margin ? m_width - margin : margin;
    const auto offset = roundTicks(target / pixelsPerTick(), TimeMap::NearestAway);
    const auto origin = offset ? TimelineTime::subtract(anchor, *offset) : std::nullopt;
    if (!origin || *origin == m_origin) return moved;
    setOrigin(*origin);
    return true;
}

void TimelineViewport::fitToContent()
{
    if (!m_model || m_width <= 0) return;
    const TimelineRate rate = m_model->timebase();
    const double seconds = double(std::max<qint64>(1, m_model->contentEnd())) * double(rate.denominator)
                         / double(rate.numerator);
    m_batch = true;
    m_origin = 0;
    m_pixelsPerSecond = std::clamp(m_width / seconds, m_minimumZoom, m_maximumZoom);
    m_batch = false;
    changed();
}

} // namespace QindaTK
