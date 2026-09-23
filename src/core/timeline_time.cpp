// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_time.h"

#include <QRegularExpression>
#include <algorithm>
#include <limits>
#include <numeric>

namespace QindaTK {
namespace {
using Rounding = TimeMap::Rounding;

struct U128 {
    quint64 hi = 0;
    quint64 lo = 0;
};

U128 multiply(quint64 a, quint64 b)
{
    const quint64 mask = 0xffffffffULL;
    const quint64 a0 = a & mask, a1 = a >> 32;
    const quint64 b0 = b & mask, b1 = b >> 32;
    const quint64 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const quint64 middle = (p00 >> 32) + (p01 & mask) + (p10 & mask);
    return {p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32),
            (p00 & mask) | (middle << 32)};
}

// Restoring long division. Requires divisor <= 2^62 so the running
// remainder never overflows when shifted. Fails if the quotient needs more
// than 64 bits (hi >= divisor).
bool divide(U128 n, quint64 divisor, quint64 &quotient, quint64 &remainder)
{
    if (n.hi >= divisor) return false;
    quint64 rem = n.hi;
    quint64 q = 0;
    for (int bit = 63; bit >= 0; --bit) {
        rem = (rem << 1) | ((n.lo >> bit) & 1ULL);
        q <<= 1;
        if (rem >= divisor) {
            rem -= divisor;
            q |= 1ULL;
        }
    }
    quotient = q;
    remainder = rem;
    return true;
}

quint64 power10(int exponent)
{
    quint64 value = 1;
    for (int i = 0; i < exponent; ++i) value *= 10;
    return value;
}
} // namespace

bool TimelineRate::isValid() const
{
    return numerator >= 1 && denominator >= 1 && numerator <= TimelineTime::kMaxRatePart
        && denominator <= TimelineTime::kMaxRatePart;
}

std::optional<TimelineRate> TimelineRate::normalized(qint64 numerator, qint64 denominator)
{
    const TimelineRate raw{numerator, denominator};
    if (!raw.isValid()) return std::nullopt;
    const qint64 divisor = std::gcd(numerator, denominator);
    return TimelineRate{numerator / divisor, denominator / divisor};
}

namespace TimelineTime {

std::optional<qint64> mulDiv(qint64 value, quint64 multiplier, quint64 divisor,
                             Rounding rounding)
{
    constexpr quint64 limit = quint64(1) << 62;
    if (divisor == 0 || multiplier > limit || divisor > limit) return std::nullopt;
    const bool negative = value < 0;
    const quint64 magnitude = negative ? quint64(0) - quint64(value) : quint64(value);
    quint64 q = 0, r = 0;
    if (!divide(multiply(magnitude, multiplier), divisor, q, r)) return std::nullopt;
    bool increase = false;
    if (r != 0) {
        switch (rounding) {
        case TimeMap::TowardZero: increase = false; break;
        case TimeMap::Floor: increase = negative; break;
        case TimeMap::Ceil: increase = !negative; break;
        case TimeMap::NearestAway: increase = r >= divisor - r; break;
        }
    }
    if (increase) {
        if (q == std::numeric_limits<quint64>::max()) return std::nullopt;
        ++q;
    }
    constexpr quint64 minMagnitude = quint64(1) << 63;
    if (negative) {
        if (q > minMagnitude) return std::nullopt;
        if (q == minMagnitude) return std::numeric_limits<qint64>::min();
        return -qint64(q);
    }
    if (q > quint64(std::numeric_limits<qint64>::max())) return std::nullopt;
    return qint64(q);
}

std::optional<qint64> add(qint64 left, qint64 right)
{
    constexpr auto max = std::numeric_limits<qint64>::max();
    constexpr auto min = std::numeric_limits<qint64>::min();
    if ((right > 0 && left > max - right) || (right < 0 && left < min - right))
        return std::nullopt;
    return left + right;
}

std::optional<qint64> subtract(qint64 left, qint64 right)
{
    constexpr auto max = std::numeric_limits<qint64>::max();
    constexpr auto min = std::numeric_limits<qint64>::min();
    if ((right < 0 && left > max + right) || (right > 0 && left < min + right))
        return std::nullopt;
    return left - right;
}

std::optional<qint64> parseDecimal(const QString &text)
{
    if (text.isEmpty()) return std::nullopt;
    const qsizetype first = text.front() == QLatin1Char('-') ? 1 : 0;
    if (first == text.size()) return std::nullopt;
    for (qsizetype i = first; i < text.size(); ++i)
        if (text[i] < QLatin1Char('0') || text[i] > QLatin1Char('9')) return std::nullopt;
    bool ok = false;
    const qint64 value = text.toLongLong(&ok, 10);
    return ok ? std::optional<qint64>(value) : std::nullopt;
}

QString toDecimal(qint64 value)
{
    return QString::number(value);
}

std::optional<qint64> rescale(qint64 value, TimelineRate from, TimelineRate to,
                              Rounding rounding)
{
    if (!from.isValid() || !to.isValid()) return std::nullopt;
    quint64 multiplier = quint64(to.numerator) * quint64(from.denominator);
    quint64 divisor = quint64(to.denominator) * quint64(from.numerator);
    const quint64 common = std::gcd(multiplier, divisor);
    multiplier /= common;
    divisor /= common;
    return mulDiv(value, multiplier, divisor, rounding);
}

QString formatSeconds(qint64 ticks, TimelineRate ticksPerSecond, int decimals)
{
    if (!ticksPerSecond.isValid()) return {};
    decimals = std::clamp(decimals, 0, 9);
    const quint64 scale = power10(decimals);
    quint64 multiplier = quint64(ticksPerSecond.denominator) * scale;
    quint64 divisor = quint64(ticksPerSecond.numerator);
    const quint64 common = std::gcd(multiplier, divisor);
    const auto scaled = mulDiv(ticks, multiplier / common, divisor / common,
                               TimeMap::NearestAway);
    if (!scaled) return {};
    const bool negative = *scaled < 0;
    const quint64 magnitude = negative ? quint64(0) - quint64(*scaled) : quint64(*scaled);
    QString text = negative && magnitude != 0 ? QStringLiteral("-") : QString();
    text += QString::number(magnitude / scale);
    if (decimals > 0) {
        text += QLatin1Char('.');
        text += QString::number(magnitude % scale).rightJustified(decimals, QLatin1Char('0'));
    }
    return text;
}

std::optional<qint64> parseSeconds(const QString &text, TimelineRate ticksPerSecond,
                                   Rounding rounding)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(-?)([0-9]+)(?:\\.([0-9]{1,9}))?$"));
    const auto match = pattern.match(text);
    if (!match.hasMatch() || !ticksPerSecond.isValid()) return std::nullopt;
    const QString fraction = match.captured(3);
    const quint64 scale = power10(int(fraction.size()));
    const auto whole = parseDecimal(match.captured(2));
    if (!whole) return std::nullopt;
    const auto wholeScaled = mulDiv(*whole, scale, 1, TimeMap::TowardZero);
    const auto fractionValue = fraction.isEmpty() ? std::optional<qint64>(0)
                                                  : parseDecimal(fraction);
    if (!wholeScaled || !fractionValue) return std::nullopt;
    auto scaled = add(*wholeScaled, *fractionValue);
    if (!scaled) return std::nullopt;
    if (!match.captured(1).isEmpty()) scaled = -*scaled;
    quint64 multiplier = quint64(ticksPerSecond.numerator);
    quint64 divisor = quint64(ticksPerSecond.denominator) * scale;
    const quint64 common = std::gcd(multiplier, divisor);
    return mulDiv(*scaled, multiplier / common, divisor / common, rounding);
}

std::optional<std::pair<qint64, qint64>> gridBracket(qint64 ticks, qint64 origin,
                                                     quint64 stepNumerator,
                                                     quint64 stepDenominator)
{
    if (stepNumerator == 0 || stepDenominator == 0) return std::nullopt;
    const auto relative = subtract(ticks, origin);
    if (!relative) return std::nullopt;
    const auto index = mulDiv(*relative, stepDenominator, stepNumerator, TimeMap::Floor);
    if (!index) return std::nullopt;
    const auto next = add(*index, 1);
    if (!next) return std::nullopt;
    const auto low = mulDiv(*index, stepNumerator, stepDenominator, TimeMap::Ceil);
    const auto high = mulDiv(*next, stepNumerator, stepDenominator, TimeMap::Ceil);
    if (!low || !high) return std::nullopt;
    const auto lowTicks = add(origin, *low);
    const auto highTicks = add(origin, *high);
    if (!lowTicks || !highTicks) return std::nullopt;
    return std::make_pair(*lowTicks, *highTicks);
}

} // namespace TimelineTime
} // namespace QindaTK
