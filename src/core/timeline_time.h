// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "time_map.h"

#include <QString>
#include <optional>

namespace QindaTK {

// An exact positive rational rate: ticks, samples or frames per second.
// Parts are limited to 1..2^31-1 so every product of two parts fits the
// unsigned 64-bit factors used by TimelineTime::mulDiv.
struct TimelineRate {
    qint64 numerator = 1;
    qint64 denominator = 1;
    bool isValid() const;
    static std::optional<TimelineRate> normalized(qint64 numerator, qint64 denominator);
    friend bool operator==(const TimelineRate &, const TimelineRate &) = default;
};

// AGENT-NOTE: canonical edit time is a signed 64-bit tick count in the
// timeline's declared timebase (ticks per second). Pixels, seconds as
// double and QML Numbers are presentation values only. Every conversion
// here rounds exactly once, with the caller's TimeMap::Rounding, from a
// portable 128-bit intermediate (no compiler extension, like TimeMap).
namespace TimelineTime {
using Rounding = TimeMap::Rounding;

// Default timebase: 705,600,000 ticks per second. It divides evenly into
// 8/16/22.05/24/32/44.1/48/88.2/96/176.4/192 kHz samples and into the frame
// durations of 24, 25, 30, 48, 50, 60, 120 fps and their 1000/1001 forms.
inline constexpr qint64 kDefaultTicksPerSecond = 705600000;
inline constexpr qint64 kMaxRatePart = 2147483647;

// value * multiplier / divisor, rounded once. std::nullopt on overflow or
// when divisor is zero or either factor exceeds 2^62.
std::optional<qint64> mulDiv(qint64 value, quint64 multiplier, quint64 divisor,
                             Rounding rounding);
std::optional<qint64> add(qint64 left, qint64 right);
std::optional<qint64> subtract(qint64 left, qint64 right);
// Strict signed decimal: optional '-', ASCII digits, no spaces or '+'.
std::optional<qint64> parseDecimal(const QString &text);
QString toDecimal(qint64 value);
// value counted at `from` per second, re-expressed at `to` per second.
std::optional<qint64> rescale(qint64 value, TimelineRate from, TimelineRate to,
                              Rounding rounding);
// Exact decimal seconds text ("-12.3456"), rounded NearestAway at `decimals`.
QString formatSeconds(qint64 ticks, TimelineRate ticksPerSecond, int decimals);
// Parses "12", "-0.5", "3.000125" seconds exactly, then rounds to ticks.
std::optional<qint64> parseSeconds(const QString &text, TimelineRate ticksPerSecond,
                                   Rounding rounding);
// Grid lines at origin + ceil(k * stepNumerator / stepDenominator) ticks.
// Returns the line at or before `ticks` (first) and the next line (second).
std::optional<std::pair<qint64, qint64>> gridBracket(qint64 ticks, qint64 origin,
                                                     quint64 stepNumerator,
                                                     quint64 stepDenominator);
} // namespace TimelineTime

} // namespace QindaTK
