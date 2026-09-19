// SPDX-License-Identifier: LGPL-3.0-or-later
#include "meter.h"

#include <QHoverEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace QindaTK {

Meter::Meter(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_color(0x22, 0xd3, 0xee)
    , m_trackColor(0xff, 0xff, 0xff, 0x14)
{
    // The row height and a readable bar length at compact density; callers
    // size them from Theme.size in practice.
    setImplicitSize(80, 8);
    // Hover is accepted so a meter can host a tooltip; it changes
    // nothing about how the bar draws.
    setAcceptHoverEvents(true);
}

void Meter::setValue(qreal value)
{
    if (!qFuzzyCompare(m_value, value)) {
        m_value = value;
        emit valueChanged();
        update();
    }
}

void Meter::setFrom(qreal from)
{
    if (!qFuzzyCompare(m_from, from)) {
        m_from = from;
        emit rangeChanged();
        emit valueChanged();
        update();
    }
}

void Meter::setTo(qreal to)
{
    if (!qFuzzyCompare(m_to, to)) {
        m_to = to;
        emit rangeChanged();
        emit valueChanged();
        update();
    }
}

qreal Meter::position() const
{
    const qreal span = m_to - m_from;
    if (!(span > 0) || !std::isfinite(m_value)) {
        return 0;
    }
    return std::clamp((m_value - m_from) / span, qreal(0), qreal(1));
}

void Meter::setColor(const QColor &color)
{
    if (m_color != color) {
        m_color = color;
        restyle();
    }
}

void Meter::setTrackColor(const QColor &color)
{
    if (m_trackColor != color) {
        m_trackColor = color;
        restyle();
    }
}

void Meter::setRamp(ThemeRamp *ramp)
{
    if (m_ramp == ramp) {
        return;
    }
    if (m_ramp != nullptr) {
        disconnect(m_ramp, &ThemeRamp::changed, this, &Meter::restyle);
    }
    m_ramp = ramp;
    if (m_ramp != nullptr) {
        connect(m_ramp, &ThemeRamp::changed, this, &Meter::restyle);
    }
    restyle();
}

void Meter::setRampMode(RampMode mode)
{
    if (m_rampMode != mode) {
        m_rampMode = mode;
        restyle();
    }
}

void Meter::setSegments(int segments)
{
    const int wanted = std::max(0, segments);
    if (m_segments != wanted) {
        m_segments = wanted;
        restyle();
    }
}

void Meter::setSegmentGap(qreal gap)
{
    const qreal wanted = std::max(qreal(0), gap);
    if (!qFuzzyCompare(m_segmentGap, wanted)) {
        m_segmentGap = wanted;
        restyle();
    }
}

void Meter::setRadius(qreal radius)
{
    const qreal wanted = std::max(qreal(0), radius);
    if (!qFuzzyCompare(m_radius, wanted)) {
        m_radius = wanted;
        restyle();
    }
}

void Meter::setVertical(bool vertical)
{
    if (m_vertical != vertical) {
        m_vertical = vertical;
        restyle();
    }
}

void Meter::restyle()
{
    emit styleChanged();
    emit valueChanged();
    update();
}

void Meter::setTooltip(const QString &tooltip)
{
    if (m_tooltip != tooltip) {
        m_tooltip = tooltip;
        emit tooltipChanged();
    }
}

void Meter::hoverEnterEvent(QHoverEvent *event)
{
    QQuickPaintedItem::hoverEnterEvent(event);
    if (!m_hovered) {
        m_hovered = true;
        emit hoveredChanged();
    }
}

void Meter::hoverLeaveEvent(QHoverEvent *event)
{
    QQuickPaintedItem::hoverLeaveEvent(event);
    if (m_hovered) {
        m_hovered = false;
        emit hoveredChanged();
    }
}

QColor Meter::valueColor() const
{
    if (m_ramp != nullptr) {
        const QColor ramped = m_ramp->at(position());
        if (ramped.isValid()) {
            return ramped;
        }
    }
    return m_color;
}

namespace {

// The colour of the fill at `t` along its own length.
QColor colourAt(const ThemeRamp *ramp, Meter::RampMode mode, qreal t, qreal reading,
                const QColor &flat)
{
    if (ramp == nullptr) {
        return flat;
    }
    const QColor ramped = ramp->at(mode == Meter::ByPosition ? t : reading);
    return ramped.isValid() ? ramped : flat;
}

} // namespace

void Meter::paint(QPainter *painter)
{
    const qreal w = width();
    const qreal h = height();
    if (w <= 0 || h <= 0) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);

    // The bar runs left-to-right, or bottom-to-top when vertical.
    const qreal length = m_vertical ? h : w;
    const qreal filled = length * position();

    if (m_trackColor.isValid() && m_trackColor.alpha() > 0) {
        QPainterPath track;
        track.addRoundedRect(QRectF(0, 0, w, h), m_radius, m_radius);
        painter->fillPath(track, m_trackColor);
    }
    if (filled <= 0) {
        return;
    }

    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, w, h), m_radius, m_radius);
    painter->save();
    painter->setClipPath(clip);

    if (m_segments > 0) {
        // btop's blocks: every segment is drawn at its own position on the
        // ramp, and the ones past the reading are left to the track.
        const qreal step = length / qreal(m_segments);
        const qreal solid = std::max(qreal(1), step - m_segmentGap);
        for (int i = 0; i < m_segments; ++i) {
            const qreal start = step * qreal(i);
            if (start >= filled) {
                break;
            }
            const qreal t = (qreal(i) + 0.5) / qreal(m_segments);
            const QColor colour = colourAt(m_ramp, m_rampMode, t, position(), m_color);
            // A partly-reached segment is drawn in proportion, so a slow
            // climb reads as movement rather than as a sequence of jumps.
            const qreal drawn = std::min(solid, filled - start);
            const QRectF rect = m_vertical ? QRectF(0, h - start - drawn, w, drawn)
                                           : QRectF(start, 0, drawn, h);
            painter->fillRect(rect, colour);
        }
    } else if (m_ramp != nullptr && m_rampMode == ByPosition) {
        QLinearGradient gradient = m_vertical ? QLinearGradient(0, h, 0, 0)
                                              : QLinearGradient(0, 0, length, 0);
        for (const QVariant &entry : m_ramp->stops()) {
            const QVariantMap stop = entry.toMap();
            gradient.setColorAt(std::clamp(stop.value(QStringLiteral("position")).toDouble(),
                                           0.0, 1.0),
                                stop.value(QStringLiteral("color")).value<QColor>());
        }
        const QRectF rect = m_vertical ? QRectF(0, h - filled, w, filled)
                                       : QRectF(0, 0, filled, h);
        // AGENT-GUARD: the gradient spans the whole track, not the filled
        // part, so a reading's colour does not change as the bar grows.
        painter->fillRect(rect, gradient);
    } else {
        const QRectF rect = m_vertical ? QRectF(0, h - filled, w, filled)
                                       : QRectF(0, 0, filled, h);
        painter->fillRect(rect, valueColor());
    }
    painter->restore();
}

} // namespace QindaTK
