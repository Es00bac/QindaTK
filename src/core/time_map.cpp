// SPDX-License-Identifier: LGPL-3.0-or-later
#include "time_map.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace QindaTK {
namespace {
using Wide = detail::TimeMapWide;
constexpr quint64 million = 1000000;
constexpr qint64 maximumTempo = 60000000; // 1 to 60,000,000 us/quarter
constexpr qint64 maximumRatePart = 1000000;
constexpr qint64 maximumSegmentCount = 4096;

Wide wide(qint64 value) {
    const bool negative = value < 0;
    const quint64 magnitude = negative ? static_cast<quint64>(-(value + 1)) + 1 : static_cast<quint64>(value);
    Wide result;
    result.limb[0] = static_cast<quint32>(magnitude);
    result.limb[1] = static_cast<quint32>(magnitude >> 32);
    result.negative = negative;
    return result;
}

bool zero(const Wide &value) {
    return value.limb[0] == 0 && value.limb[1] == 0 && value.limb[2] == 0 && value.limb[3] == 0;
}

Wide negate(Wide value) {
    if (!zero(value)) value.negative = !value.negative;
    return value;
}

int compareMagnitude(const Wide &a, const Wide &b) {
    for (int index = 3; index >= 0; --index) {
        if (a.limb[index] != b.limb[index]) return a.limb[index] > b.limb[index] ? 1 : -1;
    }
    return 0;
}

int compare(const Wide &a, const Wide &b) {
    if (a.negative != b.negative) return a.negative ? -1 : 1;
    const int magnitude = compareMagnitude(a, b);
    return a.negative ? -magnitude : magnitude;
}

Wide add(Wide a, Wide b) {
    Wide result;
    if (a.negative == b.negative) {
        quint64 carry = 0;
        for (int index = 0; index < 4; ++index) {
            const quint64 sum = static_cast<quint64>(a.limb[index]) + b.limb[index] + carry;
            result.limb[index] = static_cast<quint32>(sum);
            carry = sum >> 32;
        }
        Q_ASSERT(carry == 0); // validated input bounds keep magnitude below 2^128
        result.negative = a.negative && !zero(result);
    } else {
        const int order = compareMagnitude(a, b);
        const Wide &larger = order >= 0 ? a : b;
        const Wide &smaller = order >= 0 ? b : a;
        quint64 borrow = 0;
        for (int index = 0; index < 4; ++index) {
            const quint64 subtrahend = static_cast<quint64>(smaller.limb[index]) + borrow;
            result.limb[index] = static_cast<quint32>(static_cast<quint64>(larger.limb[index]) - subtrahend);
            borrow = larger.limb[index] < subtrahend ? 1 : 0;
        }
        result.negative = larger.negative && !zero(result);
    }
    return result;
}

Wide subtract(Wide a, Wide b) { return add(a, negate(b)); }

Wide multiply(Wide value, quint64 factor) {
    // All factors at call sites are validated below 2^32.
    Q_ASSERT(factor <= std::numeric_limits<quint32>::max());
    quint64 carry = 0;
    for (int index = 0; index < 4; ++index) {
        const quint64 product = static_cast<quint64>(value.limb[index]) * factor + carry;
        value.limb[index] = static_cast<quint32>(product);
        carry = product >> 32;
    }
    Q_ASSERT(carry == 0);
    if (zero(value)) value.negative = false;
    return value;
}

struct Divided { Wide quotient; quint64 remainder = 0; };
Divided divide(Wide value, quint64 divisor) {
    Q_ASSERT(divisor != 0 && divisor < (quint64{1} << 63));
    Divided result;
    for (int bit = 127; bit >= 0; --bit) {
        result.remainder = (result.remainder << 1) | ((value.limb[bit / 32] >> (bit % 32)) & 1u);
        if (result.remainder >= divisor) {
            result.remainder -= divisor;
            result.quotient.limb[bit / 32] |= quint32{1} << (bit % 32);
        }
    }
    result.quotient.negative = value.negative && !zero(result.quotient);
    return result;
}

Wide rounded(Wide value, quint64 divisor, TimeMap::Rounding rounding) {
    const Divided parts = divide(value, divisor);
    Wide answer = parts.quotient;
    if (parts.remainder != 0) {
        if (rounding == TimeMap::Floor && value.negative) answer = subtract(answer, wide(1));
        else if (rounding == TimeMap::Ceil && !value.negative) answer = add(answer, wide(1));
        else if (rounding == TimeMap::NearestAway && parts.remainder >= (divisor + 1) / 2)
            answer = add(answer, wide(value.negative ? -1 : 1));
    }
    return answer;
}

std::optional<qint64> narrow(const Wide &value) {
    if (value.limb[2] != 0 || value.limb[3] != 0) return {};
    const quint64 magnitude = (static_cast<quint64>(value.limb[1]) << 32) | value.limb[0];
    if (!value.negative) {
        if (magnitude > static_cast<quint64>(std::numeric_limits<qint64>::max())) return {};
        return static_cast<qint64>(magnitude);
    }
    const quint64 minMagnitude = quint64{1} << 63;
    if (magnitude > minMagnitude) return {};
    if (magnitude == minMagnitude) return std::numeric_limits<qint64>::min();
    return -static_cast<qint64>(magnitude);
}

long double asLongDouble(const Wide &value) {
    long double magnitude = 0;
    for (int index = 3; index >= 0; --index)
        magnitude = magnitude * 4294967296.0L + value.limb[index];
    return value.negative ? -magnitude : magnitude;
}

QString padded(qint64 value, int width) {
    return QString::number(value).rightJustified(width, QLatin1Char('0'));
}
} // namespace

TimeMap::TimeMap(QObject *parent) : QObject(parent) {}

std::optional<qint64> TimeMap::parseInteger(const QString &text) {
    if (text.isEmpty() || text.trimmed() != text) return {};
    static const QRegularExpression decimal(QStringLiteral("^[+-]?[0-9]+$"));
    if (!decimal.match(text).hasMatch()) return {};
    bool ok = false;
    const qint64 value = text.toLongLong(&ok, 10);
    return ok ? std::optional<qint64>(value) : std::nullopt;
}

std::optional<qint64> TimeMap::quantize(Wide n, quint64 d, Rounding rounding) {
    if (d <= 0) return {};
    return narrow(rounded(n, d, rounding));
}

QVariantMap TimeMap::good(qint64 value) { return {{QStringLiteral("ok"), true}, {QStringLiteral("value"), QString::number(value)}}; }
QVariantMap TimeMap::bad(const QString &reason) { return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), reason}}; }
void TimeMap::reject(const QString &reason) { m_lastError = reason; emit rejected(reason); }

void TimeMap::setSampleRate(int rate) {
    if (rate < 1 || rate > 768000) { reject(QStringLiteral("sample rate must be 1..768000")); return; }
    if (m_sampleRate == rate) return;
    m_sampleRate = rate;
    emit changed();
}

bool TimeMap::setFrameRate(qint64 n, qint64 d) {
    if (n < 1 || d < 1 || n > maximumRatePart || d > maximumRatePart) {
        reject(QStringLiteral("frame rate parts must be 1..1000000")); return false;
    }
    const qint64 divisor = std::gcd(n, d);
    n /= divisor; d /= divisor;
    if (m_frameRate.numerator == n && m_frameRate.denominator == d) return true;
    m_frameRate = {n, d};
    emit changed();
    return true;
}

void TimeMap::setTicksPerQuarter(int ppq) {
    if (ppq < 1 || ppq > 1000000) { reject(QStringLiteral("ticks per quarter must be 1..1000000")); return; }
    if (m_ppq == ppq) return;
    m_ppq = ppq;
    emit changed();
}

QString TimeMap::originSample() const { return QString::number(m_originSample); }
void TimeMap::setOriginSample(const QString &value) {
    const auto parsed = parseInteger(value);
    if (!parsed) { reject(QStringLiteral("invalid origin sample")); return; }
    if (m_originSample == *parsed) return;
    m_originSample = *parsed;
    emit changed();
}

QVariantList TimeMap::tempoSegments() const {
    QVariantList list;
    for (const Segment &segment : m_segments)
        list.push_back(QVariantMap{{QStringLiteral("tick"), QString::number(segment.tick)},
                                   {QStringLiteral("microsecondsPerQuarter"), QString::number(segment.microsecondsPerQuarter)},
                                   {QStringLiteral("numerator"), segment.numerator},
                                   {QStringLiteral("denominator"), segment.denominator}});
    return list;
}

void TimeMap::setTempoSegments(const QVariantList &list) {
    if (list.isEmpty() || list.size() > maximumSegmentCount) {
        reject(QStringLiteral("tempo map requires 1..4096 segments")); return;
    }
    std::vector<Segment> next;
    next.reserve(static_cast<size_t>(list.size()));
    for (const QVariant &item : list) {
        if (!item.canConvert<QVariantMap>()) { reject(QStringLiteral("invalid tempo segment")); return; }
        const QVariantMap map = item.toMap();
        const auto tick = parseInteger(map.value(QStringLiteral("tick")).toString());
        const auto tempo = parseInteger(map.value(QStringLiteral("microsecondsPerQuarter")).toString());
        bool nOk = false, dOk = false;
        const int numerator = map.value(QStringLiteral("numerator")).toInt(&nOk);
        const int denominator = map.value(QStringLiteral("denominator")).toInt(&dOk);
        if (!tick || !tempo || *tick < 0 || *tempo < 1 || *tempo > maximumTempo ||
            !nOk || !dOk || numerator < 1 || numerator > 128 || denominator < 1 || denominator > 128 ||
            (denominator & (denominator - 1)) != 0 ||
            (!next.empty() && *tick <= next.back().tick) || (next.empty() && *tick != 0)) {
            reject(QStringLiteral("tempo segments need ascending nonnegative ticks, tempo 1..60000000, and valid meter"));
            return;
        }
        Segment segment{*tick, *tempo, numerator, denominator, {}};
        if (!next.empty()) {
            const Segment &previous = next.back();
            segment.startUnits = add(previous.startUnits,
                multiply(wide(segment.tick - previous.tick), static_cast<quint64>(previous.microsecondsPerQuarter)));
        }
        next.push_back(segment);
    }
    m_segments = std::move(next);
    emit changed();
}

int TimeMap::segmentForTick(const std::vector<Segment> &segments, qint64 tick) {
    const auto it = std::upper_bound(segments.begin(), segments.end(), tick,
                                      [](qint64 value, const Segment &segment) { return value < segment.tick; });
    return it == segments.begin() ? 0 : static_cast<int>(it - segments.begin() - 1);
}

int TimeMap::segmentForSample(Wide delta) const {
    const Wide scaled = multiply(multiply(delta, static_cast<quint64>(m_ppq)), million);
    const auto it = std::upper_bound(m_segments.begin(), m_segments.end(), scaled,
        [this](const Wide &value, const Segment &segment) {
            return compare(value, multiply(segment.startUnits, static_cast<quint64>(m_sampleRate))) < 0;
        });
    return it == m_segments.begin() ? 0 : static_cast<int>(it - m_segments.begin() - 1);
}

std::optional<SamplePosition> TimeMap::fromFrame(FrameIndex frame, Rounding rounding) const {
    const Wide delta = rounded(multiply(multiply(wide(frame.value), static_cast<quint64>(m_sampleRate)),
                                       static_cast<quint64>(m_frameRate.denominator)),
                               static_cast<quint64>(m_frameRate.numerator), rounding);
    const auto absolute = narrow(add(wide(m_originSample), delta));
    return absolute ? std::optional<SamplePosition>(SamplePosition{*absolute}) : std::nullopt;
}

std::optional<FrameIndex> TimeMap::toFrame(SamplePosition sample, Rounding rounding) const {
    const Wide delta = subtract(wide(sample.value), wide(m_originSample));
    const auto frame = quantize(multiply(delta, static_cast<quint64>(m_frameRate.numerator)),
                                static_cast<quint64>(m_sampleRate) * m_frameRate.denominator, rounding);
    return frame ? std::optional<FrameIndex>(FrameIndex{*frame}) : std::nullopt;
}

std::optional<SamplePosition> TimeMap::fromTick(MusicalTick tick, Rounding rounding) const {
    const Segment &segment = m_segments.at(static_cast<size_t>(segmentForTick(m_segments, tick.value)));
    const Wide units = add(segment.startUnits,
        multiply(subtract(wide(tick.value), wide(segment.tick)), static_cast<quint64>(segment.microsecondsPerQuarter)));
    const Wide delta = rounded(multiply(units, static_cast<quint64>(m_sampleRate)),
                               static_cast<quint64>(m_ppq) * million, rounding);
    const auto absolute = narrow(add(wide(m_originSample), delta));
    return absolute ? std::optional<SamplePosition>(SamplePosition{*absolute}) : std::nullopt;
}

std::optional<MusicalTick> TimeMap::toTick(SamplePosition sample, Rounding rounding) const {
    const Wide difference = subtract(wide(sample.value), wide(m_originSample));
    const Segment &segment = m_segments.at(static_cast<size_t>(segmentForSample(difference)));
    const Wide n = subtract(multiply(multiply(difference, static_cast<quint64>(m_ppq)), million),
                            multiply(segment.startUnits, static_cast<quint64>(m_sampleRate)));
    const auto offset = quantize(n, static_cast<quint64>(segment.microsecondsPerQuarter) * m_sampleRate, rounding);
    if (!offset) return {};
    const auto tick = narrow(add(wide(segment.tick), wide(*offset)));
    return tick ? std::optional<MusicalTick>(MusicalTick{*tick}) : std::nullopt;
}

QVariantMap TimeMap::frameToSample(const QString &text, Rounding rounding) const {
    const auto value = parseInteger(text); if (!value) return bad(QStringLiteral("invalid frame index"));
    const auto result = fromFrame({*value}, rounding);
    return result ? good(result->value) : bad(QStringLiteral("sample position out of range"));
}
QVariantMap TimeMap::sampleToFrame(const QString &text, Rounding rounding) const {
    const auto value = parseInteger(text); if (!value) return bad(QStringLiteral("invalid sample position"));
    const auto result = toFrame({*value}, rounding);
    return result ? good(result->value) : bad(QStringLiteral("frame index out of range"));
}
QVariantMap TimeMap::tickToSample(const QString &text, Rounding rounding) const {
    const auto value = parseInteger(text); if (!value) return bad(QStringLiteral("invalid musical tick"));
    const auto result = fromTick({*value}, rounding);
    return result ? good(result->value) : bad(QStringLiteral("sample position out of range"));
}
QVariantMap TimeMap::sampleToTick(const QString &text, Rounding rounding) const {
    const auto value = parseInteger(text); if (!value) return bad(QStringLiteral("invalid sample position"));
    const auto result = toTick({*value}, rounding);
    return result ? good(result->value) : bad(QStringLiteral("musical tick out of range"));
}
QVariantMap TimeMap::compareExact(const QString &left, const QString &right) const {
    const auto a = parseInteger(left), b = parseInteger(right);
    if (!a || !b) return bad(QStringLiteral("invalid decimal position"));
    return good((*a > *b) - (*a < *b));
}

QVariantMap TimeMap::formatTimecode(const QString &text, bool dropFrame) const {
    const auto input = parseInteger(text);
    if (!input) return bad(QStringLiteral("invalid frame index"));
    const bool supportedDrop = (m_frameRate.numerator == 30000 && m_frameRate.denominator == 1001) ||
                               (m_frameRate.numerator == 60000 && m_frameRate.denominator == 1001);
    if (dropFrame && !supportedDrop) return bad(QStringLiteral("drop-frame supports only 30000/1001 or 60000/1001"));
    const qint64 nominal = (m_frameRate.numerator * 2 + m_frameRate.denominator) /
                           (2 * m_frameRate.denominator);
    if (nominal < 1 || nominal > 120) return bad(QStringLiteral("timecode nominal rate must be 1..120"));
    Wide count = wide(*input);
    if (count.negative) count = negate(count);
    if (dropFrame) {
        const qint64 drop = nominal == 30 ? 2 : 4;
        const qint64 perTen = nominal * 600 - drop * 9;
        const qint64 perMinute = nominal * 60 - drop;
        const Divided ten = divide(count, static_cast<quint64>(perTen));
        const Wide dropped = add(multiply(ten.quotient, static_cast<quint64>(drop * 9)),
            wide(ten.remainder < static_cast<quint64>(drop) ? 0 :
                 static_cast<qint64>((ten.remainder - drop) / perMinute * drop)));
        count = add(count, dropped);
    }
    const Divided framePart = divide(count, static_cast<quint64>(nominal));
    const qint64 frames = static_cast<qint64>(framePart.remainder);
    const Divided secondPart = divide(framePart.quotient, 60);
    const qint64 seconds = static_cast<qint64>(secondPart.remainder);
    const Divided minutePart = divide(secondPart.quotient, 60);
    const qint64 minutes = static_cast<qint64>(minutePart.remainder);
    const auto hours = narrow(minutePart.quotient);
    if (!hours) return bad(QStringLiteral("timecode hours out of range"));
    const QString result = (*input < 0 ? QStringLiteral("-") : QString()) +
        padded(*hours, 2) + QLatin1Char(':') + padded(minutes, 2) + QLatin1Char(':') +
        padded(seconds, 2) + (dropFrame ? QLatin1Char(';') : QLatin1Char(':')) + padded(frames, 2);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("value"), result}};
}

QVariantMap TimeMap::parseTimecode(const QString &label, bool dropFrame) const {
    const bool supportedDrop = (m_frameRate.numerator == 30000 && m_frameRate.denominator == 1001) ||
                               (m_frameRate.numerator == 60000 && m_frameRate.denominator == 1001);
    if (dropFrame && !supportedDrop) return bad(QStringLiteral("drop-frame supports only 30000/1001 or 60000/1001"));
    const qint64 nominal = (m_frameRate.numerator * 2 + m_frameRate.denominator) /
                           (2 * m_frameRate.denominator);
    if (nominal < 1 || nominal > 120) return bad(QStringLiteral("timecode nominal rate must be 1..120"));
    static const QRegularExpression pattern(QStringLiteral("^(-?)([0-9]+):([0-9]{2}):([0-9]{2})([:;])([0-9]{2,3})$"));
    const auto match = pattern.match(label);
    if (!match.hasMatch() || (match.captured(5) == QStringLiteral(";")) != dropFrame)
        return bad(QStringLiteral("invalid timecode syntax"));
    bool ok = false;
    const qint64 hours = match.captured(2).toLongLong(&ok);
    if (!ok) return bad(QStringLiteral("timecode hours out of range"));
    const qint64 minutes = match.captured(3).toLongLong();
    const qint64 seconds = match.captured(4).toLongLong();
    const qint64 frames = match.captured(6).toLongLong();
    if (minutes >= 60 || seconds >= 60 || frames >= nominal)
        return bad(QStringLiteral("timecode component out of range"));
    const qint64 drop = dropFrame ? (nominal == 30 ? 2 : 4) : 0;
    if (dropFrame && minutes % 10 != 0 && seconds == 0 && frames < drop)
        return bad(QStringLiteral("skipped drop-frame label"));
    const Wide totalMinutes = add(multiply(wide(hours), 60), wide(minutes));
    Wide count = add(multiply(add(multiply(totalMinutes, 60), wide(seconds)),
                              static_cast<quint64>(nominal)), wide(frames));
    if (dropFrame) {
        const Wide droppedMinutes = subtract(totalMinutes, divide(totalMinutes, 10).quotient);
        count = subtract(count, multiply(droppedMinutes, static_cast<quint64>(drop)));
    }
    if (match.captured(1) == QStringLiteral("-")) count = negate(count);
    const auto result = narrow(count);
    if (!result)
        return bad(QStringLiteral("frame index out of range"));
    return good(*result);
}

QVariantMap TimeMap::sampleToPixel(const QString &sample, const QString &viewportOriginSample,
                                    double pixelsPerSample) const {
    const auto position = parseInteger(sample), origin = parseInteger(viewportOriginSample);
    if (!position || !origin) return bad(QStringLiteral("invalid decimal sample position"));
    if (!std::isfinite(pixelsPerSample) || pixelsPerSample <= 0)
        return bad(QStringLiteral("pixels per sample must be positive and finite"));
    const Wide delta = subtract(wide(*position), wide(*origin));
    Wide magnitude = delta;
    magnitude.negative = false;
    if (compareMagnitude(magnitude, wide(9007199254740992LL)) > 0)
        return bad(QStringLiteral("sample displacement exceeds precise display range"));
    // Rendering is relative to the visible origin, never to absolute session time.
    const long double pixel = asLongDouble(delta) * pixelsPerSample;
    if (!std::isfinite(pixel) || std::abs(pixel) > 9007199254740992.0L)
        return bad(QStringLiteral("pixel displacement exceeds precise display range"));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("value"), static_cast<double>(pixel)}};
}

QVariantMap TimeMap::pixelToSample(double pixel, const QString &viewportOriginSample,
                                    double pixelsPerSample, Rounding rounding) const {
    const auto origin = parseInteger(viewportOriginSample);
    if (!origin) return bad(QStringLiteral("invalid viewport origin sample"));
    if (!std::isfinite(pixel) || !std::isfinite(pixelsPerSample) || pixelsPerSample <= 0)
        return bad(QStringLiteral("pixel and scale must be finite, scale positive"));
    const long double displacement = static_cast<long double>(pixel) / pixelsPerSample;
    if (!std::isfinite(displacement) || std::abs(displacement) > 9007199254740992.0L)
        return bad(QStringLiteral("sample displacement exceeds precise display range"));
    long double rounded = std::trunc(displacement);
    if (rounding == Floor) rounded = std::floor(displacement);
    else if (rounding == Ceil) rounded = std::ceil(displacement);
    else if (rounding == NearestAway) rounded = std::round(displacement);
    const auto result = narrow(add(wide(*origin), wide(static_cast<qint64>(rounded))));
    if (!result)
        return bad(QStringLiteral("sample position out of range"));
    return good(*result);
}

} // namespace QindaTK
