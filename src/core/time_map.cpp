// SPDX-License-Identifier: LGPL-3.0-or-later
#include "time_map.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace QindaTK {
namespace {
constexpr __int128 million = 1000000;
constexpr qint64 maximumTempo = 60000000; // 1 to 60,000,000 us/quarter
constexpr qint64 maximumRatePart = 1000000;
constexpr qint64 maximumSegmentCount = 4096;

std::optional<qint64> narrow(__int128 value) {
    if (value < std::numeric_limits<qint64>::min() || value > std::numeric_limits<qint64>::max()) return {};
    return static_cast<qint64>(value);
}

__int128 rounded(__int128 n, __int128 d, TimeMap::Rounding rounding) {
    __int128 q = n / d;
    const __int128 r = n % d;
    if (r != 0) {
        if (rounding == TimeMap::Floor && n < 0) --q;
        else if (rounding == TimeMap::Ceil && n > 0) ++q;
        else if (rounding == TimeMap::NearestAway && (r < 0 ? -r : r) * 2 >= d) q += n < 0 ? -1 : 1;
    }
    return q;
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

std::optional<qint64> TimeMap::quantize(__int128 n, __int128 d, Rounding rounding) {
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
        Segment segment{*tick, *tempo, numerator, denominator, 0};
        if (!next.empty()) {
            const Segment &previous = next.back();
            segment.startUnits = previous.startUnits +
                static_cast<__int128>(segment.tick - previous.tick) * previous.microsecondsPerQuarter;
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

int TimeMap::segmentForSample(__int128 delta) const {
    const __int128 scaled = delta * m_ppq * million;
    const auto it = std::upper_bound(m_segments.begin(), m_segments.end(), scaled,
        [this](__int128 value, const Segment &segment) { return value < segment.startUnits * m_sampleRate; });
    return it == m_segments.begin() ? 0 : static_cast<int>(it - m_segments.begin() - 1);
}

std::optional<SamplePosition> TimeMap::fromFrame(FrameIndex frame, Rounding rounding) const {
    const __int128 delta = rounded(static_cast<__int128>(frame.value) * m_sampleRate * m_frameRate.denominator,
                                    m_frameRate.numerator, rounding);
    const auto absolute = narrow(static_cast<__int128>(m_originSample) + delta);
    return absolute ? std::optional<SamplePosition>(SamplePosition{*absolute}) : std::nullopt;
}

std::optional<FrameIndex> TimeMap::toFrame(SamplePosition sample, Rounding rounding) const {
    const __int128 delta = static_cast<__int128>(sample.value) - m_originSample;
    const auto frame = quantize(delta * m_frameRate.numerator,
                                static_cast<__int128>(m_sampleRate) * m_frameRate.denominator, rounding);
    return frame ? std::optional<FrameIndex>(FrameIndex{*frame}) : std::nullopt;
}

std::optional<SamplePosition> TimeMap::fromTick(MusicalTick tick, Rounding rounding) const {
    const Segment &segment = m_segments.at(static_cast<size_t>(segmentForTick(m_segments, tick.value)));
    const __int128 units = segment.startUnits +
                           (static_cast<__int128>(tick.value) - segment.tick) * segment.microsecondsPerQuarter;
    const __int128 delta = rounded(units * m_sampleRate, static_cast<__int128>(m_ppq) * million, rounding);
    const auto absolute = narrow(static_cast<__int128>(m_originSample) + delta);
    return absolute ? std::optional<SamplePosition>(SamplePosition{*absolute}) : std::nullopt;
}

std::optional<MusicalTick> TimeMap::toTick(SamplePosition sample, Rounding rounding) const {
    const __int128 difference = static_cast<__int128>(sample.value) - m_originSample;
    const Segment &segment = m_segments.at(static_cast<size_t>(segmentForSample(difference)));
    const __int128 n = difference * m_ppq * million - segment.startUnits * m_sampleRate;
    const auto offset = quantize(n, static_cast<__int128>(segment.microsecondsPerQuarter) * m_sampleRate, rounding);
    if (!offset) return {};
    const auto tick = narrow(static_cast<__int128>(segment.tick) + *offset);
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
    __int128 count = *input < 0 ? -static_cast<__int128>(*input) : static_cast<__int128>(*input);
    if (dropFrame) {
        const qint64 drop = nominal == 30 ? 2 : 4;
        const qint64 perTen = nominal * 600 - drop * 9;
        const qint64 perMinute = nominal * 60 - drop;
        const __int128 remainder = count % perTen;
        count += (count / perTen) * drop * 9 +
                 (remainder < drop ? 0 : (remainder - drop) / perMinute * drop);
    }
    const qint64 frames = static_cast<qint64>(count % nominal);
    count /= nominal;
    const qint64 seconds = static_cast<qint64>(count % 60);
    count /= 60;
    const qint64 minutes = static_cast<qint64>(count % 60);
    const qint64 hours = static_cast<qint64>(count / 60);
    const QString result = (*input < 0 ? QStringLiteral("-") : QString()) +
        padded(hours, 2) + QLatin1Char(':') + padded(minutes, 2) + QLatin1Char(':') +
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
    const __int128 totalMinutes = static_cast<__int128>(hours) * 60 + minutes;
    __int128 count = (((static_cast<__int128>(hours) * 60 + minutes) * 60 + seconds) * nominal + frames);
    count -= drop * (totalMinutes - totalMinutes / 10);
    if (match.captured(1) == QStringLiteral("-")) count = -count;
    if (count < std::numeric_limits<qint64>::min() || count > std::numeric_limits<qint64>::max())
        return bad(QStringLiteral("frame index out of range"));
    return good(static_cast<qint64>(count));
}

QVariantMap TimeMap::sampleToPixel(const QString &sample, const QString &viewportOriginSample,
                                    double pixelsPerSample) const {
    const auto position = parseInteger(sample), origin = parseInteger(viewportOriginSample);
    if (!position || !origin) return bad(QStringLiteral("invalid decimal sample position"));
    if (!std::isfinite(pixelsPerSample) || pixelsPerSample <= 0)
        return bad(QStringLiteral("pixels per sample must be positive and finite"));
    const __int128 delta = static_cast<__int128>(*position) - *origin;
    // Rendering is relative to the visible origin, never to absolute session time.
    const long double pixel = static_cast<long double>(delta) * pixelsPerSample;
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
    const __int128 result = static_cast<__int128>(*origin) + static_cast<__int128>(rounded);
    if (result < std::numeric_limits<qint64>::min() || result > std::numeric_limits<qint64>::max())
        return bad(QStringLiteral("sample position out of range"));
    return good(static_cast<qint64>(result));
}

} // namespace QindaTK
