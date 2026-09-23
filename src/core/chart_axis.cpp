// SPDX-License-Identifier: LGPL-3.0-or-later
#include "chart_axis.h"

#include <QDateTime>
#include <QLocale>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
double invalid() { return std::numeric_limits<double>::quiet_NaN(); }
double niceStep(double raw)
{
    const double power = std::pow(10.0, std::floor(std::log10(raw)));
    const double scaled = raw / power;
    return (scaled <= 1 ? 1 : scaled <= 2 ? 2 : scaled <= 5 ? 5 : 10) * power;
}
}

ChartAxis::ChartAxis(QObject *parent) : QObject(parent) {}
void ChartAxis::setKind(Kind value) { if (m_kind != value) { m_kind = value; emit changed(); } }
void ChartAxis::setMinimum(double value) { if (m_minimum != value || std::isnan(value)) { m_minimum = value; emit changed(); } }
void ChartAxis::setMaximum(double value) { if (m_maximum != value || std::isnan(value)) { m_maximum = value; emit changed(); } }
void ChartAxis::setCategories(const QStringList &value) { if (m_categories != value) { m_categories = value; emit changed(); } }
void ChartAxis::setUnit(const QString &value) { if (m_unit != value) { m_unit = value; emit changed(); } }
void ChartAxis::setTimeZoneId(const QString &value) { if (m_timeZoneId != value) { m_timeZoneId = value; emit changed(); } }
QString ChartAxis::minimumUtc() const { return QString::number(m_minimumUtc); }
QString ChartAxis::maximumUtc() const { return QString::number(m_maximumUtc); }
void ChartAxis::setMinimumUtc(const QString &value)
{
    bool ok = false;
    const qint64 parsed = value.toLongLong(&ok);
    if (m_minimumUtc != parsed || m_utcDomainValid != ok) {
        m_minimumUtc = parsed; m_utcDomainValid = ok; emit changed();
    }
}
void ChartAxis::setMaximumUtc(const QString &value)
{
    bool ok = false;
    const qint64 parsed = value.toLongLong(&ok);
    if (m_maximumUtc != parsed || m_utcDomainValid != ok) {
        m_maximumUtc = parsed; m_utcDomainValid = ok; emit changed();
    }
}
void ChartAxis::setUtcDomain(qint64 minimum, qint64 maximum)
{
    m_minimumUtc = minimum; m_maximumUtc = maximum; m_utcDomainValid = true; emit changed();
}
void ChartAxis::setReversed(bool value) { if (m_reversed != value) { m_reversed = value; emit changed(); } }

QString ChartAxis::error() const
{
    if (m_kind == Category) {
        if (m_categories.isEmpty()) return QStringLiteral("Category axis has no categories");
        QStringList unique = m_categories;
        unique.removeDuplicates();
        if (unique.size() != m_categories.size()) return QStringLiteral("Category names must be unique");
        return {};
    }
    if (m_kind == DateTime) {
        if (!m_utcDomainValid || m_minimumUtc >= m_maximumUtc)
            return QStringLiteral("UTC domain must be valid and increasing");
        if (!QTimeZone(m_timeZoneId.toUtf8()).isValid()) return QStringLiteral("Invalid time zone");
        return {};
    }
    if (!std::isfinite(m_minimum) || !std::isfinite(m_maximum) || m_minimum >= m_maximum)
        return QStringLiteral("Axis domain must be finite and increasing");
    if (m_kind == Logarithmic && m_minimum <= 0)
        return QStringLiteral("Logarithmic axis domain must be positive");
    return {};
}
bool ChartAxis::isValid() const { return error().isEmpty(); }
int ChartAxis::categoryIndex(const QString &category) const { return m_categories.indexOf(category); }
double ChartAxis::map(double value) const
{
    if (!isValid() || !std::isfinite(value) || m_kind == DateTime) return invalid();
    double position = 0;
    if (m_kind == Category) {
        if (value < 0 || value >= m_categories.size() || std::floor(value) != value) return invalid();
        position = (value + 0.5) / m_categories.size();
    } else if (m_kind == Logarithmic) {
        if (value <= 0) return invalid();
        position = (std::log(value) - std::log(m_minimum)) / (std::log(m_maximum) - std::log(m_minimum));
    } else {
        position = (value - m_minimum) / (m_maximum - m_minimum);
    }
    return m_reversed ? 1 - position : position;
}
double ChartAxis::unmap(double fraction) const
{
    if (!isValid() || !std::isfinite(fraction) || m_kind == DateTime) return invalid();
    if (m_reversed) fraction = 1 - fraction;
    if (m_kind == Category) {
        if (fraction < 0 || fraction > 1) return invalid();
        return std::min(int(fraction * m_categories.size()), int(m_categories.size() - 1));
    }
    if (m_kind == Logarithmic)
        return std::exp(std::log(m_minimum) + fraction * (std::log(m_maximum) - std::log(m_minimum)));
    return m_minimum + fraction * (m_maximum - m_minimum);
}
double ChartAxis::mapUtc(qint64 milliseconds) const
{
    if (!isValid() || m_kind != DateTime) return invalid();
    // Subtract before conversion: contemporary timestamps exceed exact JS
    // integer precision, while ordinary viewport spans remain precise.
    const long double span = static_cast<long double>(m_maximumUtc) - m_minimumUtc;
    const long double offset = static_cast<long double>(milliseconds) - m_minimumUtc;
    const double fraction = double(offset / span);
    return m_reversed ? 1 - fraction : fraction;
}
qint64 ChartAxis::unmapUtc(double fraction) const
{
    if (!isValid() || m_kind != DateTime || !std::isfinite(fraction)) return 0;
    if (m_reversed) fraction = 1 - fraction;
    const long double span = static_cast<long double>(m_maximumUtc) - m_minimumUtc;
    return qint64(std::round(static_cast<long double>(m_minimumUtc) + fraction * span));
}

QVector<ChartTick> ChartAxis::ticks(int approximateCount) const
{
    QVector<ChartTick> result;
    if (!isValid()) return result;
    const int count = std::clamp(approximateCount, 2, 100);
    if (m_kind == Category) {
        for (int i = 0; i < m_categories.size(); ++i) result.append({double(i), m_categories.at(i)});
        return result;
    }
    if (m_kind == DateTime) {
        const long double span = static_cast<long double>(m_maximumUtc) - m_minimumUtc;
        const qint64 step = qMax<qint64>(1, qint64(span / (count - 1)));
        for (int i = 0; i < count; ++i) {
            const qint64 value = m_minimumUtc + qint64(i) * step;
            const QDateTime time = QDateTime::fromMSecsSinceEpoch(value, QTimeZone(m_timeZoneId.toUtf8()));
            result.append({double(value), QLocale().toString(time, step < 86400000 ? QStringLiteral("yyyy-MM-dd HH:mm") : QStringLiteral("yyyy-MM-dd")), value});
        }
        return result;
    }
    if (m_kind == Logarithmic) {
        const int first = int(std::ceil(std::log10(m_minimum)));
        const int last = int(std::floor(std::log10(m_maximum)));
        for (int exponent = first; exponent <= last && result.size() < 100; ++exponent) {
            const double value = std::pow(10.0, exponent);
            result.append({value, QLocale().toString(value, 'g', 6)});
        }
        return result;
    }
    const double step = niceStep((m_maximum - m_minimum) / (count - 1));
    for (double value = std::ceil(m_minimum / step) * step;
         value <= m_maximum + step * 1e-9 && result.size() < 101; value += step) {
        QString label;
        label = QLocale().toString(value, 'g', 10);
        result.append({value, label});
    }
    return result;
}
} // namespace QindaTK
