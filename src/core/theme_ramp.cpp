// SPDX-License-Identifier: LGPL-3.0-or-later
#include "theme_ramp.h"

#include "theme.h"

#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace QindaTK {

namespace {

// sRGB interpolation, matching Theme::mix: QML paints in this space, so a
// ramp interpolated here lands on the same colour a QML gradient would.
QColor lerp(const QColor &a, const QColor &b, qreal t)
{
    const qreal u = 1.0 - t;
    return QColor::fromRgbF(a.redF() * u + b.redF() * t,
                            a.greenF() * u + b.greenF() * t,
                            a.blueF() * u + b.blueF() * t,
                            a.alphaF() * u + b.alphaF() * t);
}

} // namespace

// ---- ThemeRamp -----------------------------------------------------------

ThemeRamp::ThemeRamp(QObject *parent)
    : QObject(parent)
{
}

void ThemeRamp::setStops(std::vector<Stop> stops)
{
    stops.erase(std::remove_if(stops.begin(), stops.end(),
                               [](const Stop &s) { return !s.color.isValid(); }),
                stops.end());
    for (Stop &stop : stops) {
        stop.position = std::clamp(stop.position, qreal(0), qreal(1));
    }
    std::stable_sort(stops.begin(), stops.end(),
                     [](const Stop &a, const Stop &b) { return a.position < b.position; });
    m_stops = std::move(stops);
    emit changed();
}

QColor ThemeRamp::at(qreal t) const
{
    if (m_stops.empty()) {
        return {};
    }
    if (!std::isfinite(t) || t <= m_stops.front().position) {
        return m_stops.front().color;
    }
    if (t >= m_stops.back().position) {
        return m_stops.back().color;
    }
    for (std::size_t i = 1; i < m_stops.size(); ++i) {
        const Stop &hi = m_stops[i];
        if (t > hi.position) {
            continue;
        }
        const Stop &lo = m_stops[i - 1];
        const qreal span = hi.position - lo.position;
        // Coincident stops are a hard edge, not a division by zero.
        if (span <= 0) {
            return hi.color;
        }
        return lerp(lo.color, hi.color, (t - lo.position) / span);
    }
    return m_stops.back().color;
}

QColor ThemeRamp::forValue(qreal value, qreal from, qreal to) const
{
    if (!(to > from)) {
        return m_stops.empty() ? QColor() : m_stops.front().color;
    }
    return at((value - from) / (to - from));
}

QVariantList ThemeRamp::stops() const
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(m_stops.size()));
    for (const Stop &stop : m_stops) {
        QVariantMap entry;
        entry.insert(QStringLiteral("position"), stop.position);
        entry.insert(QStringLiteral("color"), stop.color);
        list.append(entry);
    }
    return list;
}

QList<QColor> ThemeRamp::colors() const
{
    QList<QColor> list;
    list.reserve(static_cast<qsizetype>(m_stops.size()));
    for (const Stop &stop : m_stops) {
        list.append(stop.color);
    }
    return list;
}

// ---- ThemeRamps ----------------------------------------------------------

ThemeRamps::ThemeRamps(QObject *parent)
    : QObject(parent)
    , m_load(new ThemeRamp(this))
    , m_thermal(new ThemeRamp(this))
    , m_memory(new ThemeRamp(this))
    , m_network(new ThemeRamp(this))
    , m_io(new ThemeRamp(this))
    , m_neutral(new ThemeRamp(this))
{
}

QStringList ThemeRamps::names()
{
    return {QStringLiteral("load"),    QStringLiteral("thermal"), QStringLiteral("memory"),
            QStringLiteral("network"), QStringLiteral("io"),      QStringLiteral("neutral")};
}

ThemeRamp *ThemeRamps::byName(const QString &name) const
{
    if (name == QLatin1String("load")) {
        return m_load;
    }
    if (name == QLatin1String("thermal")) {
        return m_thermal;
    }
    if (name == QLatin1String("memory")) {
        return m_memory;
    }
    if (name == QLatin1String("network")) {
        return m_network;
    }
    if (name == QLatin1String("io")) {
        return m_io;
    }
    if (name == QLatin1String("neutral")) {
        return m_neutral;
    }
    return nullptr;
}

// AGENT-NOTE: the stop positions are the measured shape of btop's gradients,
// not a preference: utilisation stays "calm" through the first half of the
// range and turns only as it approaches saturation, so an idle machine is
// visually quiet. Only the *colours* come from the theme; moving a position
// changes what the reader believes about the machine.
void ThemeRamps::rebuild(const ThemeColors &c)
{
    m_load->setStops({{0.0, c.success()}, {0.55, c.warning()}, {1.0, c.danger()}});
    m_thermal->setStops(
        {{0.0, c.info()}, {0.45, c.success()}, {0.75, c.warning()}, {1.0, c.danger()}});
    m_memory->setStops({{0.0, c.accent()}, {0.6, c.warning()}, {1.0, c.danger()}});
    // AGENT-GUARD: throughput is not a hazard -- a saturated link is not a
    // fault the way a saturated disk queue is -- so `network` ramps accent
    // brightness rather than green->red. It is built by mixing against bg
    // and text instead of naming two roles, because a preset may resolve
    // accent and info to the same colour (sloom-dark does) and a ramp whose
    // ends collapse renders every reading identically.
    m_network->setStops({{0.0, lerp(c.bg(), c.accent(), 0.5)},
                         {0.55, c.accent()},
                         {1.0, lerp(c.accent(), c.text(), 0.55)}});
    m_io->setStops({{0.0, c.accent()}, {0.6, c.warning()}, {1.0, c.danger()}});
    m_neutral->setStops({{0.0, c.textMuted()}, {1.0, c.text()}});
}

} // namespace QindaTK
